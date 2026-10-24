/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Static UI definitions - EDITOR REPLACEABLE
 *
 * This file and ui.c contain the static UI layouts that can be
 * replaced by LVGL Pro Editor generated code. When migrating to
 * an editor workflow:
 *   1. Design screens in LVGL Pro Editor
 *   2. Export C code to this directory
 *   3. Replace ui.c and ui.h with generated versions
 *   4. Ensure exported widget names match those referenced in ui_touch.c
 */

#ifndef UI_H
#define UI_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UI configuration based on sensor capabilities */
struct ui_mtch_config {
	uint8_t num_buttons;   /* Number of buttons to display */
	uint16_t button_mask;  /* Bitmask of available buttons (bit N = button N) */
	uint8_t num_sliders;   /* Number of sliders to display (1 or 2) */
	bool has_wheel;        /* Has wheel control */
};

/*
 * Widget references exposed for runtime updates.
 * When using LVGL Pro Editor, ensure these names match
 * the widget names defined in the editor.
 */

/* Demo tab widgets */
extern lv_obj_t *ui_counter_label;
extern lv_obj_t *ui_arc;
extern lv_obj_t *ui_arc_label;

/* MTCH tab widgets */
extern lv_obj_t *ui_mtch_wheel;
extern lv_obj_t *ui_mtch_slider1;
extern lv_obj_t *ui_mtch_slider2;
extern lv_obj_t *ui_mtch_buttons[11];

/* Main tabview - needed to add dynamic touch tab */
extern lv_obj_t *ui_tabview;
extern lv_obj_t *ui_tab_touch;

/*
 * Initialize the static UI.
 * Creates tabview, demo tab, and MTCH tab.
 * Touch tab container is created but populated by ui_touch module.
 */
void ui_init(void);

/*
 * Configure MTCH tab based on sensor capabilities.
 * Call after ui_init() and after sensor detection.
 * Hides unused sliders and buttons based on config.
 */
void ui_mtch_configure(const struct ui_mtch_config *config);

/*
 * Get the touch tab container for dynamic content.
 * Called by ui_touch to add touch indicators.
 */
lv_obj_t *ui_get_touch_tab(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
