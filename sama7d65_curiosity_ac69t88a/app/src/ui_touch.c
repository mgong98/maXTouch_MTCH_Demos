/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dynamic touch visualization - MUST STAY IN C
 */

#include "ui_touch.h"

static const lv_color_t slot_colors[] = {
	LV_COLOR_MAKE(231, 76, 60),   /* Red */
	LV_COLOR_MAKE(46, 204, 113),  /* Green */
	LV_COLOR_MAKE(52, 152, 219),  /* Blue */
	LV_COLOR_MAKE(241, 196, 15),  /* Yellow */
	LV_COLOR_MAKE(155, 89, 182),  /* Purple */
	LV_COLOR_MAKE(230, 126, 34),  /* Orange */
	LV_COLOR_MAKE(26, 188, 156),  /* Teal */
	LV_COLOR_MAKE(236, 240, 241), /* White */
	LV_COLOR_MAKE(52, 73, 94),    /* Dark */
	LV_COLOR_MAKE(149, 165, 166), /* Gray */
};

struct touch_indicator {
	lv_obj_t *circle;
	lv_obj_t *id_label;
	lv_obj_t *coord_label;
};

static struct ui_touch_state touch_state;
static struct touch_indicator indicators[UI_TOUCH_MAX_POINTS];
static lv_obj_t *touch_tab_obj;
static lv_obj_t *count_label;
static lv_obj_t *max_label;
static lv_obj_t *hint_label;
static lv_obj_t *hold_btn;
static bool hold_mode;
static uint8_t max_touches;

struct ui_touch_state *ui_touch_get_state(void)
{
	return &touch_state;
}

static void hold_btn_cb(lv_event_t *e)
{
	hold_mode = !hold_mode;

	lv_obj_t *label = lv_obj_get_child(hold_btn, 0);

	if (hold_mode) {
		lv_label_set_text(label, "Hold: ON");
	} else {
		lv_label_set_text(label, "Hold: OFF");
		max_touches = 0;
	}
}

void ui_touch_init(lv_obj_t *parent)
{
	touch_tab_obj = parent;

	/* Title */
	lv_obj_t *title = lv_label_create(parent);
	lv_label_set_text(title, "Multitouch Test");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

	/* Touch count labels */
	count_label = lv_label_create(parent);
	lv_label_set_text(count_label, "Active: 0");
	lv_obj_set_style_text_font(count_label, &lv_font_montserrat_20, 0);
	lv_obj_align(count_label, LV_ALIGN_TOP_MID, -60, 45);

	max_label = lv_label_create(parent);
	lv_label_set_text(max_label, "Max: 0");
	lv_obj_set_style_text_font(max_label, &lv_font_montserrat_20, 0);
	lv_obj_align(max_label, LV_ALIGN_TOP_MID, 60, 45);

	/* Hint */
	hint_label = lv_label_create(parent);
	lv_label_set_text(hint_label, "Touch the screen with 1..5 fingers");
	lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_opa(hint_label, LV_OPA_50, 0);
	lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 72);

	/* Hold toggle button */
	hold_btn = lv_btn_create(parent);
	lv_obj_set_size(hold_btn, 120, 40);
	lv_obj_align(hold_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
	lv_obj_add_event_cb(hold_btn, hold_btn_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *btn_label = lv_label_create(hold_btn);
	lv_label_set_text(btn_label, "Hold: OFF");
	lv_obj_center(btn_label);

	/* Pre-allocate indicator objects for each slot */
	for (int i = 0; i < UI_TOUCH_MAX_POINTS; i++) {
		struct touch_indicator *ind = &indicators[i];

		ind->circle = lv_obj_create(parent);
		lv_obj_set_size(ind->circle, UI_TOUCH_INDICATOR_SIZE, UI_TOUCH_INDICATOR_SIZE);
		lv_obj_set_style_radius(ind->circle, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(ind->circle, slot_colors[i], 0);
		lv_obj_set_style_bg_opa(ind->circle, LV_OPA_80, 0);
		lv_obj_set_style_border_color(ind->circle, lv_color_white(), 0);
		lv_obj_set_style_border_width(ind->circle, 2, 0);
		lv_obj_clear_flag(ind->circle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(ind->circle, LV_OBJ_FLAG_HIDDEN);

		ind->id_label = lv_label_create(ind->circle);
		lv_label_set_text_fmt(ind->id_label, "%d", i);
		lv_obj_set_style_text_font(ind->id_label, &lv_font_montserrat_20, 0);
		lv_obj_align(ind->id_label, LV_ALIGN_CENTER, 0, -8);

		ind->coord_label = lv_label_create(ind->circle);
		lv_label_set_text(ind->coord_label, "");
		lv_obj_set_style_text_font(ind->coord_label, &lv_font_montserrat_14, 0);
		lv_obj_align(ind->coord_label, LV_ALIGN_CENTER, 0, 12);
	}
}

void ui_touch_update(void)
{
	if (!touch_tab_obj) {
		return;
	}

	lv_area_t tab_area;
	lv_obj_get_coords(touch_tab_obj, &tab_area);
	int32_t off_x = tab_area.x1;
	int32_t off_y = tab_area.y1;

	uint8_t active_count = 0;

	for (int i = 0; i < UI_TOUCH_MAX_POINTS; i++) {
		struct touch_indicator *ind = &indicators[i];

		if (touch_state.points[i].active) {
			int32_t x = touch_state.points[i].x;
			int32_t y = touch_state.points[i].y;

			lv_obj_set_pos(ind->circle,
				       x - off_x - UI_TOUCH_INDICATOR_SIZE / 2,
				       y - off_y - UI_TOUCH_INDICATOR_SIZE / 2);
			lv_label_set_text_fmt(ind->coord_label, "%d,%d", x, y);
			lv_obj_clear_flag(ind->circle, LV_OBJ_FLAG_HIDDEN);
			active_count++;
		} else if (!hold_mode) {
			lv_obj_add_flag(ind->circle, LV_OBJ_FLAG_HIDDEN);
		}
	}

	if (active_count > max_touches) {
		max_touches = active_count;
	}

	lv_label_set_text_fmt(count_label, "Active: %u", active_count);
	lv_label_set_text_fmt(max_label, "Max: %u", max_touches);

	if (active_count > 0) {
		lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
	} else if (!hold_mode) {
		lv_obj_clear_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
	}
}
