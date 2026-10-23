/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dynamic touch visualization - MUST STAY IN C
 *
 * This module handles the multitouch test tab with dynamically
 * created and positioned touch indicators. This cannot be replaced
 * by an editor because:
 *   - Touch indicators are created/hidden at runtime
 *   - Positions are updated based on live touch coordinates
 *   - Number of active indicators varies dynamically
 */

#ifndef UI_TOUCH_H
#define UI_TOUCH_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_TOUCH_MAX_POINTS    10
#define UI_TOUCH_INDICATOR_SIZE 70

/*
 * Touch point state updated by input callbacks.
 */
struct ui_touch_point {
	int32_t x;
	int32_t y;
	bool active;
};

/*
 * Touch state structure for input handlers to update.
 */
struct ui_touch_state {
	struct ui_touch_point points[UI_TOUCH_MAX_POINTS];
	int32_t current_slot;
	int32_t pending_x;
	int32_t pending_y;
	bool has_x;
	bool has_y;
};

/*
 * Get the touch state for input handlers to update.
 */
struct ui_touch_state *ui_touch_get_state(void);

/*
 * Initialize touch tab with indicators.
 * Call after ui_init() with the touch tab container.
 */
void ui_touch_init(lv_obj_t *parent);

/*
 * Update touch indicator positions and visibility.
 * Call periodically from main loop.
 */
void ui_touch_update(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_TOUCH_H */
