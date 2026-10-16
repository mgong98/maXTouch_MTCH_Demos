/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * LCDC POC application with three tabs:
 *   1. LVGL widget demo (button counter, arc, slider)
 *   2. Multitouch visualizer using raw input events
 *   3. MTCH demo (wheel, sliders, buttons)
 *
 * This main.c is kept minimal - UI creation is delegated to:
 *   - ui/ui.c: Static layouts (editor-replaceable)
 *   - ui_touch.c: Dynamic touch indicators (must stay in C)
 *   - input_handlers.c: Hardware input callbacks (must stay in C)
 */

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>

#include "ui/ui.h"
#include "ui_touch.h"
#include "input_handlers.h"
#include "../../drivers/input/microchip_mtch.h"

LOG_MODULE_REGISTER(main_app, LOG_LEVEL_INF);

static const struct gpio_dt_spec lvds_pwm =
	GPIO_DT_SPEC_GET(DT_NODELABEL(gpio_lvds_pwm), gpios);

#define TOUCH_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_touch))
#define MTCH_DEV  DEVICE_DT_GET(DT_NODELABEL(mtch))

int main(void)
{
	int ret;

	LOG_INF("Setting up GPIOs...");

	if (!gpio_is_ready_dt(&lvds_pwm)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&lvds_pwm, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}

	LOG_INF("LCDC POC Demo");

	const struct device *display_dev =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}

	if (!device_is_ready(TOUCH_DEV)) {
		LOG_ERR("Touch device not ready");
		return -ENODEV;
	}

	/* Initialize static UI (editor-replaceable) */
	ui_init();
	LOG_INF("Static UI initialized");

	/* Configure MTCH UI based on detected sensor */
	if (device_is_ready(MTCH_DEV)) {
		struct mtch_sensor_caps caps;
		if (mtch_get_sensor_caps(MTCH_DEV, &caps) == 0) {
			struct ui_mtch_config ui_cfg = {
				.num_buttons = caps.num_buttons,
				.button_mask = caps.button_mask,
				.num_sliders = caps.num_sliders,
				.has_wheel = caps.has_wheel,
			};
			ui_mtch_configure(&ui_cfg);
			LOG_INF("MTCH UI configured: %d buttons (mask=0x%03X), %d sliders",
				caps.num_buttons, caps.button_mask, caps.num_sliders);
		}
	}

	/* Initialize dynamic touch tab (must stay in C) */
	ui_touch_init(ui_get_touch_tab());
	LOG_INF("Touch UI initialized");

	/* Initialize input handlers */
	input_handlers_init();

	display_blanking_off(display_dev);

	LOG_INF("Entering main loop");

	while (1) {
		/* Update dynamic UI elements */
		ui_touch_update();
		input_handlers_update_mtch_ui();

		/* Process LVGL tasks */
		lv_timer_handler();
		k_sleep(K_MSEC(LV_DEF_REFR_PERIOD));
	}

	return 0;
}
