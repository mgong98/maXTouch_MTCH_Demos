/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Microchip SAM DSI Host Controller driver header.
 */

#ifndef MICROCHIP_SAM_DSI_H_
#define MICROCHIP_SAM_DSI_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DSI Generic Header for DCS/Generic packets.
 * For short packets, Data0/Data1 are in the header.
 * For long packets, size specifies word count in bytes.
 */
typedef union {
	uint32_t header_u32;
	struct {
		uint32_t data_type : 6;
		uint32_t virtual_channel : 2;
		uint8_t data[2];
	} short_pkt;
	struct {
		uint32_t data_type : 6;
		uint32_t virtual_channel : 2;
		uint32_t size : 16;
	} long_pkt;
} dsi_generic_header_t;

/*
 * DSI Generic Payload for long packets.
 * Payload is written 4 bytes at a time.
 */
typedef union {
	uint32_t data_u32;
	uint16_t data_u16[2];
	uint8_t data[4];
} dsi_generic_payload_t;

/* DSI data types */
#define DSI_DT_GEN_SHORT_WR_0P     0x03
#define DSI_DT_GEN_SHORT_WR_1P     0x13
#define DSI_DT_GEN_SHORT_WR_2P     0x23
#define DSI_DT_DCS_SHORT_WR_0P     0x05
#define DSI_DT_DCS_SHORT_WR_1P     0x15
#define DSI_DT_GEN_LONG_WR         0x29
#define DSI_DT_DCS_LONG_WR         0x39

/* DCS commands */
#define DCS_EXIT_SLEEP_MODE        0x11
#define DCS_SET_DISPLAY_ON         0x29
#define DCS_SET_DISPLAY_OFF        0x28
#define DCS_ENTER_SLEEP_MODE       0x10
#define DCS_SET_ADDRESS_MODE       0x36  /* MADCTL - Memory Access Control */

/* MADCTL bits for rotation */
#define MADCTL_MY                  0x80  /* Row Address Order */
#define MADCTL_MX                  0x40  /* Column Address Order */
#define MADCTL_MV                  0x20  /* Row/Column Exchange */
#define MADCTL_ML                  0x10  /* Vertical Refresh Order */
#define MADCTL_BGR                 0x08  /* RGB/BGR Order */
#define MADCTL_MH                  0x04  /* Horizontal Refresh Order */

#ifdef __cplusplus
}
#endif

#endif /* MICROCHIP_SAM_DSI_H_ */
