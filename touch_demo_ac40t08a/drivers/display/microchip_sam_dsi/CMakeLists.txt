# Copyright (c) 2026 Microchip Technology Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_DISPLAY_MICROCHIP_SAM_DSI microchip_sam_dsi.c)
