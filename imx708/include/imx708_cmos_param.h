#ifndef __IMX708_CMOS_PARAM_H_
#define __IMX708_CMOS_PARAM_H_

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

// Include necessary headers from the platform (e.g., CVITEK SDK)
#ifdef ARCH_CV182X
#include <linux/cvi_vip_cif.h>
#include <linux/cvi_vip_snsr.h>
#include "cvi_type.h"
#else
#include <linux/cif_uapi.h>
#include <linux/vi_snsr.h>
#include <linux/cvi_type.h>
#endif
#include "cvi_sns_ctrl.h"
#include "imx708_cmos_ex.h" // IMX708 specific definitions from this driver

// Define register lists for different modes, similar to RPi driver structure
// These are placeholder structures and would be filled with actual register values
// from the RPi kernel driver (imx708.c -> mode_common_regs, mode_4608x2592_regs etc.)

// Example: Common registers (to be applied before mode-specific regs)
// static const struct _sensor_reg_s imx708_mode_common_regs[] = {
//    {0x0100, 0x00}, // Example: standby
//    // ... many more common registers from RPi imx708.c ...
// };

// Example: Registers for 4608x2592 mode
// static const struct _sensor_reg_s imx708_mode_4608x2592_regs[] = {
//    {0x0342, 0x3D}, // Example: LINE_LENGTH_PCK_H (from RPi driver)
//    {0x0343, 0x20}, // Example: LINE_LENGTH_PCK_L
//    // ... many more mode-specific registers ...
// };

// Sensor mode definitions based on IMX708_MODE_E from imx708_cmos_ex.h
// The parameters here are derived from RPi imx708.c driver's struct imx708_mode
static const IMX708_MODE_S g_astImx708_mode[IMX708_MODE_NUM] = {
    [IMX708_MODE_4608X2592P30] = {
        .name = "4608x2592P30",
        .astImg[0] = { // Assuming linear mode, WDR would use astImg[1]
            .stSnsSize = { .u32Width = 4608, .u32Height = 2592 },
            .stWndRect = { .s32X = 0, .s32Y = 0, .u32Width = 4608, .u32Height = 2592 },
            .stMaxSize = { .u32Width = 4608, .u32Height = 2592 },
        },
        .f32MaxFps = 30.0,
        .f32MinFps = 1.0, // Placeholder, RPi driver calculates this based on VMAX
        // From RPi imx708.c for 4608x2592 mode:
        // .line_length_pix = 0x3d20 => 15648 (LINE_LENGTH_PCK_A)
        // .vblank_min = 2649 => (FRM_LENGTH_A)
        // .vblank_default = 2649
        .u32HtsDef = 15648, // Horizontal Total Size (Line Length in pixel clocks)
        .u32VtsDef = 2649,  // Vertical Total Size (Frame length in lines)
        .line_length_pck = 15648, // from RPi driver
        .exposure_min_lines = 1, // from RPi driver (IMX708_EXPOSURE_MIN)
        .exposure_step_lines = 1, // from RPi driver (IMX708_EXPOSURE_STEP)
        .hdr_mode = CVI_FALSE,
        .stExp[0] = { .u16Min = 1, .u16Max = 0xFFFF /* VTS - offset */, .u16Def = 0x640, .u16Step = 1 },
        .stAgain[0] = { .u32Min = 112, .u32Max = 960, .u32Def = 112, .u32Step = 1 }, // Based on RPi IMX708_ANA_GAIN_MIN/MAX/STEP
        .stDgain[0] = { .u32Min = 0x0100, .u32Max = 0xFFFF, .u32Def = 0x0100, .u32Step = 1 }, // Based on RPi IMX708_DGTL_GAIN_MIN/MAX/STEP
    },
    [IMX708_MODE_2304X1296P60] = {
        .name = "2304x1296P60_BINNED", // Assuming 2x2 binned from full sensor or a crop
        .astImg[0] = {
            .stSnsSize = { .u32Width = 2304, .u32Height = 1296 },
            .stWndRect = { .s32X = 0, .s32Y = 0, .u32Width = 2304, .u32Height = 1296 },
            .stMaxSize = { .u32Width = 2304, .u32Height = 1296 },
        },
        .f32MaxFps = 60.0,
        .f32MinFps = 1.0, // Placeholder
        // From RPi imx708.c for 2304x1296 mode (mode_2x2binned_regs):
        // .line_length_pix = 0x1e90 => 7824
        // .vblank_min = 1336
        // .vblank_default = 1336
        .u32HtsDef = 7824,
        .u32VtsDef = 1336,
        .line_length_pck = 7824,
        .exposure_min_lines = 1,
        .exposure_step_lines = 1,
        .hdr_mode = CVI_FALSE,
        .stExp[0] = { .u16Min = 1, .u16Max = 0xFFFF, .u16Def = 0x320, .u16Step = 1 },
        .stAgain[0] = { .u32Min = 112, .u32Max = 960, .u32Def = 112, .u32Step = 1 },
        .stDgain[0] = { .u32Min = 0x0100, .u32Max = 0xFFFF, .u32Def = 0x0100, .u32Step = 1 },
    },
    // Add other modes as defined in IMX708_MODE_E and based on RPi driver capabilities
};

// ISP Calibration Data - Placeholder or to be omitted initially
// These would need to be specifically calibrated for IMX708 on LicheeRV Nano.
// For now, we can use dummy values or omit them if the ISP framework handles defaults.

// static ISP_CMOS_NOISE_CALIBRATION_S g_stIspNoiseCalibratioImx708 = { ... };
// static ISP_CMOS_BLACK_LEVEL_S g_stIspBlcCalibratioImx708 = { ... };

// MIPI RX attributes - these might need adjustment based on LicheeRV Nano's CSI controller
// and IMX708's output configuration (e.g., number of lanes, data rate).
// The GC4653 example used: .input_mode = INPUT_MODE_MIPI, .mac_clk = RX_MAC_CLK_200M,
// .raw_data_type = RAW_DATA_10BIT, .lane_id = {4, 3, 2, -1, -1} (for beta board)
// IMX708 on RPi Cam V3 uses 2 lanes. Data type is often 10-bit (RAW10).

static struct combo_dev_attr_s imx708_rx_attr = {
    .input_mode = INPUT_MODE_MIPI,
    .mac_clk = RX_MAC_CLK_450M, // Placeholder, adjust based on IMX708 pixel clock & MIPI config
                                // RPi IMX708 link_freq is 450MHz per lane for 2 lanes.
    .mipi_attr = {
        .raw_data_type = RAW_DATA_10BIT, // Common for IMX sensors
        .lane_id = {0, 1, -1, -1, -1}, // Assuming 2 lanes, map to LicheeRV Nano's CSI lanes
                                       // Check LicheeRV Nano schematics for CSI lane mapping
        .pn_swap = {0, 0, 0, 0, 0},    // Default, check if needed
        .wdr_mode = CVI_MIPI_WDR_MODE_NONE, // Or other if HDR is implemented via MIPI WDR
    },
    .mclk = {
        .cam = 0, // MCLK ID, platform specific
        .freq = CAMPLL_FREQ_24M, // IMX708 XCLK_FREQ is 24MHz from RPi driver
    },
    .devno = 0, // Sensor device number, platform specific
};

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* __IMX708_CMOS_PARAM_H_ */

