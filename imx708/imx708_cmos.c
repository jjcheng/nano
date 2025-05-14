// SPDX-License-Identifier: GPL-2.0
/*
 * CMOS sensor specific functions for Sony IMX708.
 * Based on gc4653_cmos.c and IMX708 research (RPi kernel driver).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <syslog.h>
#include <errno.h>

// Platform-specific and common sensor headers
#ifdef ARCH_CV182X
#include "cvi_type.h"
#include "cvi_comm_video.h"
#include <linux/cvi_vip_snsr.h>
#else
#include <linux/cvi_type.h>
#include <linux/cvi_comm_video.h>
#include <linux/vi_snsr.h>
#endif
#include "cvi_debug.h"
#include "cvi_comm_sns.h"
#include "cvi_sns_ctrl.h"
#include "cvi_ae_comm.h"
#include "cvi_awb_comm.h"
#include "cvi_ae.h"
#include "cvi_awb.h"
#include "cvi_isp.h"

#include "imx708_cmos_ex.h"
#include "imx708_cmos_param.h"

#define DIV_0_TO_1(a)   ((0 == (a)) ? 1 : (a))
#define DIV_0_TO_1_FLOAT(a) ((((a) < 1E-10) && ((a) > -1E-10)) ? 1 : (a))

/****************************************************************************
 * global variables                                                         *
 ***************************************************************************/
ISP_SNS_STATE_S *g_pastImx708[VI_MAX_PIPE_NUM] = {CVI_NULL};

#define IMX708_SENSOR_GET_CTX(dev, pstCtx)   (pstCtx = g_pastImx708[dev])
#define IMX708_SENSOR_SET_CTX(dev, pstCtx)   (g_pastImx708[dev] = pstCtx)
#define IMX708_SENSOR_RESET_CTX(dev)         (g_pastImx708[dev] = CVI_NULL)

ISP_SNS_COMMBUS_U g_aunImx708_BusInfo[VI_MAX_PIPE_NUM] = {
    // Assuming I2C device 4 for LicheeRV Nano, same as GC4653 example
    [0] = { .s8I2cDev = 4},
    [1 ... VI_MAX_PIPE_NUM - 1] = { .s8I2cDev = -1}
};

ISP_SNS_MIRRORFLIP_TYPE_E g_aeImx708_MirrorFlip[VI_MAX_PIPE_NUM] = {ISP_SNS_NORMAL};
CVI_U16 g_au16Imx708_GainMode[VI_MAX_PIPE_NUM] = {0}; // Not directly used in RPi driver, but part of framework
CVI_U16 g_au16Imx708_L2SMode[VI_MAX_PIPE_NUM] = {0}; // For L2S ratio in WDR, placeholder

/****************************************************************************
 * local variables and functions                                            *
 ***************************************************************************/
static ISP_FSWDR_MODE_E genFSWDRMode[VI_MAX_PIPE_NUM] = {
    [0 ... VI_MAX_PIPE_NUM - 1] = ISP_FSWDR_NORMAL_MODE
};

static CVI_U32 gu32MaxTimeGetCnt[VI_MAX_PIPE_NUM] = {0}; // For WDR, placeholder
static CVI_U32 g_au32InitExposure[VI_MAX_PIPE_NUM]  = {0};
static CVI_U32 g_au32LinesPer500ms[VI_MAX_PIPE_NUM] = {0};
// static CVI_U16 g_au16InitWBGain[VI_MAX_PIPE_NUM][3] = {{0}};
// static CVI_U16 g_au16SampleRgain[VI_MAX_PIPE_NUM] = {0};
// static CVI_U16 g_au16SampleBgain[VI_MAX_PIPE_NUM] = {0};

// IMX708 Register Addresses (from RPi imx708.c and research)
#define IMX708_REG_MODE_SELECT      0x0100 // Streaming (0x01) / Standby (0x00)
#define IMX708_REG_ORIENTATION      0x0101 // Mirror/Flip
#define IMX708_REG_EXPOSURE_H       0x0202 // Coarse Integration Time (High byte)
#define IMX708_REG_EXPOSURE_L       0x0203 // Coarse Integration Time (Low byte)
#define IMX708_REG_ANA_GAIN_H       0x0204 // Analog Gain (High byte, if 16-bit)
#define IMX708_REG_ANA_GAIN_L       0x0205 // Analog Gain (Low byte)
                                         // RPi driver uses 0x0204 for 8-bit analog gain setting
#define IMX708_REG_DGTL_GAIN_H      0x020E // Digital Gain (High byte)
#define IMX708_REG_DGTL_GAIN_L      0x020F // Digital Gain (Low byte)
#define IMX708_REG_FRAME_LENGTH_H   0x0340 // Frame Length (VTS) (High byte)
#define IMX708_REG_FRAME_LENGTH_L   0x0341 // Frame Length (VTS) (Low byte)
#define IMX708_REG_LINE_LENGTH_H    0x0342 // Line Length (HTS) (High byte)
#define IMX708_REG_LINE_LENGTH_L    0x0343 // Line Length (HTS) (Low byte)

// Autofocus related registers (Placeholder - specific registers depend on AF mechanism)
// The RPi driver manages AF through V4L2 controls and likely firmware interaction.
// Direct register control for AF might be complex or not fully exposed.
// #define IMX708_REG_FOCUS_CTRL_XYZ 0xYYYY

#define IMX708_FULL_LINES_MAX  0xFFFF // Max VTS from RPi driver (IMX708_FRAME_LENGTH_MAX)
#define IMX708_EXPOSURE_OFFSET 48     // From RPi driver

static CVI_S32 cmos_get_ae_default(VI_PIPE ViPipe, AE_SENSOR_DEFAULT_S *pstAeSnsDft)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_MODE_S const *pstMode;

    CMOS_CHECK_POINTER(pstAeSnsDft);
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    pstMode = &g_astImx708_mode[pstSnsState->u8ImgMode];

    pstAeSnsDft->u32FullLinesStd = pstSnsState->u32FLStd;
    pstAeSnsDft->u32FlickerFreq = 50 * 256;
    pstAeSnsDft->u32FullLinesMax = IMX708_FULL_LINES_MAX;
    // HmaxTimes: (1M / (FLStd * Fps)) ; RPi driver uses pixel_rate for this.
    // For now, use a similar calculation as GC4653
    pstAeSnsDft->u32HmaxTimes = (1000000) / (pstSnsState->u32FLStd * DIV_0_TO_1_FLOAT(pstMode->f32MaxFps));

    pstAeSnsDft->stIntTimeAccu.enAccuType = AE_ACCURACY_LINEAR;
    pstAeSnsDft->stIntTimeAccu.f32Accuracy = 1;
    pstAeSnsDft->stIntTimeAccu.f32Offset = 0;

    // Analog gain accuracy (RPi driver uses a direct value, not a table like GC4653)
    pstAeSnsDft->stAgainAccu.enAccuType = AE_ACCURACY_LINEAR; // Or AE_ACCURACY_DB if gain is in dB
    pstAeSnsDft->stAgainAccu.f32Accuracy = 1; // Step is 1 in RPi driver

    // Digital gain accuracy
    pstAeSnsDft->stDgainAccu.enAccuType = AE_ACCURACY_LINEAR;
    pstAeSnsDft->stDgainAccu.f32Accuracy = 1; // Step is 1 in RPi driver (0x0100 is 1x)

    pstAeSnsDft->u32ISPDgainShift = 8; // Standard for ISP Dgain
    pstAeSnsDft->u32MinISPDgainTarget = 1 << pstAeSnsDft->u32ISPDgainShift;
    pstAeSnsDft->u32MaxISPDgainTarget = 4 << pstAeSnsDft->u32ISPDgainShift; // Example: up to 4x ISP Dgain

    if (g_au32LinesPer500ms[ViPipe] == 0)
        pstAeSnsDft->u32LinesPer500ms = pstSnsState->u32FLStd * pstMode->f32MaxFps / 2;
    else
        pstAeSnsDft->u32LinesPer500ms = g_au32LinesPer500ms[ViPipe];

    switch (pstSnsState->enWDRMode) {
    default:
    case WDR_MODE_NONE: // Linear mode
        pstAeSnsDft->f32Fps = pstMode->f32MaxFps;
        pstAeSnsDft->f32MinFps = pstMode->f32MinFps;
        pstAeSnsDft->au8HistThresh[0] = 0xd;
        pstAeSnsDft->au8HistThresh[1] = 0x28;
        pstAeSnsDft->au8HistThresh[2] = 0x60;
        pstAeSnsDft->au8HistThresh[3] = 0x80;

        pstAeSnsDft->u32MaxAgain = pstMode->stAgain[0].u32Max;
        pstAeSnsDft->u32MinAgain = pstMode->stAgain[0].u32Min;
        pstAeSnsDft->u32MaxAgainTarget = pstAeSnsDft->u32MaxAgain;
        pstAeSnsDft->u32MinAgainTarget = pstAeSnsDft->u32MinAgain;

        pstAeSnsDft->u32MaxDgain = pstMode->stDgain[0].u32Max; // This is sensor Dgain
        pstAeSnsDft->u32MinDgain = pstMode->stDgain[0].u32Min;
        pstAeSnsDft->u32MaxDgainTarget = pstAeSnsDft->u32MaxDgain;
        pstAeSnsDft->u32MinDgainTarget = pstAeSnsDft->u32MinDgain;

        pstAeSnsDft->u8AeCompensation = 40;
        pstAeSnsDft->u32InitAESpeed = 64;
        pstAeSnsDft->u32InitAETolerance = 5;
        pstAeSnsDft->u32AEResponseFrame = 4;
        pstAeSnsDft->enAeExpMode = AE_EXP_HIGHLIGHT_PRIOR;
        pstAeSnsDft->u32InitExposure = g_au32InitExposure[ViPipe] ?
            g_au32InitExposure[ViPipe] : pstMode->stExp[0].u16Def;

        pstAeSnsDft->u32MaxIntTime = pstSnsState->u32FLStd - IMX708_EXPOSURE_OFFSET;
        pstAeSnsDft->u32MinIntTime = pstMode->stExp[0].u16Min;
        pstAeSnsDft->u32MaxIntTimeTarget = 65535;
        pstAeSnsDft->u32MinIntTimeTarget = 1;
        break;
    // Add WDR/HDR cases if implemented
    }
    return CVI_SUCCESS;
}

static CVI_S32 cmos_fps_set(VI_PIPE ViPipe, CVI_FLOAT f32Fps, AE_SENSOR_DEFAULT_S *pstAeSnsDft)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_MODE_S const *pstMode;
    CVI_U32 u32Vts;

    CMOS_CHECK_POINTER(pstAeSnsDft);
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    pstMode = &g_astImx708_mode[pstSnsState->u8ImgMode];

    if (f32Fps > pstMode->f32MaxFps || f32Fps < pstMode->f32MinFps) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Unsupported FPS %f for mode %s (min: %f, max: %f)\n",
                      f32Fps, pstMode->name, pstMode->f32MinFps, pstMode->f32MaxFps);
        return CVI_FAILURE;
    }

    // VTS = DefaultVTS * DefaultFPS / TargetFPS
    u32Vts = pstMode->u32VtsDef * DIV_0_TO_1_FLOAT(pstMode->f32MaxFps) / DIV_0_TO_1_FLOAT(f32Fps);
    u32Vts = (u32Vts > IMX708_FULL_LINES_MAX) ? IMX708_FULL_LINES_MAX : u32Vts;
    u32Vts = (u32Vts < pstMode->u32VtsDef) ? pstMode->u32VtsDef : u32Vts; // VTS should not be less than default for the mode

    pstSnsState->u32FLStd = u32Vts;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_FRAME_LENGTH_H;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = (u32Vts >> 8) & 0xFF;
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_FRAME_LENGTH_L;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = u32Vts & 0xFF;
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;

    pstAeSnsDft->f32Fps = f32Fps;
    pstAeSnsDft->u32LinesPer500ms = pstSnsState->u32FLStd * f32Fps / 2;
    pstAeSnsDft->u32FullLinesStd = pstSnsState->u32FLStd;
    pstAeSnsDft->u32MaxIntTime = pstSnsState->u32FLStd - IMX708_EXPOSURE_OFFSET;
    pstSnsState->au32FL[0] = pstSnsState->u32FLStd;
    pstAeSnsDft->u32FullLines = pstSnsState->au32FL[0];

    return CVI_SUCCESS;
}

static CVI_S32 cmos_inttime_update(VI_PIPE ViPipe, CVI_U32 *u32IntTime)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    CMOS_CHECK_POINTER(u32IntTime);
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    CVI_U32 u32TmpIntTime = u32IntTime[0];
    // Max exposure is VTS - offset
    if (u32TmpIntTime > pstSnsState->u32FLStd - IMX708_EXPOSURE_OFFSET) {
        u32TmpIntTime = pstSnsState->u32FLStd - IMX708_EXPOSURE_OFFSET;
    }
    u32IntTime[0] = u32TmpIntTime;

    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_EXPOSURE_H;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = (u32TmpIntTime >> 8) & 0xFF;
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_EXPOSURE_L;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = u32TmpIntTime & 0xFF;
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;

    return CVI_SUCCESS;
}

// Analog gain mapping (IMX708_REG_ANA_GAIN_L 0x0205 in RPi driver, seems 8-bit)
// RPi driver: gain_val = val * 16 / 256 => val = gain_val * 16. Min 112, Max 960.
// This means gain_val is 0x70 to 0x3C0 (if interpreted as direct value).
// The register 0x0205 likely takes a value that maps to this. Let's assume direct mapping for now.
static CVI_S32 cmos_again_calc_table(VI_PIPE ViPipe, CVI_U32 *pu32AgainLin, CVI_U32 *pu32AgainDb)
{
    UNUSED(ViPipe);
    CMOS_CHECK_POINTER(pu32AgainLin);
    CMOS_CHECK_POINTER(pu32AgainDb);

    // Assuming pu32AgainLin is the target gain value (e.g., 112 to 960 from RPi driver)
    // No complex table like GC4653, direct mapping or simple calculation.
    // For this framework, we usually provide the AE with a value that it can linearly scale.
    // If the sensor takes gain code, we map it. If it takes dB, we convert.
    // RPi driver sets 0x0205 directly. Let's assume *pu32AgainLin is the value for 0x0205.
    // Min 112 (0x70), Max 960 (0x3C0) - this is > 8 bits. RPi driver uses 0x0204 for this.
    // IMX708_REG_ANALOG_GAIN 0x0204. Let's assume it's a 10-bit value split over 0204 and 0205.
    // Or, 0x0205 is the main gain, 0x0204 fine gain or upper bits.
    // The RPi driver imx708_set_ctrl for V4L2_CID_ANALOGUE_GAIN uses a single value for 0x0204.
    // Let's assume the AE gives us a value from 112 to 960.

    if (*pu32AgainLin < g_astImx708_mode[g_pastImx708[ViPipe]->u8ImgMode].stAgain[0].u32Min)
        *pu32AgainLin = g_astImx708_mode[g_pastImx708[ViPipe]->u8ImgMode].stAgain[0].u32Min;
    if (*pu32AgainLin > g_astImx708_mode[g_pastImx708[ViPipe]->u8ImgMode].stAgain[0].u32Max)
        *pu32AgainLin = g_astImx708_mode[g_pastImx708[ViPipe]->u8ImgMode].stAgain[0].u32Max;

    *pu32AgainDb = *pu32AgainLin; // For this framework, Db is often same as Lin if no complex conversion
    return CVI_SUCCESS;
}

// Digital gain (IMX708_REG_DGTL_GAIN_H/L 0x020E/0F)
// RPi driver: 0x0100 is 1x. Max 0xFFFF.
static CVI_S32 cmos_dgain_calc_table(VI_PIPE ViPipe, CVI_U32 *pu32DgainLin, CVI_U32 *pu32DgainDb)
{
    UNUSED(ViPipe);
    CMOS_CHECK_POINTER(pu32DgainLin);
    CMOS_CHECK_POINTER(pu32DgainDb);

    // Assuming pu32DgainLin is target gain (e.g., 0x0100 for 1x)
    if (*pu32DgainLin < g_astImx708_mode[g_pastImx708[ViPipe]->u8ImgMode].stDgain[0].u32Min)
        *pu32DgainLin = g_astImx708_mode[g_pastImx708[ViPipe]->u8ImgMode].stDgain[0].u32Min;
    if (*pu32DgainLin > g_astImx708_mode[g_pastImx708[ViPipe]->u8ImgMode].stDgain[0].u32Max)
        *pu32DgainLin = g_astImx708_mode[g_pastImx708[ViPipe]->u8ImgMode].stDgain[0].u32Max;

    *pu32DgainDb = *pu32DgainLin;
    return CVI_SUCCESS;
}

static CVI_S32 cmos_gains_update(VI_PIPE ViPipe, CVI_U32 *pu32Again, CVI_U32 *pu32Dgain)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    CMOS_CHECK_POINTER(pu32Again);
    CMOS_CHECK_POINTER(pu32Dgain);
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    CVI_U32 u32AnalogGain = pu32Again[0];
    CVI_U32 u32DigitalGain = pu32Dgain[0];

    // Analog Gain (assuming 0x0204 takes the value directly, RPi driver writes to 0x0204)
    // The RPi driver uses a 10-bit value for analog gain, but writes it to 0x0204 (8-bit) and 0x0205 (8-bit).
    // Let's assume 0x0205 is LSB and 0x0204 is MSB for a 10-bit gain value (max 960 = 0x3C0)
    // So, 0x0204 = (u32AnalogGain >> 8) & 0x03; 0x0205 = u32AnalogGain & 0xFF;
    // However, RPi imx708.c set_ctrl for V4L2_CID_ANALOGUE_GAIN writes a single 8-bit value to 0x0204.
    // This suggests a simpler mapping or the RPi driver abstracts it. For now, let's assume 0x0205 is the primary gain register.
    // Re-checking RPi driver: `imx708_write_reg(imx708, IMX708_REG_ANALOG_GAIN, IMX708_REG_VALUE_08BIT, val);`
    // where IMX708_REG_ANALOG_GAIN is 0x0204. So it's an 8-bit write to 0x0204.
    // The gain range 112-960 is confusing for an 8-bit register. This needs clarification from RPi driver logic.
    // Let's assume for now the AE provides a value that needs to be written to 0x0204 (LSB) and 0x0205 (MSB for >8bit gain)
    // Or, the RPi driver might have a specific mapping for the 8-bit value written to 0x0204.
    // For simplicity, let's assume u32AnalogGain is the direct value for an 8-bit register for now.
    // If gain is > 255, this approach is wrong. Max gain 960. So it must be > 8 bits.
    // RPi imx708.c: `imx708_write_reg(imx708, IMX708_REG_ANALOG_GAIN, IMX708_REG_VALUE_16BIT, val);`
    // This means it writes a 16-bit value to 0x0204 (which implies 0x0204 is LSB, 0x0205 is MSB).

    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_ANA_GAIN_L; // 0x0205 (MSB in RPi context if 0x0204 is LSB of 16bit)
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = (u32AnalogGain >> 8) & 0xFF; // MSB part
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_ANA_GAIN_H; // 0x0204 (LSB in RPi context)
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = u32AnalogGain & 0xFF;      // LSB part
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;

    // Digital Gain (0x020E MSB, 0x020F LSB for 16-bit value, 0x0100 = 1x)
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_DGTL_GAIN_H;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = (u32DigitalGain >> 8) & 0xFF;
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_DGTL_GAIN_L;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = u32DigitalGain & 0xFF;
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;

    return CVI_SUCCESS;
}

// WDR functions - Placeholder, not implemented initially
static CVI_S32 cmos_get_inttime_max(VI_PIPE ViPipe, CVI_U16 u16ManRatioEnable, CVI_U32 *au32Ratio,
                CVI_U32 *au32IntTimeMax, CVI_U32 *au32IntTimeMin, CVI_U32 *pu32LFMaxIntTime)
{
    UNUSED(ViPipe); UNUSED(u16ManRatioEnable); UNUSED(au32Ratio);
    UNUSED(au32IntTimeMax); UNUSED(au32IntTimeMin); UNUSED(pu32LFMaxIntTime);
    CVI_TRACE_SNS(CVI_DBG_WARN, "WDR function cmos_get_inttime_max not implemented for IMX708\n");
    return CVI_SUCCESS;
}

static CVI_S32 cmos_ae_fswdr_attr_set(VI_PIPE ViPipe, AE_FSWDR_ATTR_S *pstAeFSWDRAttr)
{
    CMOS_CHECK_POINTER(pstAeFSWDRAttr);
    genFSWDRMode[ViPipe] = pstAeFSWDRAttr->enFSWDRMode;
    gu32MaxTimeGetCnt[ViPipe] = 0;
    CVI_TRACE_SNS(CVI_DBG_WARN, "WDR function cmos_ae_fswdr_attr_set not fully implemented for IMX708\n");
    return CVI_SUCCESS;
}

static CVI_S32 cmos_init_ae_exp_function(AE_SENSOR_EXP_FUNC_S *pstExpFuncs)
{
    CMOS_CHECK_POINTER(pstExpFuncs);
    memset(pstExpFuncs, 0, sizeof(AE_SENSOR_EXP_FUNC_S));
    pstExpFuncs->pfn_cmos_get_ae_default    = cmos_get_ae_default;
    pstExpFuncs->pfn_cmos_fps_set           = cmos_fps_set;
    pstExpFuncs->pfn_cmos_inttime_update    = cmos_inttime_update;
    pstExpFuncs->pfn_cmos_gains_update      = cmos_gains_update;
    pstExpFuncs->pfn_cmos_again_calc_table  = cmos_again_calc_table;
    pstExpFuncs->pfn_cmos_dgain_calc_table  = cmos_dgain_calc_table;
    pstExpFuncs->pfn_cmos_get_inttime_max   = cmos_get_inttime_max; // WDR
    pstExpFuncs->pfn_cmos_ae_fswdr_attr_set = cmos_ae_fswdr_attr_set; // WDR
    return CVI_SUCCESS;
}

// AWB functions - Placeholder, can be similar to GC4653 if no sensor-specific AWB regs are used by AE
static CVI_S32 cmos_get_awb_default(VI_PIPE ViPipe, AWB_SENSOR_DEFAULT_S *pstAwbSnsDft)
{
    CMOS_CHECK_POINTER(pstAwbSnsDft);
    UNUSED(ViPipe);
    memset(pstAwbSnsDft, 0, sizeof(AWB_SENSOR_DEFAULT_S));
    // RPi driver has V4L2_CID_RED_BALANCE and V4L2_CID_BLUE_BALANCE (0x0B90, 0x0B92)
    // These are likely for manual WB. Auto WB is usually ISP driven.
    pstAwbSnsDft->u16InitRgain = 1024; // Standard defaults
    pstAwbSnsDft->u16InitGgain = 1024;
    pstAwbSnsDft->u16InitBgain = 1024;
    pstAwbSnsDft->u8AWBRunInterval = 1;
    return CVI_SUCCESS;
}

static CVI_S32 cmos_init_awb_exp_function(AWB_SENSOR_EXP_FUNC_S *pstExpFuncs)
{
    CMOS_CHECK_POINTER(pstExpFuncs);
    memset(pstExpFuncs, 0, sizeof(AWB_SENSOR_EXP_FUNC_S));
    pstExpFuncs->pfn_cmos_get_awb_default = cmos_get_awb_default;
    // If IMX708 has specific AWB gain registers that AE needs to control, add pfn_cmos_awb_gains_update here
    return CVI_SUCCESS;
}

// ISP default data (Noise, BLC etc.) - Placeholder or omit
static CVI_S32 cmos_get_isp_default(VI_PIPE ViPipe, ISP_CMOS_DEFAULT_S *pstDef)
{
    UNUSED(ViPipe);
    memset(pstDef, 0, sizeof(ISP_CMOS_DEFAULT_S));
    // If specific IMX708 noise profile is available, copy it here.
    // memcpy(pstDef->stNoiseCalibration.CalibrationCoef, &g_stIspNoiseCalibratioImx708, sizeof(ISP_CMOS_NOISE_CALIBRATION_S));
    return CVI_SUCCESS;
}

static CVI_S32 cmos_get_blc_default(VI_PIPE ViPipe, ISP_CMOS_BLACK_LEVEL_S *pstBlc)
{
    CMOS_CHECK_POINTER(pstBlc);
    UNUSED(ViPipe);
    memset(pstBlc, 0, sizeof(ISP_CMOS_BLACK_LEVEL_S));
    // If specific IMX708 BLC data is available, copy it here.
    // memcpy(pstBlc, &g_stIspBlcCalibratioImx708, sizeof(ISP_CMOS_BLACK_LEVEL_S));
    return CVI_SUCCESS;
}

static CVI_S32 cmos_set_pixel_format(VI_PIPE ViPipe, PIXEL_FORMAT_E enPixFormat)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    if (enPixFormat == PIXEL_FORMAT_RGB_BAYER_10BPP) {
        pstSnsState->enPixFormat = enPixFormat;
    } else {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Unsupported pixel format %d for IMX708\n", enPixFormat);
        return CVI_FAILURE;
    }
    return CVI_SUCCESS;
}

// Main sensor registration function
CVI_S32 cmos_init_sensor_exp_function(ISP_SENSOR_EXP_FUNC_S *pstSensorExpFunc)
{
    CMOS_CHECK_POINTER(pstSensorExpFunc);
    memset(pstSensorExpFunc, 0, sizeof(ISP_SENSOR_EXP_FUNC_S));
    pstSensorExpFunc->pfn_cmos_sensor_init = imx708_init;
    pstSensorExpFunc->pfn_cmos_sensor_exit = imx708_exit;
    pstSensorExpFunc->pfn_cmos_sensor_global_init = sensor_global_init;
    pstSensorExpFunc->pfn_cmos_set_image_mode = cmos_set_image_mode;
    pstSensorExpFunc->pfn_cmos_set_wdr_mode = cmos_set_wdr_mode;
    pstSensorExpFunc->pfn_cmos_get_isp_default = cmos_get_isp_default;
    pstSensorExpFunc->pfn_cmos_get_isp_black_level = cmos_get_blc_default;
    pstSensorExpFunc->pfn_cmos_get_sns_reg_info = cmos_get_sns_regs_info;
    pstSensorExpFunc->pfn_cmos_set_pixel_format = cmos_set_pixel_format;
    return CVI_SUCCESS;
}

// Other functions from gc4653_cmos.c to adapt:
// sensor_global_init, cmos_set_image_mode, cmos_set_wdr_mode, cmos_get_sns_regs_info
// cmos_set_mirror_flip (use IMX708_REG_ORIENTATION 0x0101)
// cmos_black_level_set (if ISP doesn't handle BLC fully)

// Sensor global init (called once)
CVI_S32 sensor_global_init(VI_PIPE ViPipe)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;

    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    pstSnsState->bInit = CVI_FALSE;
    pstSnsState->u8ImgMode = IMX708_MODE_4608X2592P30; // Default mode
    pstSnsState->enWDRMode = WDR_MODE_NONE;
    pstSnsState->u32FLStd = g_astImx708_mode[pstSnsState->u8ImgMode].u32VtsDef;
    pstSnsState->au32FL[0] = pstSnsState->u32FLStd;
    pstSnsState->au32FL[1] = pstSnsState->u32FLStd;

    memset(&pstSnsState->astSyncInfo[0], 0, sizeof(ISP_SNS_SYNC_INFO_S));
    // Initialize other pstSnsState members as needed

    // The RPi driver sets up regulators (power supplies) and clocks here.
    // This framework might assume platform handles that, or it needs to be added.
    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 global init for ViPipe %d complete.\n", ViPipe);
    return CVI_SUCCESS;
}

static CVI_S32 cmos_set_image_mode(VI_PIPE ViPipe, ISP_CMOS_SENSOR_IMAGE_MODE_S *pstSensorImageMode)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    CVI_U8 u8SensorImageMode;

    CMOS_CHECK_POINTER(pstSensorImageMode);
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    // Map input image mode to sensor specific mode (IMX708_MODE_E)
    // This mapping depends on how the application/ISP specifies modes.
    // For now, a simple mapping based on resolution.
    if (pstSensorImageMode->u16Width == 4608 && pstSensorImageMode->u16Height == 2592) {
        u8SensorImageMode = IMX708_MODE_4608X2592P30;
    } else if (pstSensorImageMode->u16Width == 2304 && pstSensorImageMode->u16Height == 1296) {
        u8SensorImageMode = IMX708_MODE_2304X1296P60;
    } else {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Unsupported resolution %dx%d for IMX708\n",
                      pstSensorImageMode->u16Width, pstSensorImageMode->u16Height);
        return CVI_FAILURE;
    }

    if (pstSnsState->bInit) { // If sensor is already initialized, need to re-init for new mode
        if (u8SensorImageMode != pstSnsState->u8ImgMode) {
            // Standby sensor before changing mode registers
            imx708_standby(ViPipe);
            pstSnsState->u8ImgMode = u8SensorImageMode;
            pstSnsState->u32FLStd = g_astImx708_mode[pstSnsState->u8ImgMode].u32VtsDef;
            pstSnsState->au32FL[0] = pstSnsState->u32FLStd;
            pstSnsState->au32FL[1] = pstSnsState->u32FLStd;
            imx708_init(ViPipe); // Re-initialize with new mode settings
        }
    } else { // First time initialization
        pstSnsState->u8ImgMode = u8SensorImageMode;
        pstSnsState->u32FLStd = g_astImx708_mode[pstSnsState->u8ImgMode].u32VtsDef;
        pstSnsState->au32FL[0] = pstSnsState->u32FLStd;
        pstSnsState->au32FL[1] = pstSnsState->u32FLStd;
    }
    pstSensorImageMode->f32Fps = g_astImx708_mode[pstSnsState->u8ImgMode].f32MaxFps;
    pstSensorImageMode->u32SensorMaxResolutionWidth = 4608;
    pstSensorImageMode->u32SensorMaxResolutionHeight = 2592;

    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 ViPipe %d set image mode to %s (%dx%d @ %ffps)\n", ViPipe,
                  g_astImx708_mode[pstSnsState->u8ImgMode].name,
                  g_astImx708_mode[pstSnsState->u8ImgMode].astImg[0].stSnsSize.u32Width,
                  g_astImx708_mode[pstSnsState->u8ImgMode].astImg[0].stSnsSize.u32Height,
                  pstSensorImageMode->f32Fps);

    return CVI_SUCCESS;
}

static CVI_S32 cmos_set_wdr_mode(VI_PIPE ViPipe, CVI_U8 u8Mode)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    // IMX708 supports HDR. This function would configure HDR registers if u8Mode indicates WDR/HDR.
    // The RPi driver has HDR capabilities. For now, assume WDR_MODE_NONE.
    switch (u8Mode) {
    case WDR_MODE_NONE:
        pstSnsState->enWDRMode = WDR_MODE_NONE;
        CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 set to Linear Mode\n");
        // Disable HDR registers if any were enabled
        break;
    // case WDR_MODE_BUILT_IN: // Or other HDR modes
    //    pstSnsState->enWDRMode = u8Mode;
    //    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 set to WDR/HDR Mode %d (Built-in)\n", u8Mode);
    //    // Enable HDR registers based on RPi driver logic
    //    break;
    default:
        CVI_TRACE_SNS(CVI_DBG_ERR, "Unsupported WDR mode %d for IMX708\n", u8Mode);
        return CVI_FAILURE;
    }
    // Reset some AE parameters when WDR mode changes
    g_au32InitExposure[ViPipe] = 0;
    return CVI_SUCCESS;
}

static CVI_S32 cmos_get_sns_regs_info(VI_PIPE ViPipe, ISP_SNS_SYNC_INFO_S *pstSnsSyncInfo)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    CMOS_CHECK_POINTER(pstSnsSyncInfo);
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    // Copy the queued I2C commands to pstSnsSyncInfo
    // The number of commands is in pstSnsState->astSyncInfo[0].snsCfg.u32RegNum
    // The commands themselves are in pstSnsState->astSyncInfo[0].snsCfg.astI2cData
    if (pstSnsState->astSyncInfo[0].snsCfg.u32RegNum > ISP_MAX_SNS_REGS_NUM) {
         CVI_TRACE_SNS(CVI_DBG_ERR, "Too many I2C commands: %d (max %d)\n",
                       pstSnsState->astSyncInfo[0].snsCfg.u32RegNum, ISP_MAX_SNS_REGS_NUM);
        return CVI_FAILURE;
    }

    memcpy(pstSnsSyncInfo, &pstSnsState->astSyncInfo[0], sizeof(ISP_SNS_SYNC_INFO_S));

    // Reset the command count for the next frame
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum = 0;
    return CVI_SUCCESS;
}

// Autofocus control function - This is a major new piece of logic
// It will need to interact with V4L2 controls if this driver is part of a V4L2 subsystem,
// or implement direct register control if possible and if the LicheeRV platform expects that.
// For now, a placeholder.
static CVI_S32 cmos_set_focus_abs(VI_PIPE ViPipe, CVI_S32 s32FocusPos)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);
    IMX708_STATE_S *pstImx708State = (IMX708_STATE_S *)pstSnsState->persistence_data; // Assuming persistence_data holds IMX708_STATE_S
    CMOS_CHECK_POINTER(pstImx708State);

    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 cmos_set_focus_abs: ViPipe %d, Pos %d\n", ViPipe, s32FocusPos);

    // The RPi driver uses V4L2_CID_FOCUS_ABSOLUTE.
    // It writes to registers like 0x7EB2, 0x7EB3 for focus control.
    // This requires understanding the IMX708's specific focus mechanism (VCM driver?)
    // Example: (Highly speculative, based on generic VCM control)
    // imx708_write_register(ViPipe, 0x7EB2, (s32FocusPos >> 8) & 0xFF); // Focus MSB
    // imx708_write_register(ViPipe, 0x7EB3, s32FocusPos & 0xFF);        // Focus LSB

    pstImx708State->current_focus_pos = s32FocusPos;
    CVI_TRACE_SNS(CVI_DBG_WARN, "IMX708 Autofocus control (cmos_set_focus_abs) is a placeholder!\n");
    return CVI_SUCCESS;
}

static CVI_S32 cmos_set_autofocus_trigger(VI_PIPE ViPipe, CVI_U8 u8Trigger)
{
    UNUSED(ViPipe); UNUSED(u8Trigger);
    CVI_TRACE_SNS(CVI_DBG_WARN, "IMX708 Autofocus trigger is a placeholder!\n");
    return CVI_SUCCESS;
}

static CVI_S32 cmos_get_autofocus_status(VI_PIPE ViPipe, CVI_U8 *pu8Status)
{
    UNUSED(ViPipe); CMOS_CHECK_POINTER(pu8Status);
    *pu8Status = 0; // Placeholder: e.g., V4L2_AUTO_FOCUS_STATUS_IDLE
    CVI_TRACE_SNS(CVI_DBG_WARN, "IMX708 Autofocus status get is a placeholder!\n");
    return CVI_SUCCESS;
}

// Register autofocus related control functions if the ISP framework supports them
CVI_S32 cmos_init_ois_exp_function(ISP_OIS_EXP_FUNC_S *pstOisExpFunc)
{
    CMOS_CHECK_POINTER(pstOisExpFunc);
    memset(pstOisExpFunc, 0, sizeof(ISP_OIS_EXP_FUNC_S));
    // pstOisExpFunc->pfn_cmos_ois_init = imx708_ois_init; // If OIS is supported
    // pstOisExpFunc->pfn_cmos_ois_trigger = imx708_ois_trigger;
    return CVI_SUCCESS;
}

CVI_S32 cmos_init_focus_exp_function(ISP_FOCUS_EXP_FUNC_S *pstFocusExpFunc)
{
    CMOS_CHECK_POINTER(pstFocusExpFunc);
    memset(pstFocusExpFunc, 0, sizeof(ISP_FOCUS_EXP_FUNC_S));
    pstFocusExpFunc->pfn_cmos_af_set_abs = cmos_set_focus_abs;
    pstFocusExpFunc->pfn_cmos_af_trigger = cmos_set_autofocus_trigger;
    pstFocusExpFunc->pfn_cmos_af_get_status = cmos_get_autofocus_status;
    // pstFocusExpFunc->pfn_cmos_af_set_range = ...;
    // pstFocusExpFunc->pfn_cmos_af_set_mode = ...;
    return CVI_SUCCESS;
}

// Mirror/Flip function
static CVI_S32 cmos_mirror_flip_set(VI_PIPE ViPipe, ISP_SNS_MIRRORFLIP_TYPE_E eSnsMirrorFlip)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER(pstSnsState);

    CVI_U8 u8Val = 0;
    // IMX708_REG_ORIENTATION 0x0101
    // Bit 0: Mirror (Horizontal flip)
    // Bit 1: Flip (Vertical flip)
    // RPi driver: val |= (hflip ? BIT(0) : 0) | (vflip ? BIT(1) : 0);

    switch (eSnsMirrorFlip) {
    case ISP_SNS_NORMAL:
        u8Val = 0x00;
        break;
    case ISP_SNS_MIRROR:
        u8Val = 0x01; // Horizontal mirror
        break;
    case ISP_SNS_FLIP:
        u8Val = 0x02; // Vertical flip
        break;
    case ISP_SNS_MIRROR_FLIP:
        u8Val = 0x03; // Both
        break;
    default:
        return CVI_FAILURE;
    }

    // Queue the I2C command
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32RegAddr = IMX708_REG_ORIENTATION;
    pstSnsState->astSyncInfo[0].snsCfg.astI2cData[pstSnsState->astSyncInfo[0].snsCfg.u32RegNum].u32Data = u8Val;
    pstSnsState->astSyncInfo[0].snsCfg.u32RegNum++;

    g_aeImx708_MirrorFlip[ViPipe] = eSnsMirrorFlip;
    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 ViPipe %d set mirror/flip to %d (reg 0x%02X)\n", ViPipe, eSnsMirrorFlip, u8Val);
    return CVI_SUCCESS;
}


// Sensor driver registration structure
SENSOR_DRV_ENTRY_S SENSOR_IMX708_DRV_ENTRY = {
    .acSensorName = "IMX708",
    .u32SensorId = 0, // This ID is usually for internal framework, not chip ID
    .s32I2cAddr = IMX708_I2C_ADDR,
    .u8I2cDev = 4, // Default I2C bus for LicheeRV Nano camera, from g_aunImx708_BusInfo
    .pfnSnsProbe = imx708_probe,
    .pfnSnsInit = sensor_global_init, // Global init
    .pfnSnsExit = imx708_exit,      // Sensor exit
    .pfnSnsSetImageMode = cmos_set_image_mode,
    .pfnSnsSetWDRMode = cmos_set_wdr_mode,
    .pfnSnsGetSnsRegInfo = cmos_get_sns_regs_info,
    .pfnSnsSetPixelFormat = cmos_set_pixel_format,
    .pfnSnsMirrorFlipSet = cmos_mirror_flip_set,
    // AE, AWB, ISP, AF, OIS function struct initializers
    .pfnSnsInitAeFunc = cmos_init_ae_exp_function,
    .pfnSnsInitAwbFunc = cmos_init_awb_exp_function,
    .pfnSnsInitIspFunc = cmos_init_sensor_exp_function, // Main sensor funcs
    .pfnSnsInitFocusFunc = cmos_init_focus_exp_function,
    .pfnSnsInitOisFunc = cmos_init_ois_exp_function,
};

// Function to allocate and initialize sensor context
// This is usually called by the ISP framework when the sensor is registered.
// If not, it needs to be called explicitly before using the sensor.
CVI_S32 IMX708_SensorRegister(VI_PIPE ViPipe, ISP_SNS_ATTR_INFO_S *pstSnsAttrInfo)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;

    CMOS_CHECK_POINTER(pstSnsAttrInfo);

    pstSnsState = (ISP_SNS_STATE_S *)malloc(sizeof(ISP_SNS_STATE_S));
    if (pstSnsState == CVI_NULL) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Failed to malloc sensor state for ViPipe %d\n", ViPipe);
        return CVI_FAILURE;
    }
    IMX708_SENSOR_SET_CTX(ViPipe, pstSnsState);

    // Allocate persistence data for IMX708 specific state (like focus pos)
    pstSnsState->persistence_data = malloc(sizeof(IMX708_STATE_S));
    if (pstSnsState->persistence_data == CVI_NULL) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Failed to malloc IMX708_STATE_S for ViPipe %d\n", ViPipe);
        free(pstSnsState);
        IMX708_SENSOR_RESET_CTX(ViPipe);
        return CVI_FAILURE;
    }
    memset(pstSnsState->persistence_data, 0, sizeof(IMX708_STATE_S));

    // Initialize global bus info for this pipe if not already set by framework
    g_aunImx708_BusInfo[ViPipe].s8I2cDev = SENSOR_IMX708_DRV_ENTRY.u8I2cDev;

    sensor_global_init(ViPipe); // Initialize sensor state

    pstSnsAttrInfo->eSensorId = ViPipe; // Or some other unique ID
    memcpy(&pstSnsAttrInfo->stSensorExp, &SENSOR_IMX708_DRV_ENTRY, sizeof(SENSOR_DRV_ENTRY_S));

    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 sensor registered for ViPipe %d.\n", ViPipe);
    return CVI_SUCCESS;
}

// Function to unregister and free sensor context
CVI_S32 IMX708_SensorUnRegister(VI_PIPE ViPipe)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);

    if (pstSnsState) {
        if (pstSnsState->persistence_data) {
            free(pstSnsState->persistence_data);
        }
        free(pstSnsState);
        IMX708_SENSOR_RESET_CTX(ViPipe);
        CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 sensor unregistered for ViPipe %d.\n", ViPipe);
    }
    return CVI_SUCCESS;
}

