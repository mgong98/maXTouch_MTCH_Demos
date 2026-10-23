/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware input handlers - MUST STAY IN C
 *
 * This module handles raw input events from touch and MTCH devices.
 * These callbacks are hardware-specific and cannot be replaced by
 * a visual editor.
 */

#ifndef INPUT_HANDLERS_H
#define INPUT_HANDLERS_H

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize input handlers.
 * Registers callbacks for touch and MTCH devices.
 * Must be called after devices are ready.
 */
void input_handlers_init(void);

/*
 * Update MTCH demo widgets with current input values.
 * Call periodically from main loop.
 */
void input_handlers_update_mtch_ui(void);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_HANDLERS_H */
