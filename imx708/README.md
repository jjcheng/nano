# IMX708 Camera Driver for LicheeRV Nano (Experimental)

## 1. Introduction

This package contains an experimental Linux kernel driver for the Sony IMX708 CSI camera module, adapted for use with the LicheeRV Nano board. It is based on the provided GC4653 driver structure and information gathered from the Raspberry Pi IMX708 kernel driver.

**IMPORTANT NOTES:**
*   This driver is **experimental and untested on actual hardware**. You will need to perform all testing and debugging on your LicheeRV Nano board with the IMX708 module connected.
*   **Autofocus functionality is highly experimental** and provided as stubs. Full autofocus implementation is complex and may require significant platform-specific integration and calibration, which is beyond the scope of this initial adaptation.
*   The driver aims to support basic image capture, resolution setting, and low power consumption features.
*   You will need a cross-compilation toolchain for your LicheeRV Nano and familiarity with building and loading kernel modules for your specific Linux distribution on the board.

## 2. Package Contents

*   `Makefile`: Used to build the driver (libsns_imx708.a and libsns_imx708.so).
*   `imx708_sensor_ctl.c`: Low-level sensor control, I2C communication, probe, init, standby/restart.
*   `imx708_cmos.c`: ISP callback functions (AE, AWB, FPS, exposure, gain), mode setting, autofocus stubs.
*   `include/imx708_cmos_ex.h`: External declarations, sensor mode enums, and structures.
*   `include/imx708_cmos_param.h`: Sensor mode parameters, default settings, MIPI RX attributes.
*   `README.md`: This file.

## 3. Prerequisites

*   LicheeRV Nano board with IMX708 CSI camera module correctly connected.
*   A working Linux build environment for the LicheeRV Nano, including the kernel headers/sources and a cross-compiler.
*   Familiarity with compiling kernel modules and using I2C tools on Linux.

## 4. Compilation

1.  **Toolchain Setup:** Ensure your cross-compilation toolchain (e.g., RISC-V GCC) is in your PATH and your environment is set up for kernel module compilation (e.g., `ARCH=riscv`, `CROSS_COMPILE=riscv64-unknown-linux-gnu-`).
2.  **Makefile Parameters:** The provided `Makefile` includes a line:
    `PARAM_FILE=../../../../../../$(shell echo $(MW_VER))/Makefile.param`
    This path is relative and specific to a particular SDK structure (likely CVITEK). You **MUST** adapt this `PARAM_FILE` path or the include mechanism to match your LicheeRV Nano SDK or build system. The `Makefile.param` typically defines variables like `CC`, `LD`, `MW_INC`, `ISP_INC`, `KERNEL_INC`, `MW_LIB`, etc.
    If your build system does not use such a `Makefile.param`, you will need to modify the `Makefile` to directly define these compiler, linker, include paths, and library output paths.
3.  **Navigate to Driver Directory:**
    `cd /path/to/imx708_driver`
4.  **Build:**
    `make clean`
    `make`

    If the build is successful, it should produce `libsns_imx708.a` and `libsns_imx708.so` in the output directory specified by `$(MW_LIB)` (or your adapted path).
    This driver seems to be built as a library to be linked with a larger ISP/sensor manager rather than a standalone `.ko` module. You will need to integrate it according to your platform's sensor driver framework.

## 5. Loading and Testing (General Guidance)

Since this driver is built as a library (`.a`, `.so`), it's likely intended to be part of a larger camera system software stack on your LicheeRV Nano, rather than a standalone kernel module loaded with `insmod`.

1.  **Integration:** You will need to integrate these compiled libraries into your LicheeRV Nano's camera ISP framework. This typically involves:
    *   Placing the `libsns_imx708.a` and `libsns_imx708.so` into the appropriate library directory in your root filesystem or SDK.
    *   Ensuring the ISP sensor manager or equivalent software is configured to recognize and use the "IMX708" sensor. This might involve modifying configuration files or platform data that lists available sensors.
    *   The driver registers itself with the name "IMX708" via the `SENSOR_IMX708_DRV_ENTRY` structure in `imx708_cmos.c`.

2.  **Initial Check (Sensor Probe):**
    *   After integrating the driver and booting your LicheeRV Nano, check the kernel log for messages related to IMX708:
        `dmesg | grep -i imx708`
    *   You should look for messages indicating successful I2C initialization and sensor probe, like:
        `IMX708 sensor detected with ID 0x0708 on ViPipe X!`
    *   If you see ID mismatch errors or I2C communication errors, double-check:
        *   IMX708 I2C address (defined as `0x1A` in `imx708_cmos_ex.h`).
        *   I2C bus number used (defined in `g_aunImx708_BusInfo` in `imx708_cmos.c`, currently set to device 4, same as GC4653 example).
        *   Physical connection of the camera module.

3.  **Basic Image Capture:**
    *   If your LicheeRV Nano platform has V4L2 (Video4Linux2) support and tools like `v4l2-ctl` or a GStreamer pipeline for camera capture, attempt to list available video devices and capture frames.
    *   Example (highly platform-dependent):
        `v4l2-ctl --list-devices`
        `v4l2-ctl -d /dev/videoX --stream-mmap --stream-count=1 --stream-to=frame.raw`
    *   The driver defines modes like 4608x2592. Your capture application should be configured to use a supported resolution.

4.  **Resolution Setting:**
    *   The driver is structured to support multiple resolutions (e.g., 4608x2592, 2304x1296). Test switching between these resolutions if your capture application allows it.

5.  **Autofocus:**
    *   The autofocus functions are currently stubs. `cmos_set_focus_abs` in `imx708_cmos.c` contains placeholder comments on potential registers.
    *   Full autofocus functionality will require significant further development and platform integration.

6.  **Low Power (Standby/Restart):**
    *   The `imx708_standby()` and `imx708_restart()` functions are implemented to write to the mode select register (0x0100). Testing this would require a way to trigger these functions and measure power consumption, which is advanced.

## 6. Debugging and Providing Feedback

*   **Kernel Logs:** The primary source of debugging information will be kernel logs. Use `dmesg` frequently.
    `dmesg -wH` (to follow logs in real-time with human-readable timestamps)
*   **I2C Tools:** If you have `i2c-tools` installed on your LicheeRV Nano, you can use `i2cdetect`, `i2cget`, and `i2cset` to manually probe the sensor and read/write registers for debugging. Be very careful with `i2cset` as incorrect writes can hang or damage the sensor.
    *   Example: `i2cdetect -y 4` (to scan I2C bus 4, assuming this is the correct bus)
    *   Example: `i2cget -y 4 0x1a 0x0016 w` (to read the 16-bit chip ID from IMX708 at address 0x1a, register 0x0016 - this assumes 0x0017 is also read for the LSB).
*   **Provide Detailed Information:** If you encounter issues, please provide:
    *   The exact steps you took.
    *   Relevant kernel log snippets (`dmesg`).
    *   Output from any commands you ran.
    *   Details about your LicheeRV Nano Linux distribution and kernel version.

## 7. Disclaimer

This driver is provided as-is, without any warranty. Use it at your own risk. The developer (AI Agent Manus) is not liable for any damage or issues arising from its use. Significant effort will be required from your side to test, debug, and potentially enhance this driver for stable operation on your specific hardware setup.

