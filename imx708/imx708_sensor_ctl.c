// SPDX-License-Identifier: GPL-2.0
/*
 * Basic sensor control for Sony IMX708 camera.
 * Based on gc4653_sensor_ctl.c and IMX708 research (RPi kernel driver).
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

// Include platform-specific and common sensor headers
#ifdef ARCH_CV182X
#include <linux/cvi_vip_snsr.h>
#include "cvi_comm_video.h"
#else
#include <linux/vi_snsr.h>
#include <linux/cvi_comm_video.h>
#endif
#include "cvi_sns_ctrl.h"
#include "imx708_cmos_ex.h" // IMX708 specific definitions

// Define global I2C address for IMX708 (from imx708_cmos_ex.h)
CVI_U8 imx708_i2c_addr = IMX708_I2C_ADDR;
const CVI_U32 imx708_addr_byte = IMX708_ADDR_BYTE;
const CVI_U32 imx708_data_byte = IMX708_DATA_BYTE;

static int g_imx708_fd[VI_MAX_PIPE_NUM] = {[0 ... (VI_MAX_PIPE_NUM - 1)] = -1};

// Forward declarations for mode-specific initializations
static void imx708_linear_4608x2592_init(VI_PIPE ViPipe);
// Add other mode init function declarations as they are defined

/**
 * @brief Initialize I2C communication for a given sensor pipe.
 *
 * @param ViPipe The video input pipe ID.
 * @return CVI_S32 CVI_SUCCESS on success, CVI_FAILURE otherwise.
 */
int imx708_i2c_init(VI_PIPE ViPipe)
{
    char acDevFile[16] = {0};
    CVI_U8 u8DevNum;
    int ret;

    if (ViPipe < 0 || ViPipe >= VI_MAX_PIPE_NUM) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "ViPipe %d out of range!\n", ViPipe);
        return CVI_FAILURE;
    }

    if (g_imx708_fd[ViPipe] >= 0) {
        CVI_TRACE_SNS(CVI_DBG_INFO, "I2C for ViPipe %d already initialized.\n", ViPipe);
        return CVI_SUCCESS;
    }

    u8DevNum = g_aunImx708_BusInfo[ViPipe].s8I2cDev;
    if (u8DevNum == (CVI_U8)-1) { // Check if I2C device is configured
        CVI_TRACE_SNS(CVI_DBG_ERR, "I2C device not configured for ViPipe %d\n", ViPipe);
        return CVI_FAILURE;
    }

    snprintf(acDevFile, sizeof(acDevFile), "/dev/i2c-%u", u8DevNum);
    g_imx708_fd[ViPipe] = open(acDevFile, O_RDWR, 0600);

    if (g_imx708_fd[ViPipe] < 0) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Open %s error (%d)!\n", acDevFile, errno);
        return CVI_FAILURE;
    }

    ret = ioctl(g_imx708_fd[ViPipe], I2C_SLAVE_FORCE, imx708_i2c_addr);
    if (ret < 0) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_SLAVE_FORCE to 0x%x error (%d)!\n", imx708_i2c_addr, errno);
        close(g_imx708_fd[ViPipe]);
        g_imx708_fd[ViPipe] = -1;
        return CVI_FAILURE;
    }
    CVI_TRACE_SNS(CVI_DBG_INFO, "I2C for ViPipe %d initialized, addr 0x%x on /dev/i2c-%d\n", ViPipe, imx708_i2c_addr, u8DevNum);
    return CVI_SUCCESS;
}

/**
 * @brief Close I2C communication for a given sensor pipe.
 *
 * @param ViPipe The video input pipe ID.
 * @return CVI_S32 CVI_SUCCESS on success, CVI_FAILURE if not initialized.
 */
int imx708_i2c_exit(VI_PIPE ViPipe)
{
    if (ViPipe < 0 || ViPipe >= VI_MAX_PIPE_NUM) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "ViPipe %d out of range!\n", ViPipe);
        return CVI_FAILURE;
    }

    if (g_imx708_fd[ViPipe] >= 0) {
        close(g_imx708_fd[ViPipe]);
        g_imx708_fd[ViPipe] = -1;
        CVI_TRACE_SNS(CVI_DBG_INFO, "I2C for ViPipe %d closed.\n", ViPipe);
        return CVI_SUCCESS;
    }
    CVI_TRACE_SNS(CVI_DBG_WARN, "I2C for ViPipe %d not initialized or already closed.\n", ViPipe);
    return CVI_FAILURE;
}

/**
 * @brief Read a value from a sensor register via I2C.
 *
 * @param ViPipe The video input pipe ID.
 * @param addr The 16-bit register address.
 * @return int The 8-bit register value, or -1 on failure.
 */
int imx708_read_register(VI_PIPE ViPipe, int addr)
{
    int ret, data;
    CVI_U8 buf[2]; // Max 2 bytes for address
    CVI_U8 read_buf[1]; // Max 1 byte for data (IMX708_DATA_BYTE)

    if (ViPipe < 0 || ViPipe >= VI_MAX_PIPE_NUM || g_imx708_fd[ViPipe] < 0) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "I2C not initialized for ViPipe %d or invalid ViPipe.\n", ViPipe);
        return -1;
    }

    if (imx708_addr_byte == 2) {
        buf[0] = (addr >> 8) & 0xFF;
        buf[1] = addr & 0xFF;
    } else if (imx708_addr_byte == 1) {
        buf[0] = addr & 0xFF;
    } else {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Unsupported imx708_addr_byte: %d\n", imx708_addr_byte);
        return -1;
    }

    ret = write(g_imx708_fd[ViPipe], buf, imx708_addr_byte);
    if (ret != imx708_addr_byte) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_WRITE address 0x%04X error (%d, expected %d, ret %d)!\n", addr, errno, imx708_addr_byte, ret);
        return -1;
    }

    ret = read(g_imx708_fd[ViPipe], read_buf, imx708_data_byte);
    if (ret != imx708_data_byte) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_READ data from 0x%04X error (%d, expected %d, ret %d)!\n", addr, errno, imx708_data_byte, ret);
        return -1;
    }

    data = read_buf[0];
    // syslog(LOG_DEBUG, "i2c r 0x%04x = 0x%02x\n", addr, data); // Too verbose for general use
    return data;
}

/**
 * @brief Write a value to a sensor register via I2C.
 *
 * @param ViPipe The video input pipe ID.
 * @param addr The 16-bit register address.
 * @param data The 8-bit data to write.
 * @return int CVI_SUCCESS on success, CVI_FAILURE otherwise.
 */
int imx708_write_register(VI_PIPE ViPipe, int addr, int data)
{
    CVI_U8 buf[3]; // Max 2 bytes for address + 1 byte for data
    int ret;
    CVI_U8 idx = 0;

    if (ViPipe < 0 || ViPipe >= VI_MAX_PIPE_NUM || g_imx708_fd[ViPipe] < 0) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "I2C not initialized for ViPipe %d or invalid ViPipe.\n", ViPipe);
        return CVI_FAILURE;
    }

    if (imx708_addr_byte == 2) {
        buf[idx++] = (addr >> 8) & 0xFF;
        buf[idx++] = addr & 0xFF;
    } else if (imx708_addr_byte == 1) {
        buf[idx++] = addr & 0xFF;
    } else {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Unsupported imx708_addr_byte: %d\n", imx708_addr_byte);
        return CVI_FAILURE;
    }

    if (imx708_data_byte == 1) {
        buf[idx++] = data & 0xFF;
    } else {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Unsupported imx708_data_byte: %d\n", imx708_data_byte);
        return CVI_FAILURE;
    }

    ret = write(g_imx708_fd[ViPipe], buf, imx708_addr_byte + imx708_data_byte);
    if (ret != (imx708_addr_byte + imx708_data_byte)) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_WRITE 0x%02X to 0x%04X error (%d, expected %d, ret %d)!\n",
                      data, addr, errno, imx708_addr_byte + imx708_data_byte, ret);
        return CVI_FAILURE;
    }
    // syslog(LOG_DEBUG, "i2c w 0x%04x 0x%02x\n", addr, data); // Too verbose
    return CVI_SUCCESS;
}

/**
 * @brief Write an array of register values.
 *
 * @param ViPipe The video input pipe ID.
 * @param reg_list Pointer to the array of register_addr/value pairs.
 * @param num_regs Number of registers in the array.
 * @return int CVI_SUCCESS on success, CVI_FAILURE otherwise.
 */
int imx708_write_register_array(VI_PIPE ViPipe, const struct _sensor_reg_s *reg_list, CVI_U32 num_regs)
{
    CVI_U32 i;
    int ret;

    if (reg_list == NULL) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Register list is NULL.\n");
        return CVI_FAILURE;
    }

    for (i = 0; i < num_regs; i++) {
        ret = imx708_write_register(ViPipe, reg_list[i].reg_addr, reg_list[i].reg_val);
        if (ret != CVI_SUCCESS) {
            CVI_TRACE_SNS(CVI_DBG_ERR, "Failed to write reg 0x%04X with val 0x%02X.\n",
                          reg_list[i].reg_addr, reg_list[i].reg_val);
            return CVI_FAILURE;
        }
    }
    return CVI_SUCCESS;
}

static void delay_ms(int ms)
{
    usleep(ms * 1000);
}

/**
 * @brief Put the IMX708 sensor into standby mode.
 *
 * @param ViPipe The video input pipe ID.
 */
void imx708_standby(VI_PIPE ViPipe)
{
    // Refer to RPi driver: IMX708_REG_MODE_SELECT (0x0100), IMX708_MODE_STANDBY (0x00)
    imx708_write_register(ViPipe, 0x0100, 0x00);
    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 standby mode set for ViPipe %d.\n", ViPipe);
}

/**
 * @brief Restart the IMX708 sensor (exit standby, start streaming).
 *
 * @param ViPipe The video input pipe ID.
 */
void imx708_restart(VI_PIPE ViPipe)
{
    // Refer to RPi driver: IMX708_REG_MODE_SELECT (0x0100), IMX708_MODE_STREAMING (0x01)
    imx708_write_register(ViPipe, 0x0100, 0x01);
    delay_ms(20); // Add a small delay after starting stream, common practice
    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 streaming mode set for ViPipe %d.\n", ViPipe);
}

/**
 * @brief Probe the IMX708 sensor to check its presence and ID.
 *
 * @param ViPipe The video input pipe ID.
 * @return int CVI_SUCCESS if sensor is detected, CVI_FAILURE otherwise.
 */
int imx708_probe(VI_PIPE ViPipe)
{
    CVI_U16 dev_id = 0;
    int high_byte, low_byte;

    if (imx708_i2c_init(ViPipe) != CVI_SUCCESS) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "IMX708 I2C init failed for ViPipe %d.\n", ViPipe);
        return CVI_FAILURE;
    }

    // IMX708_REG_CHIP_ID (0x0016) holds 16-bit ID 0x0708
    // Assuming MSB is at 0x0016, LSB at 0x0017, or it's a 16-bit read.
    // The RPi driver reads 0x0016 (value 0x07) and 0x0017 (value 0x08).
    // Let's assume 0x0016 is high byte, 0x0017 is low byte for now.
    // This needs to be confirmed against how the RPi driver actually reads it.
    // The define IMX708_REG_CHIP_ID 0x0016 suggests it's a single register for the ID or start of it.
    // The RPi driver imx708_read_reg function handles 8-bit and 16-bit values.
    // For now, let's try reading two consecutive 8-bit registers if chip ID is 16-bit.

    high_byte = imx708_read_register(ViPipe, IMX708_CHIP_ID_REG); // Read 0x0016
    if (high_byte < 0) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Read IMX708 Chip ID high byte failed for ViPipe %d.\n", ViPipe);
        imx708_i2c_exit(ViPipe);
        return CVI_FAILURE;
    }
    low_byte = imx708_read_register(ViPipe, IMX708_CHIP_ID_REG + 1); // Read 0x0017
    if (low_byte < 0) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "Read IMX708 Chip ID low byte failed for ViPipe %d.\n", ViPipe);
        imx708_i2c_exit(ViPipe);
        return CVI_FAILURE;
    }

    dev_id = (CVI_U16)((high_byte << 8) | low_byte);

    if (dev_id == IMX708_CHIP_ID) {
        CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708 sensor detected with ID 0x%04X on ViPipe %d!\n", dev_id, ViPipe);
        // imx708_i2c_exit(ViPipe); // Keep I2C open if probe is part of init flow
        return CVI_SUCCESS;
    } else {
        CVI_TRACE_SNS(CVI_DBG_ERR, "IMX708 sensor ID mismatch! Expected 0x%04X, got 0x%04X on ViPipe %d.\n",
                      IMX708_CHIP_ID, dev_id, ViPipe);
        imx708_i2c_exit(ViPipe);
        return CVI_FAILURE;
    }
}

/**
 * @brief Initialize the IMX708 sensor for a specific mode.
 * This function will call the mode-specific init function.
 *
 * @param ViPipe The video input pipe ID.
 */
void imx708_init(VI_PIPE ViPipe)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER_VOID(pstSnsState);

    CVI_TRACE_SNS(CVI_DBG_INFO, "Starting IMX708_init for ViPipe %d, Mode: %d\n", ViPipe, pstSnsState->u8ImgMode);

    if (imx708_i2c_init(ViPipe) != CVI_SUCCESS) {
        CVI_TRACE_SNS(CVI_DBG_ERR, "IMX708 I2C init failed during main init for ViPipe %d.\n", ViPipe);
        return;
    }

    // Call the appropriate mode initialization function based on pstSnsState->u8ImgMode
    // For now, defaulting to one mode. This needs to be dynamic.
    // Example:
    switch (pstSnsState->u8ImgMode) {
    case IMX708_MODE_4608X2592P30:
        imx708_linear_4608x2592_init(ViPipe);
        break;
    // case IMX708_MODE_2304X1296P60:
        // imx708_binned_2304x1296_init(ViPipe); // Example
        // break;
    default:
        CVI_TRACE_SNS(CVI_DBG_ERR, "Unsupported mode %d for IMX708 on ViPipe %d\n", pstSnsState->u8ImgMode, ViPipe);
        return;
    }

    pstSnsState->bInit = CVI_TRUE;
    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708_init complete for ViPipe %d.\n", ViPipe);
}

/**
 * @brief Exit/Deinitialize the IMX708 sensor.
 *
 * @param ViPipe The video input pipe ID.
 */
void imx708_exit(VI_PIPE ViPipe)
{
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER_VOID(pstSnsState);

    imx708_i2c_exit(ViPipe);
    pstSnsState->bInit = CVI_FALSE;
    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708_exit complete for ViPipe %d.\n", ViPipe);
}


// --- Mode Specific Initializations ---
// These will be populated based on RPi kernel driver register lists

/**
 * @brief Initialize IMX708 for 4608x2592 @ 30fps linear mode.
 *
 * @param ViPipe The video input pipe ID.
 */
static void imx708_linear_4608x2592_init(VI_PIPE ViPipe)
{
    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708: Initializing for 4608x2592P30 mode on ViPipe %d.\n", ViPipe);

    // Placeholder: This sequence needs to be ported from the RPi imx708.c driver
    // It will involve writing many registers from `mode_common_regs` and `mode_4608x2592_regs`
    // Example structure:
    // imx708_write_register(ViPipe, 0x0100, 0x00); // Standby before major changes
    // delay_ms(5);
    // ... write common registers ...
    // ... write mode specific registers ...
    // imx708_write_register(ViPipe, 0x0100, 0x01); // Stream on
    // delay_ms(20);

    // For now, just a debug message
    CVI_TRACE_SNS(CVI_DBG_WARN, "IMX708: Placeholder for 4608x2592P30 init sequence on ViPipe %d.\n", ViPipe);

    // After writing all registers, update sensor state if necessary
    // (e.g., frame length, line length based on mode defaults)
    ISP_SNS_STATE_S *pstSnsState = CVI_NULL;
    IMX708_SENSOR_GET_CTX(ViPipe, pstSnsState);
    CMOS_CHECK_POINTER_VOID(pstSnsState);

    // Example: (These values would come from imx708_cmos_param.h for this mode)
    // pstSnsState->u32FLStd = 2649; // From RPi driver for 4608x2592 mode (FRM_LENGTH_A)
    // pstSnsState->u32FLStd = g_astImx708_mode[IMX708_MODE_4608X2592P30].u32VtsDef;

    CVI_TRACE_SNS(CVI_DBG_INFO, "IMX708: 4608x2592P30 Init Placeholder OK for ViPipe %d.\n", ViPipe);
}

// Add other mode-specific init functions here
// static void imx708_binned_2304x1296_init(VI_PIPE ViPipe) { ... }

