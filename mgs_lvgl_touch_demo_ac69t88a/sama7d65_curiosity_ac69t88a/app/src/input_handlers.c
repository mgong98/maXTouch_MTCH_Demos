/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware input handlers - MUST STAY IN C
 */

#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include "input_handlers.h"
#include "ui_touch.h"
#include "ui/ui.h"

LOG_MODULE_REGISTER(input_handlers, LOG_LEVEL_INF);

#define TOUCH_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_touch))
#define MTCH_DEV  DEVICE_DT_GET(DT_NODELABEL(mtch))

/* MTCH state */
static int32_t mtch_wheel_angle;
static int32_t mtch_slider1_val;
static int32_t mtch_slider2_val;
static bool mtch_wheel_absolute_mode = true;
static int32_t mtch_wheel_last_position;

/* Map incoming button index to UI button index
 * This mapping is used for all sensors - the driver handles
 * sensor-specific report ID to button index translation.
 * For 3240P self cap (9 buttons), indices 5-6 are unused.
 */
static const int btn_mapping[11] = {
	10,  /* incoming 0 -> UI 10 */
	9,   /* incoming 1 -> UI 9 */
	8,   /* incoming 2 -> UI 8 */
	7,   /* incoming 3 -> UI 7 */
	6,   /* incoming 4 -> UI 6 */
	5,   /* incoming 5 -> UI 5 */
	3,   /* incoming 6 -> UI 3 */
	4,   /* incoming 7 -> UI 4 */
	1,   /* incoming 8 -> UI 1 */
	2,   /* incoming 9 -> UI 2 */
	0,   /* incoming 10 -> UI 0 */
};

/* Multitouch input event callback */

static void touch_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	struct ui_touch_state *state = ui_touch_get_state();

	switch (evt->type) {
	case INPUT_EV_ABS:
		switch (evt->code) {
		case INPUT_ABS_MT_SLOT:
			if (evt->value >= 0 && evt->value < UI_TOUCH_MAX_POINTS) {
				state->current_slot = evt->value;
			}
			state->has_x = false;
			state->has_y = false;
			break;
		case INPUT_ABS_X:
			state->pending_x = evt->value;
			state->has_x = true;
			break;
		case INPUT_ABS_Y:
			state->pending_y = evt->value;
			state->has_y = true;
			break;
		default:
			break;
		}
		break;

	case INPUT_EV_KEY:
		if (evt->code == INPUT_BTN_TOUCH) {
			int32_t slot = state->current_slot;

			if (slot < 0 || slot >= UI_TOUCH_MAX_POINTS) {
				break;
			}

			if (evt->value) {
				if (state->has_x) {
					state->points[slot].x = state->pending_x;
				}
				if (state->has_y) {
					state->points[slot].y = state->pending_y;
				}
				state->points[slot].active = true;
			} else {
				state->points[slot].active = false;
			}
		}
		break;

	default:
		break;
	}
}

INPUT_CALLBACK_DEFINE(TOUCH_DEV, touch_input_cb, NULL);

/* MTCH input event callback */

static void mtch_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (evt->type) {
	case INPUT_EV_ABS:
		switch (evt->code) {
		case 0: /* Wheel - sensor range is 0-255 - Range set to 8 */
			if (mtch_wheel_absolute_mode) {
				mtch_wheel_angle = (evt->value * 360) / 255;
			} else {
				int32_t delta = evt->value - mtch_wheel_last_position;
				mtch_wheel_angle += (delta * 360) / 255;
				if (mtch_wheel_angle < 0) {
					mtch_wheel_angle += 360;
				}
				if (mtch_wheel_angle >= 360) {
					mtch_wheel_angle -= 360;
				}
			}
			mtch_wheel_last_position = evt->value;
			break;
		case 1: /* Slider 1 - sensor range is 0-63 -- Range set to 5 */
			mtch_slider1_val = (evt->value * 255) / 63;
			break;
		case 2: /* Slider 2 - sensor range is 0-63 */
			mtch_slider2_val = (evt->value * 255) / 63;
			break;
		default:
			break;
		}
		break;

	case INPUT_EV_KEY:
		if (evt->code >= 0x100 && evt->code < 0x100 + 11) {
			int incoming_idx = evt->code - 0x100;
			int btn_idx = btn_mapping[incoming_idx];
			if (evt->value) {
				lv_obj_set_style_bg_color(ui_mtch_buttons[btn_idx],
							  lv_palette_main(LV_PALETTE_GREEN), 0);
			} else {
				lv_obj_set_style_bg_color(ui_mtch_buttons[btn_idx],
							  lv_color_make(100, 100, 100), 0);
			}
		}
		break;

	default:
		break;
	}
}

INPUT_CALLBACK_DEFINE(MTCH_DEV, mtch_input_cb, NULL);

void input_handlers_init(void)
{
	LOG_INF("Input handlers initialized");
}

void input_handlers_update_mtch_ui(void)
{
	if (!ui_mtch_wheel) {
		return;
	}

	lv_arc_set_value(ui_mtch_wheel, mtch_wheel_angle);
	lv_slider_set_value(ui_mtch_slider1, mtch_slider1_val, LV_ANIM_OFF);
	lv_slider_set_value(ui_mtch_slider2, mtch_slider2_val, LV_ANIM_OFF);
}
