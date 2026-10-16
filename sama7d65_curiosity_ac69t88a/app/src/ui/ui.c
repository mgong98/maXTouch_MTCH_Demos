/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Static UI implementation - EDITOR REPLACEABLE
 *
 * This file contains static UI layouts that can be replaced by
 * LVGL Pro Editor generated code. The widget creation code below
 * is equivalent to what an editor would generate.
 */

#include "ui.h"

#define TAB_BAR_HEIGHT 50

/* Demo tab widgets */
lv_obj_t *ui_counter_label;
lv_obj_t *ui_arc;
lv_obj_t *ui_arc_label;

/* MTCH tab widgets */
lv_obj_t *ui_mtch_wheel;
lv_obj_t *ui_mtch_slider1;
lv_obj_t *ui_mtch_slider2;
lv_obj_t *ui_mtch_buttons[11];

/* MTCH tab labels (for hiding) */
static lv_obj_t *ui_mtch_slider2_label;

/* Tabview and tabs */
lv_obj_t *ui_tabview;
lv_obj_t *ui_tab_touch;

/* Internal counter for demo */
static uint32_t counter;

/* Event callbacks for demo tab */

static void btn_event_cb(lv_event_t *e)
{
	counter++;
	lv_label_set_text_fmt(ui_counter_label, "Count: %u", counter);
}

static void arc_event_cb(lv_event_t *e)
{
	int32_t val = lv_arc_get_value(ui_arc);

	lv_label_set_text_fmt(ui_arc_label, "%d%%", val);
	lv_obj_align_to(ui_arc_label, ui_arc, LV_ALIGN_CENTER, 0, 0);
}

/* Demo tab creation */

static void create_demo_tab(lv_obj_t *parent)
{
	/* Title label */
	lv_obj_t *title = lv_label_create(parent);
	lv_label_set_text(title, "LVGL on Zephyr");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

	/* Info label */
	lv_obj_t *info = lv_label_create(parent);
	lv_label_set_text(info, "SAMA7D65 Curiosity Kit with AC69T88A LVDS Display");
	lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
	lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 48);

	/* Counter button */
	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_set_size(btn, 200, 60);
	lv_obj_align(btn, LV_ALIGN_CENTER, 0, -20);
	lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *btn_label = lv_label_create(btn);
	lv_label_set_text(btn_label, "Tap Me");
	lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_20, 0);
	lv_obj_center(btn_label);

	/* Counter display */
	ui_counter_label = lv_label_create(parent);
	lv_label_set_text(ui_counter_label, "Count: 0");
	lv_obj_set_style_text_font(ui_counter_label, &lv_font_montserrat_20, 0);
	lv_obj_align(ui_counter_label, LV_ALIGN_CENTER, 0, 30);

	/* Arc widget */
	ui_arc = lv_arc_create(parent);
	lv_arc_set_range(ui_arc, 0, 100);
	lv_arc_set_value(ui_arc, 70);
	lv_obj_set_size(ui_arc, 180, 180);
	lv_obj_set_ext_click_area(ui_arc, 20);
	lv_obj_align(ui_arc, LV_ALIGN_BOTTOM_LEFT, 40, -20);
	lv_obj_add_event_cb(ui_arc, arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

	/* Arc percentage label */
	ui_arc_label = lv_label_create(parent);
	lv_label_set_text(ui_arc_label, "70%");
	lv_obj_set_style_text_font(ui_arc_label, &lv_font_montserrat_20, 0);
	lv_obj_align_to(ui_arc_label, ui_arc, LV_ALIGN_CENTER, 0, 0);

	/* Brightness slider */
	lv_obj_t *slider = lv_slider_create(parent);
	lv_slider_set_range(slider, 0, 255);
	lv_slider_set_value(slider, 128, LV_ANIM_OFF);
	lv_obj_set_width(slider, 280);
	lv_obj_set_ext_click_area(slider, 20);

	lv_obj_update_layout(parent);
	int32_t arc_cy = lv_obj_get_y(ui_arc) + lv_obj_get_height(ui_arc) / 2;
	int32_t slider_h = lv_obj_get_height(slider);
	int32_t parent_cw = lv_obj_get_content_width(parent);

	lv_obj_set_pos(slider, parent_cw - 40 - 280, arc_cy - slider_h / 2);

	lv_obj_t *slider_label = lv_label_create(parent);
	lv_label_set_text(slider_label, "Brightness");
	lv_obj_set_style_text_font(slider_label, &lv_font_montserrat_14, 0);
	lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
}

/* MTCH demo tab creation */

static void create_mtch_tab(lv_obj_t *parent)
{
	lv_obj_set_style_pad_all(parent, 10, 0);

	/* Title - centered */
	lv_obj_t *title = lv_label_create(parent);
	lv_label_set_text(title, "MTCH Demo");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

	/* Wheel - enlarged and centered between left edge and sliders */
	int32_t wheel_size = 180;
	int32_t wheel_x_pos = 100;

	lv_obj_t *wheel_label = lv_label_create(parent);
	lv_label_set_text(wheel_label, "Wheel");
	lv_obj_set_style_text_font(wheel_label, &lv_font_montserrat_14, 0);
	lv_obj_align(wheel_label, LV_ALIGN_TOP_LEFT, wheel_x_pos - 15, 45);

	ui_mtch_wheel = lv_arc_create(parent);
	lv_arc_set_range(ui_mtch_wheel, 0, 360);
	lv_arc_set_value(ui_mtch_wheel, 0);
	lv_arc_set_bg_angles(ui_mtch_wheel, 0, 360);
	lv_obj_set_size(ui_mtch_wheel, wheel_size, wheel_size);
	lv_obj_align(ui_mtch_wheel, LV_ALIGN_LEFT_MID, wheel_x_pos - wheel_size / 2, 10);

	/* Sliders - centered */
	int32_t slider_width = 200;
	int32_t slider_height = 20;

	/* Slider 1 Label */
	lv_obj_t *slider1_label = lv_label_create(parent);
	lv_label_set_text(slider1_label, "Slider 1");
	lv_obj_set_style_text_font(slider1_label, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_align(slider1_label, LV_ALIGN_CENTER, 0, -50);

	/* Slider 1 */
	ui_mtch_slider1 = lv_slider_create(parent);
	lv_slider_set_range(ui_mtch_slider1, 0, 255);
	lv_slider_set_value(ui_mtch_slider1, 0, LV_ANIM_OFF);
	lv_obj_set_size(ui_mtch_slider1, slider_width, slider_height);
	lv_obj_set_style_pad_all(ui_mtch_slider1, 4, 0);
	lv_obj_align(ui_mtch_slider1, LV_ALIGN_CENTER, 0, -30);

	/* Slider 2 Label */
	ui_mtch_slider2_label = lv_label_create(parent);
	lv_label_set_text(ui_mtch_slider2_label, "Slider 2");
	lv_obj_set_style_text_font(ui_mtch_slider2_label, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_align(ui_mtch_slider2_label, LV_ALIGN_CENTER, 0, 20);

	/* Slider 2 */
	ui_mtch_slider2 = lv_slider_create(parent);
	lv_slider_set_range(ui_mtch_slider2, 0, 255);
	lv_slider_set_value(ui_mtch_slider2, 0, LV_ANIM_OFF);
	lv_obj_set_size(ui_mtch_slider2, slider_width, slider_height);
	lv_obj_set_style_pad_all(ui_mtch_slider2, 4, 0);
	lv_obj_align(ui_mtch_slider2, LV_ALIGN_CENTER, 0, 40);


	/* Button section label */
	lv_obj_t *button_label = lv_label_create(parent);
	lv_label_set_text(button_label, "Buttons");
	lv_obj_set_style_text_font(button_label, &lv_font_montserrat_14, 0);
	lv_obj_align(button_label, LV_ALIGN_TOP_RIGHT, -90, 45);

	int32_t btn_size = 40;
	int32_t btn_gap = 5;
	int32_t btn_start_y = 65;
	int32_t row_spacing = 45;
	int btn_idx = 0;

	/* Row of 2 width: used for large button and rows of 2 */
	int32_t row2_total_width = 2 * btn_size + btn_gap;
	/* Center point for upper rows (large button + 3 rows of 2) */
	int32_t center_x = -20 - row2_total_width / 2;

	/* Button 0: Large button at top (left edge aligned with button 2's left edge) */
	ui_mtch_buttons[btn_idx] = lv_btn_create(parent);
	lv_obj_set_size(ui_mtch_buttons[btn_idx], row2_total_width, btn_size);
	lv_obj_align(ui_mtch_buttons[btn_idx], LV_ALIGN_TOP_RIGHT,
		     center_x - row2_total_width / 2 + btn_size + btn_gap, btn_start_y);
	lv_obj_set_style_bg_color(ui_mtch_buttons[btn_idx], lv_color_make(100, 100, 100), 0);

	lv_obj_t *btn_label = lv_label_create(ui_mtch_buttons[btn_idx]);
	lv_label_set_text_fmt(btn_label, "%d", btn_idx);
	lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
	lv_obj_center(btn_label);
	btn_idx++;

	/* Buttons 1-2: Row of 2 (centered under large button) */
	for (int i = 0; i < 2; i++) {
		ui_mtch_buttons[btn_idx] = lv_btn_create(parent);
		lv_obj_set_size(ui_mtch_buttons[btn_idx], btn_size, btn_size);
		int32_t x_offset = center_x - row2_total_width / 2 + (1 - i) * (btn_size + btn_gap);
		int32_t y_offset = btn_start_y + row_spacing;
		lv_obj_align(ui_mtch_buttons[btn_idx], LV_ALIGN_TOP_RIGHT, x_offset, y_offset);
		lv_obj_set_style_bg_color(ui_mtch_buttons[btn_idx], lv_color_make(100, 100, 100), 0);

		btn_label = lv_label_create(ui_mtch_buttons[btn_idx]);
		lv_label_set_text_fmt(btn_label, "%d", btn_idx);
		lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
		lv_obj_center(btn_label);
		btn_idx++;
	}

	/* Buttons 3-4: Row of 2 (centered) */
	for (int i = 0; i < 2; i++) {
		ui_mtch_buttons[btn_idx] = lv_btn_create(parent);
		lv_obj_set_size(ui_mtch_buttons[btn_idx], btn_size, btn_size);
		int32_t x_offset = center_x - row2_total_width / 2 + (1 - i) * (btn_size + btn_gap);
		int32_t y_offset = btn_start_y + 2 * row_spacing;
		lv_obj_align(ui_mtch_buttons[btn_idx], LV_ALIGN_TOP_RIGHT, x_offset, y_offset);
		lv_obj_set_style_bg_color(ui_mtch_buttons[btn_idx], lv_color_make(100, 100, 100), 0);

		btn_label = lv_label_create(ui_mtch_buttons[btn_idx]);
		lv_label_set_text_fmt(btn_label, "%d", btn_idx);
		lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
		lv_obj_center(btn_label);
		btn_idx++;
	}

	/* Buttons 5, 6: Row of 2 (centered) */
	static const int row3_btns[2] = {5, 6};
	for (int i = 0; i < 2; i++) {
		int btn_num = row3_btns[i];
		ui_mtch_buttons[btn_num] = lv_btn_create(parent);
		lv_obj_set_size(ui_mtch_buttons[btn_num], btn_size, btn_size);
		int32_t x_offset = center_x - row2_total_width / 2 + (1 - i) * (btn_size + btn_gap);
		int32_t y_offset = btn_start_y + 3 * row_spacing;
		lv_obj_align(ui_mtch_buttons[btn_num], LV_ALIGN_TOP_RIGHT, x_offset, y_offset);
		lv_obj_set_style_bg_color(ui_mtch_buttons[btn_num], lv_color_make(100, 100, 100), 0);

		btn_label = lv_label_create(ui_mtch_buttons[btn_num]);
		lv_label_set_text_fmt(btn_label, "%d", btn_num);
		lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
		lv_obj_center(btn_label);
	}

	/* Buttons 7-10: Row of 4 at bottom (smaller buttons)
	 * Order: 10, 9, 8, 7 from left to right
	 */
	int32_t small_btn_size = 35;
	int32_t small_btn_gap = 5;
	int32_t row4_total_width = 4 * small_btn_size + 3 * small_btn_gap;
	static const int row4_order[4] = {10, 9, 8, 7};
	for (int i = 0; i < 4; i++) {
		int btn_num = row4_order[i];
		ui_mtch_buttons[btn_num] = lv_btn_create(parent);
		lv_obj_set_size(ui_mtch_buttons[btn_num], small_btn_size, small_btn_size);
		int32_t x_offset = center_x - row4_total_width / 2 + (3 - i) * (small_btn_size + small_btn_gap);
		int32_t y_offset = btn_start_y + 4 * row_spacing;
		lv_obj_align(ui_mtch_buttons[btn_num], LV_ALIGN_TOP_RIGHT, x_offset, y_offset);
		lv_obj_set_style_bg_color(ui_mtch_buttons[btn_num], lv_color_make(100, 100, 100), 0);

		btn_label = lv_label_create(ui_mtch_buttons[btn_num]);
		lv_label_set_text_fmt(btn_label, "%d", btn_num);
		lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
		lv_obj_center(btn_label);
	}
}

/* Touch tab - container only, populated by ui_touch module */

static void create_touch_tab(lv_obj_t *parent)
{
	lv_obj_set_style_pad_all(parent, 0, 0);
	lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
}

/* Public API */

void ui_init(void)
{
	lv_display_t *disp = lv_display_get_default();

	lv_theme_default_init(disp,
			      lv_palette_main(LV_PALETTE_BLUE),
			      lv_palette_main(LV_PALETTE_CYAN),
			      true, &lv_font_montserrat_14);

	lv_obj_t *scr = lv_screen_active();

	/* Create tabview */
	ui_tabview = lv_tabview_create(scr);
	lv_tabview_set_tab_bar_size(ui_tabview, TAB_BAR_HEIGHT);
	lv_obj_set_size(ui_tabview, LV_PCT(100), LV_PCT(100));

	/* Disable scrolling on content */
	lv_obj_t *content = lv_tabview_get_content(ui_tabview);
	lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

	/* Create tabs */
	lv_obj_t *tab_demo = lv_tabview_add_tab(ui_tabview, "Demo");
	ui_tab_touch = lv_tabview_add_tab(ui_tabview, "Touch Test");
	lv_obj_t *tab_mtch = lv_tabview_add_tab(ui_tabview, "MTCH Demo");

	lv_obj_clear_flag(tab_demo, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(ui_tab_touch, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(tab_mtch, LV_OBJ_FLAG_SCROLLABLE);

	/* Populate tabs */
	create_demo_tab(tab_demo);
	create_touch_tab(ui_tab_touch);
	create_mtch_tab(tab_mtch);
}

lv_obj_t *ui_get_touch_tab(void)
{
	return ui_tab_touch;
}

void ui_mtch_configure(const struct ui_mtch_config *config)
{
	if (config == NULL) {
		return;
	}

	/* Hide slider 2 if not available */
	if (config->num_sliders < 2) {
		if (ui_mtch_slider2 != NULL) {
			lv_obj_add_flag(ui_mtch_slider2, LV_OBJ_FLAG_HIDDEN);
		}
		if (ui_mtch_slider2_label != NULL) {
			lv_obj_add_flag(ui_mtch_slider2_label, LV_OBJ_FLAG_HIDDEN);
		}
	}

	/* Hide buttons not in the button mask */
	for (int i = 0; i < 11; i++) {
		if (ui_mtch_buttons[i] != NULL) {
			if (!(config->button_mask & (1 << i))) {
				lv_obj_add_flag(ui_mtch_buttons[i], LV_OBJ_FLAG_HIDDEN);
			}
		}
	}
}
