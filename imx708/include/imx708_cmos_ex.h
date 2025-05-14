#ifndef __IMX708_CMOS_EX_H_
#define __IMX708_CMOS_EX_H_

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

// Include necessary headers from the platform (e.g., CVITEK SDK)
// These are based on gc4653_cmos_ex.h and might need adjustment
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

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

// Define IMX708 specific constants
#define IMX708_I2C_ADDR         0x1A  // From RPi driver research
#define IMX708_CHIP_ID_REG      0x0016 // From RPi driver research
#define IMX708_CHIP_ID          0x0708 // From RPi driver research

#define IMX708_ADDR_BYTE        2 // Assuming 2-byte register addresses
#define IMX708_DATA_BYTE        1 // Assuming 1-byte data for basic R/W, RPi driver handles 8/16 bit writes

// Basic mode definitions (to be expanded based on RPi driver and user needs)
// Example: Full resolution, binned mode
typedef enum _IMX708_MODE_E {
    IMX708_MODE_4608X2592P30 = 0, // Full resolution at 30fps
    IMX708_MODE_2304X1296P60,     // Example binned/scaled mode
    // Add other modes as needed
    IMX708_MODE_NUM
} IMX708_MODE_E;

// Sensor state structure (can be similar to GC4653_STATE_S initially)
typedef struct _IMX708_STATE_S {
    CVI_U32 u32Sexp_MAX; // Example, to be refined
    // Add other state variables as needed, e.g., for autofocus
    CVI_S32 current_focus_pos;
} IMX708_STATE_S;

// Sensor mode parameters structure
typedef struct _IMX708_MODE_S {
    ISP_WDR_SIZE_S astImg[2]; // For WDR, adapt as needed
    CVI_FLOAT f32MaxFps;
    CVI_FLOAT f32MinFps;
    CVI_U32 u32HtsDef; // Horizontal Total Size (Line Length)
    CVI_U32 u32VtsDef; // Vertical Total Size (Frame Length)
    SNS_ATTR_S stExp[2]; // Exposure attributes
    SNS_ATTR_LARGE_S stAgain[2]; // Analog gain attributes
    SNS_ATTR_LARGE_S stDgain[2]; // Digital gain attributes
    char name[64];
    // Add IMX708 specific mode parameters, e.g., from RPi driver struct imx708_mode
    CVI_U32 line_length_pck; // Line length in pixel clocks
    CVI_U32 exposure_min_lines;
    CVI_U32 exposure_step_lines;
    CVI_BOOL hdr_mode; // Flag for HDR
    // Register list for this mode
    // const struct imx708_reg_list *reg_list; (if porting RPi driver structure directly)
} IMX708_MODE_S;

/****************************************************************************
 * External variables and functions                                         *
 ****************************************************************************/

// Global sensor state array (one per VI_PIPE)
extern ISP_SNS_STATE_S *g_pastImx708[VI_MAX_PIPE_NUM];

// I2C bus information
extern ISP_SNS_COMMBUS_U g_aunImx708_BusInfo[];

// Mirror/flip status
extern ISP_SNS_MIRRORFLIP_TYPE_E g_aeImx708_MirrorFlip[VI_MAX_PIPE_NUM];

// I2C address for the sensor
extern CVI_U8 imx708_i2c_addr;

// Register and data byte lengths for I2C communication
extern const CVI_U32 imx708_addr_byte;
extern const CVI_U32 imx708_data_byte;

// Core sensor control functions to be defined in imx708_sensor_ctl.c
extern void imx708_init(VI_PIPE ViPipe);
extern void imx708_exit(VI_PIPE ViPipe);
extern void imx708_standby(VI_PIPE ViPipe);
extern void imx708_restart(VI_PIPE ViPipe);
extern int  imx708_write_register(VI_PIPE ViPipe, int addr, int data);
extern int  imx708_read_register(VI_PIPE ViPipe, int addr);
// Function to write multiple registers (useful for init sequences)
extern int imx708_write_register_array(VI_PIPE ViPipe, const struct _sensor_reg_s *reg_list, CVI_U32 num_regs);

extern int  imx708_probe(VI_PIPE ViPipe);

// Define a structure for register arrays if not already available
struct _sensor_reg_s {
    CVI_U16 reg_addr;
    CVI_U8  reg_val;
};

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* __IMX708_CMOS_EX_H_ */

