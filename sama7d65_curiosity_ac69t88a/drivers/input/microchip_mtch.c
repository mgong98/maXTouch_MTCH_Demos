/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Microchip MTCH 3xxx capacitive touch controller driver.
 * Supports buttons, sliders, and wheels with interrupt or polling modes.
 */

#define DT_DRV_COMPAT microchip_mtch

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_touch.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>

#include "microchip_mtch.h"

LOG_MODULE_REGISTER(mtch_ctrl_panel, CONFIG_INPUT_LOG_LEVEL);

struct mtch_config {
	struct input_touchscreen_common_config common;
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec rst_gpio;
	struct gpio_dt_spec irq_gpio;
	uint16_t startup_delay_ms;
};

struct mtch_data {
	const struct device *dev;
	struct gpio_callback gpio_cb;
	struct k_work work;
	struct k_mutex i2c_lock;

	/* Resolved I2C address (may differ from DT after probe) */
	uint16_t actual_address;

#ifdef CONFIG_INPUT_MICROCHIP_MTCH_INTERRUPT
	struct gpio_callback irq_cb;
#else
	struct k_timer poll_timer;
#endif

	/* Object table addresses and report IDs */
	uint16_t t2_address;
	uint16_t t5_address;
	uint8_t t5_msg_size;
	uint16_t t6_address;
	uint8_t t6_reportid;
	uint16_t t7_address;
	uint16_t t18_address;
	uint8_t t19_reportid_min;
	uint16_t t38_address;
	uint8_t t38_obj_size;
	uint16_t t44_address;
	uint8_t max_reportid;
	uint16_t t254_address;
	uint8_t t254_obj_size;

	/* Sensor identification */
	struct mtch_sensor_info sensor_info;
	uint16_t t500_address;
	uint8_t t500_reportid_min;
	uint16_t t501_address;
	uint8_t t501_reportid_min;
	uint8_t t501_reportid_max;
	uint16_t t502_address;
	uint8_t t502_reportid_min;
	uint8_t t502_reportid_max;

	/* Demo has 1 wheel, 2 sliders max, 11 buttons */
	/* T502 wheel/slider state */
	uint16_t t502_last_pos[4];
	bool t502_in_detect[4];
	bool wheel_relative;

	/* Chip info */
	struct mtch_info info;
	uint32_t info_crc;
	uint32_t config_crc;
	uint8_t t6_status;
	struct mtch_t7_config t7_cfg;

	/* Message buffer for T44+T5 bulk read */
	uint8_t msg_buf[MTCH_MAX_MSG_COUNT * MTCH_MAX_MSG_SIZE];

#ifdef CONFIG_INPUT_MICROCHIP_MTCH_INTERRUPT
	bool chg_warned;
#endif
};

INPUT_TOUCH_STRUCT_CHECK(struct mtch_config);

/* I2C helpers - use runtime-resolved address from dual-address probe */

static int mtch_i2c_read_reg(const struct device *dev, uint16_t reg,
			     void *buf, size_t len)
{
	const struct mtch_config *cfg = dev->config;
	struct mtch_data *data = dev->data;
	uint8_t addr[2] = { reg & 0xFF, (reg >> 8) & 0xFF };

	return i2c_write_read(cfg->i2c.bus, data->actual_address,
			      addr, sizeof(addr), buf, len);
}

static int mtch_i2c_write_reg(const struct device *dev, uint16_t reg,
			      const void *buf, size_t len)
{
	const struct mtch_config *cfg = dev->config;
	struct mtch_data *data = dev->data;
	uint8_t hdr[2] = { reg & 0xFF, (reg >> 8) & 0xFF };
	struct i2c_msg msgs[2] = {
		{ .buf = hdr, .len = 2, .flags = I2C_MSG_WRITE },
		{ .buf = (uint8_t *)buf, .len = len,
		  .flags = I2C_MSG_WRITE | I2C_MSG_STOP },
	};

	return i2c_transfer(cfg->i2c.bus, msgs, 2, data->actual_address);
}

static int mtch_i2c_read_reg_locked(const struct device *dev, uint16_t reg,
				    void *buf, size_t len)
{
	struct mtch_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	ret = mtch_i2c_read_reg(dev, reg, buf, len);
	k_mutex_unlock(&data->i2c_lock);
	return ret;
}

static int mtch_i2c_write_reg_locked(const struct device *dev, uint16_t reg,
				     const void *buf, size_t len)
{
	struct mtch_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->i2c_lock, K_FOREVER);
	ret = mtch_i2c_write_reg(dev, reg, buf, len);
	k_mutex_unlock(&data->i2c_lock);
	return ret;
}

/* I2C address probe with timeout via system workqueue */

struct mtch_probe_ctx {
	const struct device *bus;
	uint16_t addr;
	int result;
	struct k_work work;
	struct k_sem done;
};

static void mtch_probe_work_fn(struct k_work *work)
{
	struct mtch_probe_ctx *ctx = CONTAINER_OF(work, struct mtch_probe_ctx, work);
	uint8_t reg[2] = { 0, 0 };
	uint8_t dummy;

	ctx->result = i2c_write_read(ctx->bus, ctx->addr,
				     reg, sizeof(reg), &dummy, sizeof(dummy));
	k_sem_give(&ctx->done);
}

static int mtch_probe_address(const struct device *bus, uint16_t addr)
{
	struct mtch_probe_ctx ctx = {
		.bus = bus,
		.addr = addr,
		.result = -ETIMEDOUT,
	};

	k_work_init(&ctx.work, mtch_probe_work_fn);
	k_sem_init(&ctx.done, 0, 1);
	k_work_submit(&ctx.work);

	if (k_sem_take(&ctx.done, K_MSEC(MTCH_PROBE_TIMEOUT_MS)) != 0) {
		LOG_WRN("I2C probe timeout at 0x%02X (bus may be stuck)", addr);
		return -ETIMEDOUT;
	}

	return ctx.result;
}

/* CRC-24 over 16-bit LE words, zero-padded for odd lengths */

static void mtch_crc24_step(uint32_t *crc, uint8_t byte0, uint8_t byte1)
{
	uint32_t result;

	result = ((*crc << 1) ^ ((uint32_t)byte1 << 8 | byte0));
	if (result & 0x1000000U) {
		result ^= MXT_CRC_POLY;
	}
	*crc = result;
}

static uint32_t mtch_crc24(const uint8_t *buf, size_t len)
{
	uint32_t crc = 0;
	const uint8_t *ptr = buf;
	const uint8_t *last = buf + len - 1;

	while (ptr < last) {
		mtch_crc24_step(&crc, *ptr, *(ptr + 1));
		ptr += 2;
	}

	if (ptr == last) {
		mtch_crc24_step(&crc, *ptr, 0);
	}

	return crc & 0x00FFFFFFU;
}

/* Object table helpers */

static uint16_t mtch_obj_address(const struct mtch_object *obj)
{
	return (uint16_t)obj->start_pos_lsb | ((uint16_t)obj->start_pos_msb << 8);
}

static uint8_t mtch_obj_size(const struct mtch_object *obj)
{
	return obj->size_minus_one + 1;
}

static uint8_t mtch_obj_instances(const struct mtch_object *obj)
{
	return obj->instances_minus_one + 1;
}

/* Read and parse T254 extended object table (16-bit object types) */
static int mtch_read_t254_table(const struct device *dev)
{
	struct mtch_data *data = dev->data;
	uint8_t t254_buf[32];
	uint32_t stored_crc, calc_crc;
	int ret;

	if (data->t254_address == 0 || data->t254_obj_size == 0) {
		return 0;
	}

	ret = mtch_i2c_read_reg_locked(dev, data->t254_address,
				       t254_buf, data->t254_obj_size);
	if (ret < 0) {
		LOG_ERR("Failed to read T254: %d", ret);
		return ret;
	}

	/* Validate T254 CRC (3 bytes at end) */
	size_t crc_offset = data->t254_obj_size - 3;
	stored_crc = (uint32_t)t254_buf[crc_offset] |
		     ((uint32_t)t254_buf[crc_offset + 1] << 8) |
		     ((uint32_t)t254_buf[crc_offset + 2] << 16);
	calc_crc = mtch_crc24(t254_buf, crc_offset);

	if (stored_crc != calc_crc) {
		LOG_ERR("T254 CRC mismatch: stored=0x%06X calc=0x%06X",
			stored_crc, calc_crc);
		return -EINVAL;
	}

	LOG_INF("T254 CRC validated: 0x%06X", stored_crc);

	/* Parse T254 entries (up to 3 extended objects per block, 7 bytes each) */
	uint8_t reportid = data->max_reportid;
	for (uint8_t i = 0; i < 3; i++) {
		uint8_t *entry = &t254_buf[i * 7];
		uint16_t type = (uint16_t)entry[0] | ((uint16_t)entry[1] << 8);
		uint16_t addr = (uint16_t)entry[2] | ((uint16_t)entry[3] << 8);

		if (addr == 0) {
			continue;
		}

		uint8_t size = entry[4] + 1;
		uint8_t instances = entry[5] + 1;
		uint8_t num_ids = entry[6];

		LOG_INF("T%u (ext): addr=0x%04X size=%u instances=%u ids=%u",
			type, addr, size, instances, num_ids);

		switch (type) {
		case TOUCH_MTCHKEYARRAY_T500:
			data->t500_reportid_min = reportid;
			break;
		case SPT_MTCHKEYCTRL_T501:
			data->t501_reportid_min = reportid;
			data->t501_reportid_max = reportid + num_ids * instances - 1;
			break;
		case SPT_MTCHSLIDERCTRL_T502:
			data->t502_reportid_min = reportid;
			data->t502_reportid_max = reportid + num_ids * instances - 1;
			break;
		}

		reportid += num_ids * instances;
	}

	data->max_reportid = reportid - 1;
	return 0;
}

static int mtch_read_info_block(const struct device *dev)
{
	const struct mtch_config *cfg = dev->config;
	struct mtch_data *data = dev->data;
	int ret;

	ret = mtch_i2c_read_reg_locked(dev, 0x0000, &data->info, MTCH_INFO_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to read info block: %d", ret);
		return ret;
	}

	LOG_INF("family=0x%02X variant=0x%02X ver=%u.%u objects=%u matrix=%ux%u",
		data->info.family_id, data->info.variant_id,
		data->info.version >> 4, data->info.version & 0xF,
		data->info.num_objects,
		data->info.matrix_x_size, data->info.matrix_y_size);

	if (data->info.num_objects == 0 || data->info.num_objects > MTCH_MAX_OBJECTS) {
		LOG_ERR("Invalid object count: %u", data->info.num_objects);
		return -ENODEV;
	}

	/* Read full info block: header + object table + 3-byte CRC */
	size_t table_size = data->info.num_objects * MTCH_OBJECT_SIZE;
	size_t block_size = MTCH_INFO_SIZE + table_size + MTCH_INFO_CHECKSUM_SIZE;
	uint8_t info_block[MTCH_MAX_INFO_SIZE];

	if (block_size > sizeof(info_block)) {
		LOG_ERR("Info block too large: %zu", block_size);
		return -ENOMEM;
	}

	ret = mtch_i2c_read_reg_locked(dev, 0x0000, info_block, block_size);
	if (ret < 0) {
		LOG_ERR("Failed to read full info block: %d", ret);
		return ret;
	}

	/* Validate CRC-24 over header + object table */
	size_t crc_offset = MTCH_INFO_SIZE + table_size;
	uint32_t stored_crc = (uint32_t)info_block[crc_offset] |
			      ((uint32_t)info_block[crc_offset + 1] << 8) |
			      ((uint32_t)info_block[crc_offset + 2] << 16);
	uint32_t calc_crc = mtch_crc24(info_block, crc_offset);

	if (stored_crc != calc_crc) {
		LOG_ERR("Info block CRC mismatch: stored=0x%06X calc=0x%06X",
			stored_crc, calc_crc);
		return -EINVAL;
	}

	data->info_crc = stored_crc;
	LOG_INF("Info block CRC: 0x%06X (OK)", stored_crc);

	/* Parse object table and extract addresses / report IDs */
	const struct mtch_object *obj_table =
		(const struct mtch_object *)&info_block[MTCH_INFO_SIZE];
	uint8_t reportid = 1;

	data->t5_address = 0;
	data->t6_address = 0;
	data->t7_address = 0;
	data->t18_address = 0;
	data->t38_address = 0;
	data->t38_obj_size = 0;
	data->t44_address = 0;
	data->t254_address = 0;
	data->t254_obj_size = 0;
	data->t500_reportid_min = 0;
	data->t501_reportid_min = 0;
	data->t501_reportid_max = 0;
	data->t502_reportid_min = 0;
	data->t502_reportid_max = 0;

	for (uint8_t i = 0; i < data->info.num_objects; i++) {
		const struct mtch_object *obj = &obj_table[i];
		uint16_t addr = mtch_obj_address(obj);
		uint8_t size = mtch_obj_size(obj);
		uint8_t instances = mtch_obj_instances(obj);
		uint8_t num_ids = obj->num_report_ids;

		LOG_DBG("T%u: addr=0x%04X size=%u instances=%u report_ids=%u",
			obj->type, addr, size, instances, num_ids);

		switch (obj->type) {
		case GEN_MESSAGE_T5:
			data->t5_address = addr;
			data->t5_msg_size = size - 1;
			break;
		case GEN_COMMAND_T6:
			data->t6_address = addr;
			data->t6_reportid = reportid;
			break;
		case GEN_POWER_T7:
			data->t7_address = addr;
			break;
		case SPT_COMMSCONFIG_T18:
			data->t18_address = addr;
			break;
		case SPT_USERDATA_T38:
			data->t38_address = addr;
			data->t38_obj_size = size;
			break;
		case SPT_MESSAGECOUNT_T44:
			data->t44_address = addr;
			break;
		case GEN_EXTENDEDOBJECTTABLE_T254:
			data->t254_address = addr;
			data->t254_obj_size = size;
			break;
		}

		reportid += num_ids * instances;
	}

	data->max_reportid = reportid - 1;

	if (data->t5_address == 0 || data->t6_address == 0 ||
	    data->t44_address == 0) {
		LOG_ERR("Missing required objects (T5/T6/T44)");
		return -ENODEV;
	}

	LOG_INF("T5=0x%04X(%u) T6=0x%04X T7=0x%04X T38=0x%04X T44=0x%04X T254=0x%04X",
		data->t5_address, data->t5_msg_size, data->t6_address,
		data->t7_address, data->t38_address, data->t44_address,
		data->t254_address);

	/* Stage 2: Read and parse T254 extended object table if present */
	if (data->t254_address != 0) {
		ret = mtch_read_t254_table(dev);
		if (ret < 0) {
			LOG_WRN("Failed to parse T254 extended objects: %d", ret);
		} else {
			LOG_INF("Extended objects: T500=%u T501=%u-%u T502=%u-%u",
				data->t500_reportid_min,
				data->t501_reportid_min, data->t501_reportid_max,
				data->t502_reportid_min, data->t502_reportid_max);
		}
	}

	return 0;
}

/* T7 power configuration */

static int mtch_read_t7_config(const struct device *dev)
{
	struct mtch_data *data = dev->data;
	int ret;

	if (data->t7_address == 0) {
		LOG_WRN("T7 not available");
		return 0;
	}

	ret = mtch_i2c_read_reg_locked(dev, data->t7_address,
				       &data->t7_cfg, sizeof(data->t7_cfg));
	if (ret < 0) {
		LOG_ERR("Failed to read T7: %d", ret);
		return ret;
	}

	LOG_INF("T7 power: active=%ums idle=%ums actv2idle=%u",
		data->t7_cfg.active, data->t7_cfg.idle, data->t7_cfg.actv2idle);

	return 0;
}

/* Set T18 CHG line to Mode 1 (level-triggered) for reliable drain loop */
static int mtch_set_chg_mode1(const struct device *dev)
{
	struct mtch_data *data = dev->data;
	uint8_t ctrl;
	int ret;

	if (data->t18_address == 0) {
		LOG_WRN("T18 not available, CHG mode unchanged");
		return -ENOTSUP;
	}

	ret = mtch_i2c_read_reg_locked(dev, data->t18_address, &ctrl, 1);
	if (ret < 0) {
		LOG_ERR("Failed to read T18: %d", ret);
		return ret;
	}

	if (ctrl & T18_CTRL_MODE) {
		LOG_INF("T18: CHG already in Mode 1 (level-triggered)");
		return 0;
	}

	ctrl |= T18_CTRL_MODE;
	ret = mtch_i2c_write_reg_locked(dev, data->t18_address, &ctrl, 1);
	if (ret < 0) {
		LOG_ERR("Failed to write T18: %d", ret);
		return ret;
	}

	LOG_INF("T18: CHG set to Mode 1 (level-triggered)");
	return 0;
}

/* Read T38 and detect sensor type
 *
 * T38 signature patterns:
 * MTCH3380P Self Cap:   {0x03, 0x03, 0x08, 0x00, 0x00}
 * MTCH3380P Mutual Cap: {0x03, 0x03, 0x08, 0x00, 0x01}
 * MTCH3240P Self Cap:   {0x03, 0x02, 0x04, 0x00, 0x00}
 * MTCH3240P Mutual Cap: {0x03, 0x02, 0x04, 0x00, 0x01}
 */
static int mtch_detect_sensor(const struct device *dev)
{
	struct mtch_data *data = dev->data;
	uint8_t t38_buf[8];
	int ret;

	data->sensor_info.device = MTCH_DEVICE_UNKNOWN;
	data->sensor_info.sensor = MTCH_SENSOR_UNKNOWN;

	if (data->t38_address == 0 || data->t38_obj_size < 5) {
		LOG_WRN("T38 not available or too small for sensor detection");
		return -ENOTSUP;
	}

	ret = mtch_i2c_read_reg_locked(dev, data->t38_address, t38_buf,
				       MIN(data->t38_obj_size, sizeof(t38_buf)));
	if (ret < 0) {
		LOG_ERR("Failed to read T38: %d", ret);
		return ret;
	}

	LOG_INF("T38 data: [%02X %02X %02X %02X %02X]",
		t38_buf[0], t38_buf[1], t38_buf[2], t38_buf[3], t38_buf[4]);

	/* Check for valid signature (byte 0 must be 0x03) */
	if (t38_buf[0] != 0x03) {
		LOG_WRN("T38: unexpected signature byte 0: 0x%02X", t38_buf[0]);
		return -EINVAL;
	}

	/* Detect device type from bytes 1 and 2 */
	if (t38_buf[1] == 0x03 && t38_buf[2] == 0x08) {
		data->sensor_info.device = MTCH_DEVICE_3380P;
		LOG_INF("Detected device: MTCH3380P");
	} else if (t38_buf[1] == 0x02 && t38_buf[2] == 0x04) {
		data->sensor_info.device = MTCH_DEVICE_3240P;
		LOG_INF("Detected device: MTCH3240P");
	} else {
		LOG_WRN("T38: unknown device signature [%02X %02X]",
			t38_buf[1], t38_buf[2]);
		return -EINVAL;
	}

	/* Detect sensor type from byte 4 */
	if (t38_buf[4] == 0x00) {
		data->sensor_info.sensor = MTCH_SENSOR_SELF_CAP;
		LOG_INF("Detected sensor: Self Capacitive");
	} else if (t38_buf[4] == 0x01) {
		data->sensor_info.sensor = MTCH_SENSOR_MUTUAL_CAP;
		LOG_INF("Detected sensor: Mutual Capacitive");
	} else {
		LOG_WRN("T38: unknown sensor type: 0x%02X", t38_buf[4]);
		return -EINVAL;
	}

	return 0;
}

/* Configure device after info block is read */
static int mtch_configure_dev(const struct device *dev)
{
	struct mtch_data *data = dev->data;

	/* Detect sensor type from T38 */
	mtch_detect_sensor(dev);

	LOG_INF("Device configured:");
	LOG_INF("  T5 msg size: %u", data->t5_msg_size);
	LOG_INF("  Max reportid: %u", data->max_reportid);
	LOG_INF("  T502 sliders/wheels: min_id=%u max_id=%u",
		data->t502_reportid_min, data->t502_reportid_max);

	/* Log sensor info */
	const char *device_str = "Unknown";
	const char *sensor_str = "Unknown";

	switch (data->sensor_info.device) {
	case MTCH_DEVICE_3380P:
		device_str = "MTCH3380P";
		break;
	case MTCH_DEVICE_3240P:
		device_str = "MTCH3240P";
		break;
	default:
		break;
	}

	switch (data->sensor_info.sensor) {
	case MTCH_SENSOR_SELF_CAP:
		sensor_str = "Self Cap";
		break;
	case MTCH_SENSOR_MUTUAL_CAP:
		sensor_str = "Mutual Cap";
		break;
	default:
		break;
	}

	LOG_INF("  Sensor: %s %s", device_str, sensor_str);

	return 0;
}

/* Message processing */

static void mtch_process_t6_message(const struct device *dev, const uint8_t *msg)
{
	struct mtch_data *data = dev->data;
	uint8_t status = msg[1];

	data->t6_status = status;

	if (status & T6_STATUS_RESET) {
		LOG_INF("T6: reset complete");
	}
	if (status & T6_STATUS_CAL) {
		LOG_INF("T6: calibrating");
	}
	if (status & T6_STATUS_CFGERR) {
		LOG_ERR("T6: configuration error");
	}
	if (status & T6_STATUS_SIGERR) {
		LOG_ERR("T6: signal error");
	}
	if (status & T6_STATUS_OFL) {
		LOG_ERR("T6: acquisition overflow");
	}

	data->config_crc = (uint32_t)msg[2] |
			   ((uint32_t)msg[3] << 8) |
			   ((uint32_t)msg[4] << 16);

	if (status == 0) {
		LOG_INF("T6: config CRC=0x%06X", data->config_crc);
	}
}

/* T500 - Touch Key Array */
static void mtch_process_t500_message(const struct device *dev, const uint8_t *msg)
{
	struct mtch_data *data = dev->data;
	uint8_t report_id = msg[0];
	uint8_t status = msg[1];

	LOG_DBG("T500 (report_id=%u): status=0x%02X", report_id, status);
}

/* Map report ID to button code for MTCH3380P mutual cap sensor
 * Based on 3380P_mutual_sensor.png layout.
 * Returns values that after btn_mapping[] in input_handlers.c
 * result in the correct UI button:
 *   Sensor BTN 0 (0x50) → UI BTN 0: return 10 (btn_mapping[10]=0)
 *   Sensor BTN 1 (0x55) → UI BTN 1: return 8  (btn_mapping[8]=1)
 *   Sensor BTN 2 (0x52) → UI BTN 2: return 9  (btn_mapping[9]=2)
 *   Sensor BTN 3 (0x56) → UI BTN 3: return 6  (btn_mapping[6]=3)
 *   Sensor BTN 4 (0x54) → UI BTN 4: return 7  (btn_mapping[7]=4)
 *   Sensor BTN 5 (0x51) → UI BTN 5: return 5  (btn_mapping[5]=5)
 *   Sensor BTN 6 (0x53) → UI BTN 6: return 4  (btn_mapping[4]=6)
 *   Sensor BTN 7 (0x44) → UI BTN 7: return 3  (btn_mapping[3]=7)
 *   Sensor BTN 8 (0x45) → UI BTN 8: return 2  (btn_mapping[2]=8)
 *   Sensor BTN 9 (0x46) → UI BTN 9: return 1  (btn_mapping[1]=9)
 *   Sensor BTN 10 (0x47) → UI BTN 10: return 0 (btn_mapping[0]=10)
 */
static int mtch_3380p_mutual_report_to_btn(uint8_t report_id)
{
	switch (report_id) {
	case 0x50: return 10;  /* Sensor BTN 0 → UI BTN 0 */
	case 0x55: return 9;   /* Sensor BTN 1 → UI BTN 2 (swapped) */
	case 0x52: return 8;   /* Sensor BTN 2 → UI BTN 1 (swapped) */
	case 0x56: return 7;   /* Sensor BTN 3 → UI BTN 4 (swapped) */
	case 0x54: return 6;   /* Sensor BTN 4 → UI BTN 3 (swapped) */
	case 0x51: return 4;   /* Sensor BTN 5 → UI BTN 6 (swapped) */
	case 0x53: return 5;   /* Sensor BTN 6 → UI BTN 5 (swapped) */
	case 0x44: return 3;   /* Sensor BTN 7 → UI BTN 7 */
	case 0x45: return 2;   /* Sensor BTN 8 → UI BTN 8 */
	case 0x46: return 1;   /* Sensor BTN 9 → UI BTN 9 */
	case 0x47: return 0;   /* Sensor BTN 10 → UI BTN 10 */
	default:   return -1;
	}
}

/* Map report ID to button index for MTCH3240P sensor
 * Report IDs are mapped to button indices that go through btn_mapping[]
 * in input_handlers.c to get the final UI button number.
 *
 * Report IDs (buttons 0-4 and 7-10, no 5 or 6):
 *   0x2E (46) → Button 0: return 10 (btn_mapping[10]=0)
 *   0x2C (44) → Button 1: return 8  (btn_mapping[8]=1)
 *   0x2D (45) → Button 2: return 9  (btn_mapping[9]=2)
 *   0x2B (43) → Button 3: return 6  (btn_mapping[6]=3)
 *   0x2A (42) → Button 4: return 7  (btn_mapping[7]=4)
 *   0x3C (60) → Button 7: return 3  (btn_mapping[3]=7)
 *   0x3D (61) → Button 8: return 2  (btn_mapping[2]=8)
 *   0x3E (62) → Button 9: return 1  (btn_mapping[1]=9)
 *   0x3F (63) → Button 10: return 0 (btn_mapping[0]=10)
 */
static int mtch_3240p_selfcap_report_to_btn(uint8_t report_id)
{
	switch (report_id) {
	case 0x2E: return 10;  /* Button 0 */
	case 0x2C: return 8;   /* Button 1 */
	case 0x2D: return 9;   /* Button 2 */
	case 0x2B: return 6;   /* Button 3 */
	case 0x2A: return 7;   /* Button 4 */
	case 0x3C: return 3;   /* Button 7 */
	case 0x3D: return 2;   /* Button 8 */
	case 0x3E: return 1;   /* Button 9 */
	case 0x3F: return 0;   /* Button 10 */
	default:   return -1;
	}
}

/* Map report ID to button index for MTCH3240P mutual cap sensor
 * Only buttons 7-10 are available on mutual cap sensor.
 * Report IDs (inverted mapping):
 *   0x45 (69) → Button 10: return 0 (btn_mapping[0]=10)
 *   0x44 (68) → Button 9: return 1 (btn_mapping[1]=9)
 *   0x43 (67) → Button 8: return 2 (btn_mapping[2]=8)
 *   0x42 (66) → Button 7: return 3 (btn_mapping[3]=7)
 */
static int mtch_3240p_mutual_report_to_btn(uint8_t report_id)
{
	switch (report_id) {
	case 0x45: return 0;   /* Button 10 */
	case 0x44: return 1;   /* Button 9 */
	case 0x43: return 2;   /* Button 8 */
	case 0x42: return 3;   /* Button 7 */
	default:   return -1;
	}
}

/* T501 - Key Controller / Button Array */
static void mtch_process_t501_message(const struct device *dev, const uint8_t *msg)
{
	struct mtch_data *data = dev->data;
	uint8_t report_id = msg[0];
	uint8_t status = msg[1];
	uint32_t key_index = report_id - data->t501_reportid_min;
	bool pressed = (status & 0x80) != 0;
	int btn_code = -1;

	LOG_INF("T501 button (key=%u): pressed=%d (reportid=%u/0x%02X)",
		key_index, pressed, report_id, report_id);

	/* Handle sensor-specific report ID mappings */
	if (data->sensor_info.device == MTCH_DEVICE_3380P &&
	    data->sensor_info.sensor == MTCH_SENSOR_MUTUAL_CAP) {
		/* MTCH3380P mutual cap - special mapping */
		int btn_idx = mtch_3380p_mutual_report_to_btn(report_id);
		if (btn_idx >= 0) {
			btn_code = 0x100 + btn_idx;
		}
	} else if (data->sensor_info.device == MTCH_DEVICE_3240P) {
		/* MTCH3240P - different mappings for self cap vs mutual cap */
		int btn_idx;
		if (data->sensor_info.sensor == MTCH_SENSOR_MUTUAL_CAP) {
			btn_idx = mtch_3240p_mutual_report_to_btn(report_id);
		} else {
			btn_idx = mtch_3240p_selfcap_report_to_btn(report_id);
		}
		if (btn_idx >= 0) {
			btn_code = 0x100 + btn_idx;
		}
	} else {
		/* Default mapping for MTCH3380P self cap:
		 * Report ID 58-68 (0x3A-0x44) → button 0-10
		 * Buttons 7-10 (report IDs 58-61) are inverted on sensor
		 */
		if (report_id >= 58 && report_id <= 68) {
			int idx = report_id - 58;
			/* Fix inversion for buttons 7-10 (report IDs 58-61) */
			if (idx >= 0 && idx <= 3) {
				idx = 3 - idx;  /* 0→3, 1→2, 2→1, 3→0 */
			}
			btn_code = 0x100 + idx;
		}
	}

	if (btn_code >= 0) {
		input_report_key(dev, btn_code, pressed, false, K_NO_WAIT);
	}
}

/* T502 - Slider/Wheel Controller */
static void mtch_process_t502_message(const struct device *dev, const uint8_t *msg)
{
	struct mtch_data *data = dev->data;
	uint8_t report_id = msg[0];
	uint8_t status = msg[1];
	uint16_t position = (uint16_t)msg[2] | ((uint16_t)msg[3] << 8);
	uint32_t slot_index = report_id - data->t502_reportid_min;
	bool detected = (status & 0x80) != 0;
	bool pos_changed = (status & 0x01) != 0;

	LOG_INF("T502 (slot=%u): pos=%u detect=%d poschange=%d (reportid=%u, min=%u)",
		slot_index, position, detected, pos_changed, report_id, data->t502_reportid_min);

	/* Track position changes */
	if (slot_index < 3) {
		data->t502_last_pos[slot_index] = position;
	}

	/* Map report IDs to UI codes:
	 * Self cap sensor:
	 *   Report ID 35 (0x23) → wheel (code 0)
	 *   Report ID 36 (0x24) → slider1 (code 1)
	 *   Report ID 37 (0x25) → slider2 (code 2)
	 *
	 * MTCH3380P Mutual cap sensor:
	 *   Slider 2 is not available
	 */
	switch (report_id) {
	case 35:
		/* MTCH3380P mutual cap sensor has inverted wheel direction */
		if (data->sensor_info.device == MTCH_DEVICE_3380P &&
		    data->sensor_info.sensor == MTCH_SENSOR_MUTUAL_CAP) {
			position = 255 - position;
		}
		LOG_INF("T502 WHEEL reporting: position=%u", position);
		input_report_abs(dev, 0, position, false, K_NO_WAIT);
		break;
	case 36:
		LOG_INF("T502 SLIDER1 reporting: position=%u", position);
		input_report_abs(dev, 1, position, false, K_NO_WAIT);
		break;
	case 37:
		/* Slider 2 not available on MTCH3380P mutual cap sensor */
		if (data->sensor_info.device == MTCH_DEVICE_3380P &&
		    data->sensor_info.sensor == MTCH_SENSOR_MUTUAL_CAP) {
			LOG_DBG("T502 SLIDER2 disabled for mutual cap sensor");
		} else {
			LOG_INF("T502 SLIDER2 reporting: position=%u", position);
			input_report_abs(dev, 2, position, false, K_NO_WAIT);
		}
		break;
	default:
		LOG_INF("T502 unknown report_id=%u, position=%u", report_id, position);
		break;
	}
}

static void mtch_process_messages(const struct device *dev, uint8_t count)
{
	struct mtch_data *data = dev->data;
	int i;

	for (i = 0; i < count; i++) {
		uint8_t *msg = &data->msg_buf[i * data->t5_msg_size];
		uint8_t report_id = msg[0];

		if (report_id == 0 || report_id == 0xFF) {
			continue;
		}

		if (report_id == data->t6_reportid) {
			mtch_process_t6_message(dev, msg);
			continue;
		}

		if (data->t500_reportid_min && report_id == data->t500_reportid_min) {
			mtch_process_t500_message(dev, msg);
			continue;
		}

		if (data->t501_reportid_min &&
		    report_id >= data->t501_reportid_min &&
		    report_id <= data->t501_reportid_max) {
			mtch_process_t501_message(dev, msg);
			continue;
		}

		if (data->t502_reportid_min &&
		    report_id >= data->t502_reportid_min &&
		    report_id <= data->t502_reportid_max) {
			mtch_process_t502_message(dev, msg);
			continue;
		}

		LOG_DBG("Unhandled report_id=%u "
			"[%02X %02X %02X %02X %02X %02X]",
			report_id,
			msg[0], msg[1], msg[2], msg[3], msg[4], msg[5]);
	}
}

/*
 * Read pending messages via T44 count + T5 bulk read.
 * Returns the number of messages processed, or negative on error.
 */
static int mtch_read_messages(const struct device *dev)
{
	struct mtch_data *data = dev->data;
	uint8_t count = 0;
	int ret;

	k_mutex_lock(&data->i2c_lock, K_FOREVER);

	ret = mtch_i2c_read_reg(dev, data->t44_address, &count, 1);
	if (ret < 0) {
		LOG_ERR("Failed to read T44: %d", ret);
		k_mutex_unlock(&data->i2c_lock);
		return ret;
	}

	if (count == 0) {
		/* Read T5 once to deassert CHG and catch late messages */
		ret = mtch_i2c_read_reg(dev, data->t5_address,
				       data->msg_buf, data->t5_msg_size);
		k_mutex_unlock(&data->i2c_lock);
		if (ret == 0 && data->msg_buf[0] != 0 &&
		    data->msg_buf[0] != 0xFF) {
			mtch_process_messages(dev, 1);
			return 1;
		}
		return 0;
	}

	if (count > MTCH_MAX_MSG_COUNT) {
		count = MTCH_MAX_MSG_COUNT;
	}
	if (count > data->max_reportid) {
		count = data->max_reportid;
	}

	size_t read_size = (size_t)count * data->t5_msg_size;

	ret = mtch_i2c_read_reg(dev, data->t5_address, data->msg_buf, read_size);
	k_mutex_unlock(&data->i2c_lock);

	if (ret < 0) {
		LOG_ERR("Failed to bulk read messages: %d", ret);
		return ret;
	}

	mtch_process_messages(dev, count);
	return (int)count;
}

/*
 * Work handler: drain messages via T44+T5 bulk reads.
 * IRQ mode: ONESHOT pattern - drain until CHG goes high, then re-enable.
 * Poll mode: drain until T44 returns 0.
 */
static void mtch_work_handler(struct k_work *work)
{
	struct mtch_data *data = CONTAINER_OF(work, struct mtch_data, work);
	const struct device *dev = data->dev;

#ifdef CONFIG_INPUT_MICROCHIP_MTCH_INTERRUPT
	const struct mtch_config *cfg = dev->config;

	int passes = 0;

	do {
		for (int tries = 0; tries < 10; tries++) {
			int ret = mtch_read_messages(dev);

			if (ret <= 0) {
				break;
			}
		}
		passes++;
		if (passes >= 50 && !data->chg_warned) {
			data->chg_warned = true;
			LOG_WRN("CHG stuck low after drain - "
				"check pull-up on CHG line "
				"(open-drain, needs pull-up to VddIO)");
		}
		k_sleep(K_MSEC(1));
	} while (cfg->irq_gpio.port != NULL &&
		 gpio_pin_get_dt(&cfg->irq_gpio));

	/* Re-enable edge interrupt now that CHG is deasserted */
	if (cfg->irq_gpio.port != NULL) {
		gpio_pin_interrupt_configure_dt(&cfg->irq_gpio,
						GPIO_INT_EDGE_TO_ACTIVE);
	}
#else
	for (int tries = 0; tries < 10; tries++) {
		int ret = mtch_read_messages(dev);

		if (ret <= 0) {
			break;
		}
	}
#endif
}

/* IRQ / polling handlers */

#ifdef CONFIG_INPUT_MICROCHIP_MTCH_INTERRUPT
static void mtch_irq_handler(const struct device *gpio, struct gpio_callback *cb,
			     uint32_t pins)
{
	struct mtch_data *data = CONTAINER_OF(cb, struct mtch_data, irq_cb);
	const struct mtch_config *cfg = data->dev->config;

	/* Disable interrupt; work handler re-enables after drain */
	gpio_pin_interrupt_configure_dt(&cfg->irq_gpio, GPIO_INT_DISABLE);
	k_work_submit(&data->work);
}
#else
static void mtch_timer_handler(struct k_timer *timer)
{
	struct mtch_data *data = CONTAINER_OF(timer, struct mtch_data, poll_timer);

	k_work_submit(&data->work);
}
#endif

/* Reset helpers */

static int mtch_hw_reset(const struct device *dev)
{
	const struct mtch_config *cfg = dev->config;

	if (cfg->rst_gpio.port == NULL) {
		return -ENOTSUP;
	}

	gpio_pin_set_dt(&cfg->rst_gpio, 1);
	k_sleep(K_MSEC(20));
	gpio_pin_set_dt(&cfg->rst_gpio, 0);
	k_sleep(K_MSEC(1000));

	return 0;
}

/* Soft reset via T6 command, polls for reset-complete message */
static int mtch_soft_reset(const struct device *dev)
{
	struct mtch_data *data = dev->data;
	uint8_t cmd = 1;
	int ret;

	ret = mtch_i2c_write_reg_locked(dev, data->t6_address + T6_CMD_RESET,
				       &cmd, sizeof(cmd));
	if (ret < 0) {
		LOG_ERR("Failed to send soft reset: %d", ret);
		return ret;
	}

	k_sleep(K_MSEC(100));

	for (int i = 0; i < 30; i++) {
		uint8_t count = 0;

		ret = mtch_i2c_read_reg_locked(dev, data->t44_address,
					      &count, 1);
		if (ret < 0 || count == 0) {
			k_sleep(K_MSEC(100));
			continue;
		}

		uint8_t msg[MTCH_MAX_MSG_SIZE];

		for (uint8_t m = 0; m < count && m < MTCH_MAX_MSG_COUNT; m++) {
			ret = mtch_i2c_read_reg_locked(dev, data->t5_address,
						      msg, data->t5_msg_size);
			if (ret < 0) {
				break;
			}

			if (msg[0] == data->t6_reportid) {
				mtch_process_t6_message(dev, msg);
				if (data->t6_status & T6_STATUS_RESET) {
					return 0;
				}
			}
		}

		k_sleep(K_MSEC(100));
	}

	LOG_WRN("Soft reset timeout (continuing anyway)");
	return 0;
}

/* Drain pending messages by reading T5 until invalid report ID */
static int mtch_drain_messages(const struct device *dev)
{
	struct mtch_data *data = dev->data;
	uint8_t msg[16];
	int ret;
	int count = 0;

	for (int i = 0; i < 50; i++) {
		ret = mtch_i2c_read_reg_locked(dev, data->t5_address, msg,
					      data->t5_msg_size);
		if (ret < 0) {
			break;
		}

		if (msg[0] == 0 || msg[0] == 0xFF) {
			break;
		}

		if (msg[0] == data->t6_reportid) {
			mtch_process_t6_message(dev, msg);
		}

		count++;
	}

	if (count > 0) {
		LOG_DBG("Drained %d pending messages", count);
	}

	return 0;
}

/* PM support */

#ifdef CONFIG_PM_DEVICE
static int mtch_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct mtch_data *data = dev->data;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND: {
		struct mtch_t7_config sleep_cfg = {
			.idle = 0, .active = 0, .actv2idle = 0
		};

		mtch_i2c_write_reg_locked(dev, data->t7_address,
					 &sleep_cfg, sizeof(sleep_cfg));

#ifdef CONFIG_INPUT_MICROCHIP_MTCH_INTERRUPT
		const struct mtch_config *cfg = dev->config;

		if (cfg->irq_gpio.port != NULL) {
			gpio_pin_interrupt_configure_dt(&cfg->irq_gpio,
							GPIO_INT_DISABLE);
		}
#else
		k_timer_stop(&data->poll_timer);
#endif
		LOG_INF("Suspended");
		break;
	}
	case PM_DEVICE_ACTION_RESUME: {
		mtch_i2c_write_reg_locked(dev, data->t7_address,
					  &data->t7_cfg, sizeof(data->t7_cfg));

#ifdef CONFIG_INPUT_MICROCHIP_MTCH_INTERRUPT
		const struct mtch_config *cfg = dev->config;

		if (cfg->irq_gpio.port != NULL) {
			gpio_pin_interrupt_configure_dt(&cfg->irq_gpio,
							GPIO_INT_EDGE_TO_ACTIVE);
		}
#else
		k_timer_start(&data->poll_timer,
			      K_MSEC(CONFIG_INPUT_MICROCHIP_MTCH_PERIOD_MS),
			      K_MSEC(CONFIG_INPUT_MICROCHIP_MTCH_PERIOD_MS));
#endif
		mtch_drain_messages(dev);
		LOG_INF("Resumed");
		break;
	}
	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif /* CONFIG_PM_DEVICE */

/* Device initialization */

static int mtch_init(const struct device *dev)
{
	const struct mtch_config *cfg = dev->config;
	struct mtch_data *data = dev->data;
	int ret;

	data->dev = dev;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	k_mutex_init(&data->i2c_lock);
	k_work_init(&data->work, mtch_work_handler);

	/* Hardware reset if GPIO available */
	if (cfg->rst_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->rst_gpio)) {
			LOG_ERR("Reset GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->rst_gpio,
					    GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure reset GPIO: %d", ret);
			return ret;
		}

		ret = mtch_hw_reset(dev);
		if (ret < 0) {
			LOG_WRN("HW reset failed: %d (continuing)", ret);
		}
	}

	if (cfg->startup_delay_ms > 0) {
		k_sleep(K_MSEC(cfg->startup_delay_ms));
	}

	/* Probe configured address, fall back to alternate (ADDR pin) */
	uint16_t dt_addr = cfg->i2c.addr;
	uint16_t alt_addr = (dt_addr == I2C_ADDR_PRIMARY)
			    ? I2C_ADDR_SECONDARY : I2C_ADDR_PRIMARY;

	LOG_INF("Probing on %s at 0x%02X...", cfg->i2c.bus->name, dt_addr);
	ret = mtch_probe_address(cfg->i2c.bus, dt_addr);

	if (ret == 0) {
		data->actual_address = dt_addr;
	} else {
		LOG_WRN("No response at 0x%02X (%d), trying 0x%02X...",
			dt_addr, ret, alt_addr);
		ret = mtch_probe_address(cfg->i2c.bus, alt_addr);
		if (ret == 0) {
			data->actual_address = alt_addr;
			LOG_WRN("Device found at 0x%02X but 0x%02X was "
				"configured in devicetree",
				alt_addr, dt_addr);
		} else {
			LOG_ERR("Not found at 0x%02X or 0x%02X "
				"(touch disabled)", dt_addr, alt_addr);
			return -ENODEV;
		}
	}

	LOG_INF("Detected at 0x%02X", data->actual_address);

	ret = mtch_read_info_block(dev);
	if (ret < 0) {
		return ret;
	}

	ret = mtch_read_t7_config(dev);
	if (ret < 0) {
		return ret;
	}

	/* Reset and drain pending messages before enabling IRQ/polling */
	mtch_soft_reset(dev);
	mtch_drain_messages(dev);

	/* Configure device */
	mtch_configure_dev(dev);

#ifdef CONFIG_INPUT_MICROCHIP_MTCH_INTERRUPT
	mtch_set_chg_mode1(dev);

	if (cfg->irq_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->irq_gpio)) {
			LOG_ERR("IRQ GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->irq_gpio,
					    GPIO_INPUT | GPIO_PULL_UP);
		if (ret < 0) {
			LOG_ERR("Failed to configure IRQ GPIO: %d", ret);
			return ret;
		}

		gpio_init_callback(&data->irq_cb, mtch_irq_handler,
				   BIT(cfg->irq_gpio.pin));

		ret = gpio_add_callback(cfg->irq_gpio.port, &data->irq_cb);
		if (ret < 0) {
			LOG_ERR("Failed to add IRQ callback: %d", ret);
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(&cfg->irq_gpio,
						      GPIO_INT_EDGE_TO_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure IRQ: %d", ret);
			return ret;
		}

		LOG_INF("Interrupt mode");

		/* Drain messages that arrived before interrupt was enabled */
		mtch_drain_messages(dev);
	}
#else
	k_timer_init(&data->poll_timer, mtch_timer_handler, NULL);
	k_timer_start(&data->poll_timer,
		      K_MSEC(CONFIG_INPUT_MICROCHIP_MTCH_PERIOD_MS),
		      K_MSEC(CONFIG_INPUT_MICROCHIP_MTCH_PERIOD_MS));
	LOG_INF("Polling mode (%dms)", CONFIG_INPUT_MICROCHIP_MTCH_PERIOD_MS);
#endif

	return 0;
}

/* Public API to query sensor information */

int mtch_get_sensor_info(const struct device *dev, struct mtch_sensor_info *info)
{
	if (dev == NULL || info == NULL) {
		return -EINVAL;
	}

	struct mtch_data *data = dev->data;
	info->device = data->sensor_info.device;
	info->sensor = data->sensor_info.sensor;

	return 0;
}

int mtch_get_sensor_caps(const struct device *dev, struct mtch_sensor_caps *caps)
{
	if (dev == NULL || caps == NULL) {
		return -EINVAL;
	}

	struct mtch_data *data = dev->data;

	caps->has_wheel = true;

	/* Determine capabilities based on device and sensor type */
	switch (data->sensor_info.device) {
	case MTCH_DEVICE_3380P:
		caps->num_buttons = 11;
		caps->button_mask = 0x7FF; /* All 11 buttons (0-10) */
		if (data->sensor_info.sensor == MTCH_SENSOR_MUTUAL_CAP) {
			caps->num_sliders = 1;
		} else {
			caps->num_sliders = 2;
		}
		break;
	case MTCH_DEVICE_3240P:
		caps->num_sliders = 1;
		if (data->sensor_info.sensor == MTCH_SENSOR_SELF_CAP) {
			/* Self cap: buttons 0-4 and 7-10 (no 5, 6) */
			caps->num_buttons = 9;
			caps->button_mask = 0x79F; /* bits 0-4, 7-10 */
		} else {
			/* Mutual cap: only buttons 7-10 available */
			caps->num_buttons = 4;
			caps->button_mask = 0x780; /* bits 7-10 */
		}
		break;
	default:
		caps->num_buttons = 11;
		caps->button_mask = 0x7FF; /* All 11 buttons */
		caps->num_sliders = 2;
		break;
	}

	return 0;
}

/* Device instantiation */

#define MTCH_INIT(inst) \
	static const struct mtch_config mtch_config_##inst = { \
		.common = INPUT_TOUCH_DT_INST_COMMON_CONFIG_INIT(inst), \
		.i2c = I2C_DT_SPEC_INST_GET(inst), \
		.rst_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}), \
		.irq_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, irq_gpios, {0}), \
		.startup_delay_ms = DT_INST_PROP_OR(inst, startup_delay_ms, 100), \
	}; \
	static struct mtch_data mtch_data_##inst; \
	PM_DEVICE_DT_INST_DEFINE(inst, mtch_pm_action); \
	DEVICE_DT_INST_DEFINE(inst, mtch_init, \
			      PM_DEVICE_DT_INST_GET(inst), \
			      &mtch_data_##inst, &mtch_config_##inst, \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MTCH_INIT)
