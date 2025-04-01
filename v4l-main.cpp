#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <cstring>
#include <opencv2/opencv.hpp>
// Custom includes
#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

// YOLO defines
constexpr const char* MODEL_FILE_PATH = "/root/detect.cvimodel";
constexpr int MODEL_CLASS_CNT = 3; 
constexpr double MODEL_THRESH = 0.5;
constexpr double MODEL_NMS_THRESH = 0.5;
constexpr int INPUT_FRAME_WIDTH = 640;
constexpr int INPUT_FRAME_HEIGHT = 640;
constexpr int CAMERA_WIDTH = 640;
constexpr int CAMERA_HEIGHT = 480;

cvitdl_handle_t tdl_handle = nullptr;

VIDEO_FRAME_INFO_S convertYUYVMatToVideoFrameInfo(const cv::Mat &yuyv)
{
    // Ensure the input is not empty and has type CV_8UC2.
    if (yuyv.empty() || yuyv.type() != CV_8UC2) {
        throw std::runtime_error("Input cv::Mat must be non-empty and of type CV_8UC2 (YUYV format).");
    }

    // Zero initialize the structure using C++11 uniform initialization.
    VIDEO_FRAME_INFO_S frameInfo{};
    
    // Set frame dimensions.
    frameInfo.stVFrame.u32Width  = static_cast<CVI_U32>(yuyv.cols);
    frameInfo.stVFrame.u32Height = static_cast<CVI_U32>(yuyv.rows);
    
    // Set pixel format and related fields.
    frameInfo.stVFrame.enPixelFormat = PIXEL_FORMAT_YUYV;
    frameInfo.stVFrame.enVideoFormat = VIDEO_FORMAT_LINEAR;
    frameInfo.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
    frameInfo.stVFrame.enDynamicRange = DYNAMIC_RANGE_SDR8;
    frameInfo.stVFrame.enColorGamut = COLOR_GAMUT_BT601;

    // Set the stride from cv::Mat's step.
    frameInfo.stVFrame.u32Stride[0] = static_cast<CVI_U32>(yuyv.step[0]);
    // Calculate the length of the data.
    frameInfo.stVFrame.u32Length[0] = static_cast<CVI_U32>(yuyv.rows * yuyv.step[0]);

    // Allocate a buffer to hold the YUYV frame data.
    unsigned char *buffer = new unsigned char[frameInfo.stVFrame.u32Length[0]];
    if (!buffer) {
        throw std::runtime_error("Memory allocation for frame buffer failed.");
    }
    
    // Copy data from the cv::Mat into the buffer.
    std::memcpy(buffer, yuyv.data, frameInfo.stVFrame.u32Length[0]);

    // Assign the buffer to the virtual address field.
    frameInfo.stVFrame.pu8VirAddr[0] = buffer;
    // For demonstration, we assume physical and virtual addresses are the same.
    frameInfo.stVFrame.u64PhyAddr[0] = reinterpret_cast<CVI_U64>(buffer);

    // Set cropping offsets to zero (no cropping).
    frameInfo.stVFrame.s16OffsetTop = 0;
    frameInfo.stVFrame.s16OffsetLeft = 0;
    frameInfo.stVFrame.s16OffsetRight = 0;
    frameInfo.stVFrame.s16OffsetBottom = 0;

    // The other planes are not used for YUYV.
    frameInfo.stVFrame.u32Stride[1] = 0;
    frameInfo.stVFrame.u32Length[1] = 0;
    frameInfo.stVFrame.pu8VirAddr[1] = nullptr;
    frameInfo.stVFrame.u64PhyAddr[1] = 0;

    frameInfo.stVFrame.u32Stride[2] = 0;
    frameInfo.stVFrame.u32Length[2] = 0;
    frameInfo.stVFrame.pu8VirAddr[2] = nullptr;
    frameInfo.stVFrame.u64PhyAddr[2] = 0;
    
    // Set time reference, presentation timestamp, and pool ID to default values.
    frameInfo.stVFrame.u32TimeRef = 0;
    frameInfo.stVFrame.u64PTS = 0;
    frameInfo.stVFrame.pPrivateData = nullptr;
    frameInfo.stVFrame.u32FrameFlag = 0;
    frameInfo.u32PoolId = 0;  // Adjust as necessary.

    return frameInfo;
}

class V4L2Camera {
public:
    V4L2Camera(const std::string& device) : device_path(device), fd(-1), buffer(nullptr) {}
    ~V4L2Camera() { stop(); }

    bool start() {
        fd = open(device_path.c_str(), O_RDWR);
        if (fd < 0) {
            printf("Failed to open video device\n");
            return false;
        }
        struct v4l2_format format = {};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = CAMERA_WIDTH;
        format.fmt.pix.height = CAMERA_HEIGHT;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            printf("Failed to set format\n");
            return false;
        }
        struct v4l2_requestbuffers req = {};
        req.count = 1;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
            printf("Failed to request buffer\n");
            return false;
        }
        struct v4l2_buffer buffer_info = {};
        buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer_info.memory = V4L2_MEMORY_MMAP;
        buffer_info.index = 0;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buffer_info) < 0) {
            printf("Failed to query buffer\n");
            return false;
        }
        buffer = mmap(nullptr, buffer_info.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer_info.m.offset);
        if (buffer == MAP_FAILED) {
            printf("Failed to mmap buffer\n");
            return false;
        }
        if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
            printf("Failed to queue buffer\n");
            return false;
        }
        int type = buffer_info.type;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            printf("Failed to start stream\n");
            return false;
        }
        return true;
    }

    bool captureFrame() {
        // Dequeue the buffer containing the captured frame
        struct v4l2_buffer buffer_info = {};
        buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer_info.memory = V4L2_MEMORY_MMAP;
        buffer_info.index = 0;
        if (ioctl(fd, VIDIOC_DQBUF, &buffer_info) < 0) {
            std::fprintf(stderr, "Failed to dequeue buffer\n");
            return false;
        }

        std::cout << "Captured frame of size: " << buffer_info.bytesused << " bytes" << std::endl;

        // Wrap the raw buffer data in a Mat.
        // Note: Ensure that the width (640) and height (480) match your device settings.
        cv::Mat yuyv(CAMERA_HEIGHT, CAMERA_WIDTH, CV_8UC2, buffer);
        if (yuyv.empty()) {
            std::fprintf(stderr, "Captured frame is empty\n");
            // Requeue the buffer before returning if possible.
            ioctl(fd, VIDIOC_QBUF, &buffer_info);
            return false;
        }

        // Resize the image to the desired input size for YOLO detection.
        cv::Mat resized_yuyv;
        std::cout << "Resizing YUYV frame..." << std::endl;
        cv::resize(yuyv, resized_yuyv, cv::Size(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT));
        if (resized_yuyv.empty()) {
            std::fprintf(stderr, "Resized image is empty\n");
            ioctl(fd, VIDIOC_QBUF, &buffer_info);
            return false;
        }

        // Convert from YUYV to BGR format.
        // This conversion flag is correct if your data is in YUYV format.
        cv::Mat bgr;
        try {
            cv::cvtColor(resized_yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
        } catch (const cv::Exception& e) {
            std::fprintf(stderr, "Error converting color: %s\n", e.what());
            ioctl(fd, VIDIOC_QBUF, &buffer_info);
            return false;
        }

        if (bgr.empty()) {
            std::fprintf(stderr, "BGR image is empty after conversion\n");
            ioctl(fd, VIDIOC_QBUF, &buffer_info);
            return false;
        }

        // Save the converted frame to disk for verification
        std::string filename = "resized.jpg";
        if (!cv::imwrite(filename, bgr)) {
            std::fprintf(stderr, "Failed to save frame to %s\n", filename.c_str());
            ioctl(fd, VIDIOC_QBUF, &buffer_info);
            return false;
        }
        std::cout << "Saved frame to " << filename << std::endl;

        // Convert the resized YUYV image to your application's VIDEO_FRAME_INFO_S format
        VIDEO_FRAME_INFO_S frameInfo = convertYUYVMatToVideoFrameInfo(resized_yuyv);
        std::cout << "Converted to VIDEO_FRAME_INFO_S" << std::endl;

        // Perform object detection using YOLOv8
        cvtdl_object_t obj_meta = {0};
        CVI_TDL_Detection(tdl_handle, &frameInfo, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, &obj_meta);
        std::printf("Detected %d objects\n", obj_meta.size);

        // Requeue the buffer back to the capture queue
        if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
            std::fprintf(stderr, "Failed to requeue buffer\n");
            return false;
        }    
        return true;
    }

    void stop() {
        if (fd >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd, VIDIOC_STREAMOFF, &type);
            munmap(buffer, CAMERA_WIDTH * CAMERA_HEIGHT * 2);
            close(fd);
        }
    }

private:
    std::string device_path;
    int fd;
    void* buffer;
};

void initModel() {
    printf("init model\n");
    int vpssgrp_width = CAMERA_WIDTH;
    int vpssgrp_height = CAMERA_HEIGHT;
    CVI_S32 ret = MMF_INIT_HELPER2(vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1,
                                   vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1);
    if (ret != CVI_TDL_SUCCESS) { 
        throw std::runtime_error("VPSS open failed with error code: " + std::to_string(ret));
    }
    printf("vpss opened\n");
    ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Create TDL handle failed with error code: " + std::to_string(ret));
    }
    printf("tdl_handle loaded\n");
    // setup preprocess
    InputPreParam preprocess_cfg = CVI_TDL_GetPreParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    for (int i = 0; i < 3; i++) {
      preprocess_cfg.factor[i] = 0.003922;
      preprocess_cfg.mean[i] = 0.0;
    }
    preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;
    ret = CVI_TDL_SetPreParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
    if (ret != CVI_SUCCESS) {
        std::ostringstream errorMsg;
        throw std::runtime_error("Can not set yolov8 preprocess parameters" + std::to_string(ret));
    }
    printf("preprocess done\n");
    // setup yolo algorithm preprocess
    cvtdl_det_algo_param_t yolov8_param = CVI_TDL_GetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;
    ret = CVI_TDL_SetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS) {
      throw std::runtime_error("Can not set yolov8 algorithm parameters" + std::to_string(ret));
    }
    printf("algo setted\n");
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_FILE_PATH);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Open model failed with error code: " + std::to_string(ret));
    }
    printf("model loaded\n");
}

int main() {
    try {
        
        V4L2Camera camera("/dev/video0");
        if (!camera.start()) {
            return -1;
        }
        printf("camera started\n");
        // for (int i = 0; i < 30; ++i) {
        //     if (!camera.captureFrame(i)) {
        //         break;
        //     }
        // }
        initModel();
        while(1) {
            if (!camera.captureFrame()) {
                break;
            }
        }
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "FATAL ERROR: " << ex.what() << std::endl;
        return -1;
    }
}
