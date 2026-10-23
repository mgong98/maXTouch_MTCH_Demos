/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Microchip SAM DSI Host Controller Bridge Driver.
 *
 * Initializes the MIPI-DSI host and D-PHY, provides command/video mode
 * switching, and sends DCS initialization sequences to attached panels.
 */

#define DT_DRV_COMPAT microchip_sam_dsi

#include <string.h>

#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "microchip_sam_dsi.h"
#include "sama7d65_dsi_regs.h"

LOG_MODULE_REGISTER(microchip_sam_dsi, CONFIG_DISPLAY_LOG_LEVEL);

/* D-PHY Test Interface Codes */
#define DPHY_CODE_HSBRC     0x44
#define DPHY_CODE_VCO       0x10
#define DPHY_CODE_ICPCTRL   0x11
#define DPHY_CODE_LPFCTRL   0x12
#define DPHY_CODE_EN_NM     0x19
#define DPHY_CODE_NRATIO    0x17
#define DPHY_CODE_MRATIO    0x18

/* Command packet status bits */
#define DSI_CPSR_CMD_FULL   DSI_CMD_PKT_STATUS_GEN_CMD_FULL_Msk
#define DSI_CPSR_CMD_EMPTY  DSI_CMD_PKT_STATUS_GEN_CMD_EMPTY_Msk
#define DSI_CPSR_PLD_EMPTY  DSI_CMD_PKT_STATUS_GEN_PLD_W_EMPTY_Msk

/* Timeouts */
#define DSI_TIMEOUT_US      100000

struct dsi_config {
	dsi_registers_t *regs;
	uint8_t num_lanes;
	uint32_t pixel_clock_khz;
	uint16_t width;
	uint16_t height;
	uint16_t hfp;
	uint16_t hbp;
	uint16_t hspw;
	uint16_t vfp;
	uint16_t vbp;
	uint16_t vspw;
	/* D-PHY PLL parameters */
	uint8_t dphy_n_ratio;
	uint8_t dphy_m_ratio;
	uint8_t dphy_hsbrc;
	uint8_t dphy_vco_range;
	uint8_t dphy_icpctrl;
	uint8_t dphy_lpfctrl;
	/* Lane timings */
	uint8_t clk_lp2hs;
	uint8_t clk_hs2lp;
	uint8_t dat_lp2hs;
	uint8_t dat_hs2lp;
	/* Computed clocks */
	uint32_t bit_clock_khz;
	uint32_t byte_clock_khz;
	/* Panel type */
	uint8_t panel_type;
};

struct dsi_data {
	bool initialized;
	bool video_mode;
};

/* ========================================================================= */
/*                       Wait helpers                                        */
/* ========================================================================= */

static int dsi_wait_cpsr_clear(dsi_registers_t *regs, uint32_t mask)
{
	uint32_t start = k_cycle_get_32();

	while ((regs->DSI_CMD_PKT_STATUS & mask) == mask) {
		if (k_cyc_to_us_ceil32(k_cycle_get_32() - start) > DSI_TIMEOUT_US) {
			LOG_WRN("DSI CPSR timeout waiting for mask 0x%08x", mask);
			return -ETIMEDOUT;
		}
	}
	return 0;
}

static int dsi_wait_cpsr_set(dsi_registers_t *regs, uint32_t mask)
{
	uint32_t start = k_cycle_get_32();

	while ((regs->DSI_CMD_PKT_STATUS & mask) != mask) {
		if (k_cyc_to_us_ceil32(k_cycle_get_32() - start) > DSI_TIMEOUT_US) {
			LOG_WRN("DSI CPSR timeout waiting for mask 0x%08x set", mask);
			return -ETIMEDOUT;
		}
	}
	return 0;
}

/* ========================================================================= */
/*                       D-PHY Configuration                                 */
/* ========================================================================= */

static void dsi_dphy_write_test(dsi_registers_t *regs, uint8_t code, uint8_t data)
{
	regs->DSI_DPHY_TST_CTRL0 = DSI_DPHY_TST_CTRL0_PHY_TESTCLK(1);
	regs->DSI_DPHY_TST_CTRL1 = DSI_DPHY_TST_CTRL1_PHY_TESTEN(1) |
				   DSI_DPHY_TST_CTRL1_PHY_TESTDIN(code);
	regs->DSI_DPHY_TST_CTRL0 = DSI_DPHY_TST_CTRL0_PHY_TESTCLK(0);
	regs->DSI_DPHY_TST_CTRL1 = DSI_DPHY_TST_CTRL1_PHY_TESTDIN(data);
	regs->DSI_DPHY_TST_CTRL0 = DSI_DPHY_TST_CTRL0_PHY_TESTCLK(1);
	regs->DSI_DPHY_TST_CTRL0 = DSI_DPHY_TST_CTRL0_PHY_TESTCLK(0);
}

static int dsi_init_dphy(const struct dsi_config *cfg)
{
	dsi_registers_t *regs = cfg->regs;

	LOG_INF("Initializing D-PHY: %u lanes, bit_clk=%u kHz",
		cfg->num_lanes, cfg->bit_clock_khz);

	/* Reset D-PHY */
	regs->DSI_DPHY_RSTZ = DSI_DPHY_RSTZ_DPHY_SHUTDOWNZ(0) |
			      DSI_DPHY_RSTZ_DPHY_RSTZ(0);

	/* Analog reset via test interface */
	regs->DSI_DPHY_TST_CTRL0 = DSI_DPHY_TST_CTRL0_PHY_TESTCLR(1);
	regs->DSI_DPHY_TST_CTRL0 = DSI_DPHY_TST_CTRL0_PHY_TESTCLR(0);

	/* Configure D-PHY PLL via test interface */
	dsi_dphy_write_test(regs, DPHY_CODE_HSBRC, cfg->dphy_hsbrc);
	dsi_dphy_write_test(regs, DPHY_CODE_VCO,
			    (1 << 7) | (cfg->dphy_vco_range << 3));
	dsi_dphy_write_test(regs, DPHY_CODE_ICPCTRL, cfg->dphy_icpctrl);
	dsi_dphy_write_test(regs, DPHY_CODE_LPFCTRL, cfg->dphy_lpfctrl);
	dsi_dphy_write_test(regs, DPHY_CODE_EN_NM, (1 << 5) | (1 << 4));
	dsi_dphy_write_test(regs, DPHY_CODE_NRATIO, cfg->dphy_n_ratio - 1);
	dsi_dphy_write_test(regs, DPHY_CODE_MRATIO,
			    (0 << 7) | (((cfg->dphy_m_ratio - 1) >> 0) & 0x1F));
	dsi_dphy_write_test(regs, DPHY_CODE_MRATIO,
			    (1 << 7) | (((cfg->dphy_m_ratio - 1) >> 5) & 0x1F));

	/* Enable D-PHY */
	regs->DSI_DPHY_RSTZ = DSI_DPHY_RSTZ_DPHY_SHUTDOWNZ(1) |
			      DSI_DPHY_RSTZ_DPHY_RSTZ(1) |
			      DSI_DPHY_RSTZ_DPHY_ENABLECLK(1);

	/* Wait for PLL lock */
	uint32_t start = k_cycle_get_32();

	while (!(regs->DSI_DPHY_STATUS & DSI_DPHY_STATUS_PHY_LOCK_Msk)) {
		if (k_cyc_to_us_ceil32(k_cycle_get_32() - start) > DSI_TIMEOUT_US) {
			LOG_ERR("D-PHY PLL lock timeout");
			return -ETIMEDOUT;
		}
	}

	LOG_INF("D-PHY PLL locked");

	/* Configure lane count and stop wait time */
	regs->DSI_DPHY_IF_CFG = DSI_DPHY_IF_CFG_N_LANES(cfg->num_lanes - 1) |
				DSI_DPHY_IF_CFG_PHY_STOP_WAIT_TIME(0);

	/* Configure lane timings */
	regs->DSI_DPHY_TMR_LPCLK_CFG =
		DSI_DPHY_TMR_LPCLK_CFG_PHY_CLKLP2HS_TIME(cfg->clk_lp2hs) |
		DSI_DPHY_TMR_LPCLK_CFG_PHY_CLKHS2LP_TIME(cfg->clk_hs2lp);
	regs->DSI_DPHY_TMR_CFG =
		DSI_DPHY_TMR_CFG_PHY_LP2HS_TIME(cfg->dat_lp2hs) |
		DSI_DPHY_TMR_CFG_PHY_HS2LP_TIME(cfg->dat_hs2lp);

	return 0;
}

/* ========================================================================= */
/*                       DSI Host Configuration                              */
/* ========================================================================= */

static void dsi_init_host(const struct dsi_config *cfg)
{
	dsi_registers_t *regs = cfg->regs;
	uint32_t esc_clk_div = cfg->byte_clock_khz / 20000 + 1;

	LOG_INF("Initializing DSI host: esc_clk_div=%u", esc_clk_div);

	/* Clock management */
	regs->DSI_CLKMGR_CFG = DSI_CLKMGR_CFG_TX_ESC_CLK_DIVISION(esc_clk_div) |
			       DSI_CLKMGR_CFG_TO_CLK_DIVISION(10);

	regs->DSI_LPCLK_CTRL = DSI_LPCLK_CTRL_PHY_TXREQUESTCLKHS(1) |
			       DSI_LPCLK_CTRL_AUTO_CLKLANE_CTRL(0);

	/* Protocol configuration */
	regs->DSI_PCKHDL_CFG = DSI_PCKHDL_CFG_EOTP_TX_EN(0) |
			       DSI_PCKHDL_CFG_EOTP_RX_EN(0) |
			       DSI_PCKHDL_CFG_BTA_EN(1) |
			       DSI_PCKHDL_CFG_ECC_RX_EN(1) |
			       DSI_PCKHDL_CFG_CRC_RX_EN(1) |
			       DSI_PCKHDL_CFG_EOTP_TX_LP_EN(0);
}

/* ========================================================================= */
/*                       Command / Video Mode                                */
/* ========================================================================= */

static void dsi_enter_command_mode(dsi_registers_t *regs)
{
	LOG_DBG("Entering DSI command mode");

	/* Timeout counters */
	regs->DSI_TO_CNT_CFG = DSI_TO_CNT_CFG_LPRX_TO_CNT(0xFFF) |
			       DSI_TO_CNT_CFG_HSTX_TO_CNT(0xFFF);
	regs->DSI_HS_RD_TO_CNT = DSI_HS_RD_TO_CNT_HS_RD_TO_CNT(0xFFF);
	regs->DSI_LP_RD_TO_CNT = DSI_LP_RD_TO_CNT_LP_RD_TO_CNT(0xFFF);
	regs->DSI_HS_WR_TO_CNT = DSI_HS_WR_TO_CNT_HS_WR_TO_CNT(0xFFF);
	regs->DSI_LP_WR_TO_CNT = DSI_LP_WR_TO_CNT_LP_WR_TO_CNT(0xFFF);
	regs->DSI_BTA_TO_CNT = DSI_BTA_TO_CNT_BTA_TO_CNT(0xFFF);

	/* Command mode */
	regs->DSI_MODE_CFG = DSI_MODE_CFG_CMD_VIDEO_MODE(1);

	/* Command mode configuration - enable LP transmission for all types */
	regs->DSI_CMD_MODE_CFG = DSI_CMD_MODE_CFG_TEAR_FX_EN(0) |
				 DSI_CMD_MODE_CFG_ACK_RQST_EN(0) |
				 DSI_CMD_MODE_CFG_GEN_SW_0P_TX(1) |
				 DSI_CMD_MODE_CFG_GEN_SW_1P_TX(1) |
				 DSI_CMD_MODE_CFG_GEN_SW_2P_TX(1) |
				 DSI_CMD_MODE_CFG_GEN_SR_0P_TX(1) |
				 DSI_CMD_MODE_CFG_GEN_SR_1P_TX(1) |
				 DSI_CMD_MODE_CFG_GEN_SR_2P_TX(1) |
				 DSI_CMD_MODE_CFG_GEN_LW_TX(1) |
				 DSI_CMD_MODE_CFG_DCS_SW_0P_TX(1) |
				 DSI_CMD_MODE_CFG_DCS_SW_1P_TX(1) |
				 DSI_CMD_MODE_CFG_DCS_SR_0P_TX(1) |
				 DSI_CMD_MODE_CFG_DCS_LW_TX(1) |
				 DSI_CMD_MODE_CFG_MAX_RD_PKT_SIZE(1);
}

static void dsi_enter_video_mode(const struct dsi_config *cfg)
{
	dsi_registers_t *regs = cfg->regs;
	uint32_t byte_clk = cfg->byte_clock_khz;
	uint32_t pix_clk = cfg->pixel_clock_khz;

	LOG_DBG("Entering DSI video mode");

	/* Video mode timing parameters (in lane byte clock cycles) */
	#define PIX_TO_LBCC(cycles) (((cycles) * byte_clk + pix_clk - 1) / pix_clk)

	regs->DSI_VID_PKT_SIZE = cfg->width;
	regs->DSI_VID_NUM_CHUNKS = 0;
	regs->DSI_VID_NULL_SIZE = 0;
	regs->DSI_VID_HSA_TIME = PIX_TO_LBCC(cfg->hspw);
	regs->DSI_VID_HBP_TIME = PIX_TO_LBCC(cfg->hbp);
	regs->DSI_VID_HLINE_TIME = PIX_TO_LBCC(cfg->width + cfg->hspw +
					       cfg->hbp + cfg->hfp);
	regs->DSI_VID_VSA_LINES = cfg->vspw;
	regs->DSI_VID_VBP_LINES = cfg->vbp;
	regs->DSI_VID_VFP_LINES = cfg->vfp;
	regs->DSI_VID_VACTIVE_LINES = cfg->height;

	#undef PIX_TO_LBCC

	/* Enter video mode */
	regs->DSI_MODE_CFG = DSI_MODE_CFG_CMD_VIDEO_MODE(0);

	/* DPI input configuration */
	regs->DSI_DPI_VCID = DSI_DPI_VCID_DPI_VCID(0);
	regs->DSI_DPI_COLOR_CODING = DSI_DPI_COLOR_CODING_DPI_COLOR_CODING(5); /* 24-bit */
	regs->DSI_DPI_LP_CMD_TIM = DSI_DPI_LP_CMD_TIM_INVACT_LPCMD_TIME(0) |
				   DSI_DPI_LP_CMD_TIM_OUTVACT_LPCMD_TIME(16);

	/* Video mode configuration - burst mode with LP transitions */
	regs->DSI_VID_MODE_CFG = DSI_VID_MODE_CFG_VID_MODE_TYPE(2) |
				 DSI_VID_MODE_CFG_LP_VSA_EN(1) |
				 DSI_VID_MODE_CFG_LP_VBP_EN(1) |
				 DSI_VID_MODE_CFG_LP_VFP_EN(1) |
				 DSI_VID_MODE_CFG_LP_VACT_EN(1) |
				 DSI_VID_MODE_CFG_LP_HBP_EN(1) |
				 DSI_VID_MODE_CFG_LP_HFP_EN(1) |
				 DSI_VID_MODE_CFG_FRAME_BTA_ACK_EN(0) |
				 DSI_VID_MODE_CFG_LP_CMD_EN(1) |
				 DSI_VID_MODE_CFG_VPG_EN(0);

	LOG_INF("Video mode: %ux%u, HSA=%u HBP=%u HFP=%u, VSA=%u VBP=%u VFP=%u",
		cfg->width, cfg->height,
		cfg->hspw, cfg->hbp, cfg->hfp,
		cfg->vspw, cfg->vbp, cfg->vfp);
}

/* ========================================================================= */
/*                       DCS Write Functions                                 */
/* ========================================================================= */

static int dsi_write_packet(dsi_registers_t *regs,
			    dsi_generic_header_t *hdr,
			    dsi_generic_payload_t *pld)
{
	int ret;

	/* Wait while command FIFO is full */
	ret = dsi_wait_cpsr_clear(regs, DSI_CPSR_CMD_FULL);
	if (ret) {
		return ret;
	}

	switch (hdr->long_pkt.data_type) {
	case DSI_DT_GEN_SHORT_WR_0P:
	case DSI_DT_GEN_SHORT_WR_1P:
	case DSI_DT_GEN_SHORT_WR_2P:
	case DSI_DT_DCS_SHORT_WR_0P:
	case DSI_DT_DCS_SHORT_WR_1P:
		regs->DSI_GEN_HDR = hdr->header_u32;
		ret = dsi_wait_cpsr_set(regs, DSI_CPSR_CMD_EMPTY);
		break;

	case DSI_DT_GEN_LONG_WR:
	case DSI_DT_DCS_LONG_WR:
		if (hdr->long_pkt.size > 4096 || hdr->long_pkt.size == 0) {
			return -EINVAL;
		}

		uint32_t pld_words = (hdr->long_pkt.size + 3) >> 2;

		for (uint32_t i = 0; i < pld_words; i++) {
			regs->DSI_GEN_PLD_DATA = pld[i].data_u32;
		}
		regs->DSI_GEN_HDR = hdr->header_u32;
		ret = dsi_wait_cpsr_set(regs, DSI_CPSR_CMD_EMPTY | DSI_CPSR_PLD_EMPTY);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int dsi_dcs_write(dsi_registers_t *regs, const uint8_t *data, size_t len)
{
	dsi_generic_header_t hdr = {0};
	dsi_generic_payload_t pld[32] = {0};

	if (len == 0) {
		return 0;
	}

	if (len == 1) {
		hdr.short_pkt.data_type = DSI_DT_DCS_SHORT_WR_0P;
		hdr.short_pkt.data[0] = data[0];
	} else if (len == 2) {
		hdr.short_pkt.data_type = DSI_DT_DCS_SHORT_WR_1P;
		hdr.short_pkt.data[0] = data[0];
		hdr.short_pkt.data[1] = data[1];
	} else {
		if (len > sizeof(pld)) {
			return -EINVAL;
		}
		hdr.long_pkt.data_type = DSI_DT_DCS_LONG_WR;
		hdr.long_pkt.size = len;
		memcpy(pld, data, len);
	}

	return dsi_write_packet(regs, &hdr, pld);
}

/* Helper macro for DCS command sequences */
#define DSI_DCS_WRITE_SEQ(regs, ...) \
	do { \
		static const uint8_t seq[] = { __VA_ARGS__ }; \
		dsi_dcs_write(regs, seq, sizeof(seq)); \
	} while (0)

/* ========================================================================= */
/*                       HX8394-F Panel Initialization                       */
/* ========================================================================= */

/* HX8394-F manufacturer commands */
#define HX8394_CMD_SETEXTC       0xB9
#define HX8394_CMD_SETMIPI       0xBA
#define HX8394_CMD_SETPOWER      0xB1
#define HX8394_CMD_SETDISP       0xB2
#define HX8394_CMD_SETCYC        0xB4
#define HX8394_CMD_SETVCOM       0xB6
#define HX8394_CMD_SETGIP0       0xD3
#define HX8394_CMD_SETGIP1       0xD5
#define HX8394_CMD_SETGIP2       0xD6
#define HX8394_CMD_SETGIP3       0xD8
#define HX8394_CMD_SETGAMMA      0xE0
#define HX8394_CMD_SETPANEL      0xCC
#define HX8394_CMD_SETREGBANK    0xBD
#define HX8394_CMD_SETCABC       0xC9
#define HX8394_CMD_UNKNOWN1      0xC0
#define HX8394_CMD_UNKNOWN2      0xC6
#define HX8394_CMD_UNKNOWN3      0xD4

static void dsi_init_panel_hx8394f(dsi_registers_t *regs)
{
	LOG_INF("Initializing HX8394-F panel");

	/* SETEXTC: Enable extended commands */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETEXTC, 0xFF, 0x83, 0x94);

	/* SETMIPI: MIPI control */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETMIPI,
			  0x63, 0x03, 0x68, 0x6B, 0xB2, 0xC0);

	/* SETPOWER: Power settings */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETPOWER,
			  0x48, 0x12, 0x72, 0x09, 0x32, 0x54,
			  0x71, 0x71, 0x57, 0x47);

	/* SETDISP: Display settings */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETDISP,
			  0x00, 0x80, 0x64, 0x0C, 0x0D, 0x2F,
			  0x00, 0x00, 0x00, 0x00, 0x00);

	/* SETCYC: Display waveform cycles */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETCYC,
			  0x73, 0x74, 0x73, 0x74, 0x73, 0x74,
			  0x01, 0x0C, 0x86, 0x75, 0x00, 0x3F,
			  0x73, 0x74, 0x73, 0x74, 0x73, 0x74,
			  0x01, 0x0C, 0x86);

	/* SETVCOM: VCOM voltage */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETVCOM, 0x6E, 0x6E);

	/* SETGIP0: GIP Option 0 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETGIP0,
			  0x00, 0x00, 0x07, 0x07, 0x40, 0x07,
			  0x0C, 0x00, 0x08, 0x10, 0x08, 0x00,
			  0x08, 0x54, 0x15, 0x0A, 0x05, 0x0A,
			  0x02, 0x15, 0x06, 0x05, 0x06, 0x47,
			  0x44, 0x0A, 0x0A, 0x4B, 0x10, 0x07,
			  0x07, 0x0C, 0x40);

	/* SETGIP1: GIP Option 1 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETGIP1,
			  0x1C, 0x1C, 0x1D, 0x1D, 0x00, 0x01,
			  0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			  0x08, 0x09, 0x0A, 0x0B, 0x24, 0x25,
			  0x18, 0x18, 0x26, 0x27, 0x18, 0x18,
			  0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			  0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			  0x18, 0x18, 0x20, 0x21, 0x18, 0x18,
			  0x18, 0x18);

	/* SETGIP2: GIP Option 2 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETGIP2,
			  0x1C, 0x1C, 0x1D, 0x1D, 0x07, 0x06,
			  0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
			  0x0B, 0x0A, 0x09, 0x08, 0x21, 0x20,
			  0x18, 0x18, 0x27, 0x26, 0x18, 0x18,
			  0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			  0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			  0x18, 0x18, 0x25, 0x24, 0x18, 0x18,
			  0x18, 0x18);

	/* SETGAMMA: Gamma curve */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETGAMMA,
			  0x00, 0x0A, 0x15, 0x1B, 0x1E, 0x21,
			  0x24, 0x22, 0x47, 0x56, 0x65, 0x66,
			  0x6E, 0x82, 0x88, 0x8B, 0x9A, 0x9D,
			  0x98, 0xA8, 0xB9, 0x5D, 0x5C, 0x61,
			  0x66, 0x6A, 0x6F, 0x7F, 0x7F, 0x00,
			  0x0A, 0x15, 0x1B, 0x1E, 0x21, 0x24,
			  0x22, 0x47, 0x56, 0x65, 0x65, 0x6E,
			  0x81, 0x87, 0x8B, 0x98, 0x9D, 0x99,
			  0xA8, 0xBA, 0x5D, 0x5D, 0x62, 0x67,
			  0x6B, 0x72, 0x7F, 0x7F);

	/* Unknown command C0 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_UNKNOWN1, 0x1F, 0x73);

	/* SETCABC */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETCABC, 0x76, 0x00, 0x30);

	/* SETPANEL */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETPANEL, 0x0B);

	/* Unknown command D4 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_UNKNOWN3, 0x02);

	/* Register bank 2 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETREGBANK, 0x02);

	/* SETGIP3 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETGIP3,
			  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F);

	/* Register bank 0 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETREGBANK, 0x00);

	/* Register bank 1 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETREGBANK, 0x01);

	/* SETPOWER bank 1 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETPOWER, 0x00);

	/* Register bank 0 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_SETREGBANK, 0x00);

	/* Unknown command C6 */
	DSI_DCS_WRITE_SEQ(regs, HX8394_CMD_UNKNOWN2, 0xED);

	/* Exit sleep mode */
	DSI_DCS_WRITE_SEQ(regs, DCS_EXIT_SLEEP_MODE);
	k_msleep(120);

	/*
	 * MADCTL (Memory Access Control) - Set panel rotation.
	 * For 90 degree rotation (landscape mode):
	 * - MV (bit 5) = 1: Row/Column exchange
	 * - MY (bit 7) = 1: Row address order (needed for correct orientation)
	 * This tells the panel to swap its X/Y addressing, so when we write
	 * a rotated framebuffer, it displays correctly in landscape.
	 */
	DSI_DCS_WRITE_SEQ(regs, DCS_SET_ADDRESS_MODE,
			  MADCTL_MV | MADCTL_MY);
	LOG_INF("HX8394-F: MADCTL set for 90-degree rotation (landscape)");

	/* Display on */
	DSI_DCS_WRITE_SEQ(regs, DCS_SET_DISPLAY_ON);
	k_msleep(20);

	LOG_INF("HX8394-F panel initialized");
}

/* ========================================================================= */
/*                       Driver Initialization                               */
/* ========================================================================= */

static int microchip_sam_dsi_init(const struct device *dev)
{
	const struct dsi_config *cfg = dev->config;
	struct dsi_data *data = dev->data;
	dsi_registers_t *regs = cfg->regs;
	int ret;

	LOG_INF("Initializing Microchip SAM DSI at %p", regs);
	LOG_INF("  Resolution: %ux%u, pixel_clk=%u kHz",
		cfg->width, cfg->height, cfg->pixel_clock_khz);

	/* Enable DSI peripheral clock */
	PMC_REGS->PMC_PCR = PMC_PCR_CMD_Msk |
			    PMC_PCR_PID(ID_DSI) |
			    PMC_PCR_EN_Msk |
			    PMC_PCR_GCLKEN_Msk |
			    PMC_PCR_GCLKDIV(0) |
			    PMC_PCR_GCLKCSS(PMC_PCR_GCLKCSS_MAINCK_Val);

	/* Reset DSI host */
	regs->DSI_PWR_UP = DSI_PWR_UP_SHUTDOWNZ(0);

	/* Initialize D-PHY */
	ret = dsi_init_dphy(cfg);
	if (ret) {
		LOG_ERR("D-PHY initialization failed: %d", ret);
		return ret;
	}

	/* Initialize DSI host */
	dsi_init_host(cfg);

	/* Power up DSI */
	regs->DSI_PWR_UP = DSI_PWR_UP_SHUTDOWNZ(1);

	/* Enter command mode for panel initialization */
	dsi_enter_command_mode(regs);

	/* Initialize panel (HX8394-F for AC40T08A) */
	if (cfg->panel_type == 0) {
		dsi_init_panel_hx8394f(regs);
	}

	/* Switch to video mode */
	dsi_enter_video_mode(cfg);

	data->initialized = true;
	data->video_mode = true;

	LOG_INF("DSI initialization complete");
	return 0;
}

/* ========================================================================= */
/*                       Device Instantiation                                */
/* ========================================================================= */

#define DSI_TIMING_PROP(inst, prop) \
	DT_PROP(DT_CHILD(DT_DRV_INST(inst), display_timings), prop)

#define DSI_TIMING_PROP_OR(inst, prop, def) \
	DT_PROP_OR(DT_CHILD(DT_DRV_INST(inst), display_timings), prop, def)

#define MICROCHIP_SAM_DSI_INIT(inst) \
	static struct dsi_data dsi_data_##inst; \
	static const struct dsi_config dsi_config_##inst = { \
		.regs = (dsi_registers_t *)DT_INST_REG_ADDR(inst), \
		.num_lanes = DT_INST_PROP(inst, num_lanes), \
		.pixel_clock_khz = DSI_TIMING_PROP(inst, clock_frequency) / 1000, \
		.width = DT_INST_PROP(inst, width), \
		.height = DT_INST_PROP(inst, height), \
		.hfp = DSI_TIMING_PROP(inst, hfront_porch), \
		.hbp = DSI_TIMING_PROP(inst, hback_porch), \
		.hspw = DSI_TIMING_PROP(inst, hsync_len), \
		.vfp = DSI_TIMING_PROP(inst, vfront_porch), \
		.vbp = DSI_TIMING_PROP(inst, vback_porch), \
		.vspw = DSI_TIMING_PROP(inst, vsync_len), \
		.dphy_n_ratio = DT_INST_PROP_OR(inst, dphy_n_ratio, 3), \
		.dphy_m_ratio = DT_INST_PROP_OR(inst, dphy_m_ratio, 57), \
		.dphy_hsbrc = DT_INST_PROP_OR(inst, dphy_hsbrc, 0x2C), \
		.dphy_vco_range = DT_INST_PROP_OR(inst, dphy_vco_range, 2), \
		.dphy_icpctrl = DT_INST_PROP_OR(inst, dphy_icpctrl, 5), \
		.dphy_lpfctrl = DT_INST_PROP_OR(inst, dphy_lpfctrl, 1), \
		.clk_lp2hs = DT_INST_PROP_OR(inst, clk_lp2hs_time, 73), \
		.clk_hs2lp = DT_INST_PROP_OR(inst, clk_hs2lp_time, 31), \
		.dat_lp2hs = DT_INST_PROP_OR(inst, dat_lp2hs_time, 59), \
		.dat_hs2lp = DT_INST_PROP_OR(inst, dat_hs2lp_time, 22), \
		.bit_clock_khz = DT_INST_PROP_OR(inst, bit_clock_khz, 456000), \
		.byte_clock_khz = DT_INST_PROP_OR(inst, bit_clock_khz, 456000) / 8, \
		.panel_type = DT_INST_ENUM_IDX_OR(inst, panel_type, 0), \
	}; \
	DEVICE_DT_INST_DEFINE(inst, \
			      microchip_sam_dsi_init, \
			      NULL, \
			      &dsi_data_##inst, \
			      &dsi_config_##inst, \
			      POST_KERNEL, \
			      CONFIG_MICROCHIP_SAM_DSI_INIT_PRIORITY, \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(MICROCHIP_SAM_DSI_INIT)
