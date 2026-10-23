/*
 * Copyright (c) 2025 Microchip Technology Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DRIVERS_INPUT_MICROCHIP_MAXTOUCH_H_
#define DRIVERS_INPUT_MICROCHIP_MAXTOUCH_H_

#include <stdbool.h>
#include <stdint.h>

/* Information Block (7 bytes at address 0x0000) */
struct mtch_info {
	uint8_t family_id;
	uint8_t variant_id;
	uint8_t version;
	uint8_t build;
	uint8_t matrix_x_size;
	uint8_t matrix_y_size;
	uint8_t num_objects;
} __packed;

/* Object Table Element (6 bytes per entry) */
struct mtch_object {
	uint8_t type;
	uint8_t start_pos_lsb;
	uint8_t start_pos_msb;
	uint8_t size_minus_one;
	uint8_t instances_minus_one;
	uint8_t num_report_ids;
} __packed;

/* T6 Command Processor status flags */
#define T6_STATUS_RESET   0x80
#define T6_STATUS_OFL     0x40
#define T6_STATUS_SIGERR  0x20
#define T6_STATUS_CAL     0x10
#define T6_STATUS_CFGERR  0x08
#define T6_STATUS_COMSERR 0x04

/* T6 Command Processor field offsets */
#define T6_CMD_RESET      0
#define T6_CMD_BACKUPNV   1
#define T6_CMD_CALIBRATE  2
#define T6_CMD_REPORTALL  3
#define T6_CMD_DIAGNOSTIC 5

/* Object types */
#define GEN_MESSAGE_T5                5
#define GEN_COMMAND_T6                6
#define GEN_POWER_T7                  7
#define GEN_ACQUIRE_T8                8
#define SPT_COMMSCONFIG_T18           18
#define SPT_USERDATA_T38              38
#define SPT_MESSAGECOUNT_T44          44
#define GEN_EXTENDEDOBJECTTABLE_T254  254
#define TOUCH_MTCHKEYARRAY_T500       500
#define SPT_MTCHKEYCTRL_T501          501
#define SPT_MTCHSLIDERCTRL_T502       502

/* Device types detected from T38 */
enum mtch_device_type {
	MTCH_DEVICE_UNKNOWN = 0,
	MTCH_DEVICE_3380P,
	MTCH_DEVICE_3240P,
};

/* Sensor types detected from T38 */
enum mtch_sensor_type {
	MTCH_SENSOR_UNKNOWN = 0,
	MTCH_SENSOR_SELF_CAP,
	MTCH_SENSOR_MUTUAL_CAP,
};

/* Sensor identification info from T38 */
struct mtch_sensor_info {
	enum mtch_device_type device;
	enum mtch_sensor_type sensor;
};

/* Sensor capabilities based on device and sensor type */
struct mtch_sensor_caps {
	uint8_t num_buttons;   /* Number of buttons */
	uint16_t button_mask;  /* Bitmask of available buttons (bit N = button N) */
	uint8_t num_sliders;   /* Number of sliders (1 or 2) */
	bool has_wheel;        /* Has wheel control */
};

/* Public API to query sensor information */
struct device;
int mtch_get_sensor_info(const struct device *dev, struct mtch_sensor_info *info);
int mtch_get_sensor_caps(const struct device *dev, struct mtch_sensor_caps *caps);

/* T18 SPT_COMMSCONFIG: MODE bit selects CHG line behavior */
#define T18_CTRL_MODE 0x40

/* T7 Power Configuration */
struct mtch_t7_config {
	uint8_t idle;
	uint8_t active;
	uint8_t actv2idle;
} __packed;

/* Protocol limits */
#define MTCH_INFO_SIZE          sizeof(struct mtch_info)
#define MTCH_OBJECT_SIZE        sizeof(struct mtch_object)
#define MTCH_INFO_CHECKSUM_SIZE 3
#define MTCH_MAX_OBJECTS        30
#define MTCH_MAX_INFO_SIZE      (MTCH_INFO_SIZE + \
				(MTCH_MAX_OBJECTS * MTCH_OBJECT_SIZE) + \
				MTCH_INFO_CHECKSUM_SIZE)
#define MTCH_MAX_MSG_SIZE       16
#define MTCH_MAX_MSG_COUNT      20

/* CRC-24 polynomial used by maXTouch info block validation */
#define MXT_CRC_POLY 0x80001BU

/* I2C addresses (selected by hardware ADDR pin) */
#define I2C_ADDR_PRIMARY   0x3A
#define I2C_ADDR_SECONDARY 0x3B

/* I2C probe timeout in milliseconds */
#define MTCH_PROBE_TIMEOUT_MS 2000

#endif /* DRIVERS_INPUT_MICROCHIP_MAXTOUCH_H_ */
