#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <cstring>
#include <opencv2/opencv.hpp>
#include <sstream>

// Custom includes
#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

constexpr const char* DEVICE = "/dev/video0";

// YOLO defines
constexpr const char* MODEL_FILE_PATH = "/root/detect.cvimodel";
constexpr const int MODEL_CLASS_CNT = 3; 
constexpr const double MODEL_THRESH = 0.5;
constexpr const double MODEL_NMS_THRESH = 0.5;
constexpr const int MODEL_INPUT_WIDTH = 320;
constexpr const int MODEL_INPUT_HEIGHT = 320;
constexpr const int MIN_FRAME_WIDTH = 640;
constexpr const int MIN_FRAME_HEIGHT = 480;
constexpr const int MAX_FRAME_WIDTH = 2592;
constexpr const int MAX_FRAME_HEIGHT = 1944;

cvitdl_handle_t tdl_handle = nullptr;
V4L2Camera camera(DEVICE);

// Convert a BGR cv::Mat to VIDEO_FRAME_INFO_S.
VIDEO_FRAME_INFO_S convertBGRMatToVideoFrameInfo(const cv::Mat &bgr) {
    // Ensure the input is not empty and has type CV_8UC3.
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        throw std::runtime_error("Input cv::Mat must be non-empty and of type CV_8UC3 (BGR format).");
    }
    VIDEO_FRAME_INFO_S frameInfo{};  // Zero initialize the structure.
    
    // Set frame dimensions.
    frameInfo.stVFrame.u32Width  = static_cast<CVI_U32>(bgr.cols);
    frameInfo.stVFrame.u32Height = static_cast<CVI_U32>(bgr.rows);
    
    // Set pixel format and related fields.
    // Here, we assume that the detection algorithm expects data in RGB.
    // If needed, you can convert BGR to RGB in the buffer.
    frameInfo.stVFrame.enPixelFormat = PIXEL_FORMAT_RGB_888;
    frameInfo.stVFrame.enVideoFormat = VIDEO_FORMAT_LINEAR;
    frameInfo.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
    frameInfo.stVFrame.enDynamicRange = DYNAMIC_RANGE_SDR8;
    frameInfo.stVFrame.enColorGamut = COLOR_GAMUT_BT601;
    
    // Set the stride and length.
    frameInfo.stVFrame.u32Stride[0] = static_cast<CVI_U32>(bgr.step[0]);
    frameInfo.stVFrame.u32Length[0] = static_cast<CVI_U32>(bgr.rows * bgr.step[0]);
    
    // Allocate a buffer to hold the frame data.
    unsigned char *buffer = new unsigned char[frameInfo.stVFrame.u32Length[0]];
    if (!buffer) {
        throw std::runtime_error("Memory allocation for frame buffer failed.");
    }
    
    // Copy data from the cv::Mat into the buffer.
    std::memcpy(buffer, bgr.data, frameInfo.stVFrame.u32Length[0]);
    
    // Assign the buffer to the virtual address field.
    frameInfo.stVFrame.pu8VirAddr[0] = buffer;
    // For demonstration, we assume physical and virtual addresses are the same.
    frameInfo.stVFrame.u64PhyAddr[0] = reinterpret_cast<CVI_U64>(buffer);
    
    // Set cropping offsets to zero (no cropping).
    frameInfo.stVFrame.s16OffsetTop = 0;
    frameInfo.stVFrame.s16OffsetLeft = 0;
    frameInfo.stVFrame.s16OffsetRight = 0;
    frameInfo.stVFrame.s16OffsetBottom = 0;
    
    // Clear the other planes (not used for a 3-channel image).
    frameInfo.stVFrame.u32Stride[1] = 0;
    frameInfo.stVFrame.u32Length[1] = 0;
    frameInfo.stVFrame.pu8VirAddr[1] = nullptr;
    frameInfo.stVFrame.u64PhyAddr[1] = 0;
    
    frameInfo.stVFrame.u32Stride[2] = 0;
    frameInfo.stVFrame.u32Length[2] = 0;
    frameInfo.stVFrame.pu8VirAddr[2] = nullptr;
    frameInfo.stVFrame.u64PhyAddr[2] = 0;
    
    // Set time reference, presentation timestamp, and pool ID.
    frameInfo.stVFrame.u32TimeRef = 0;
    frameInfo.stVFrame.u64PTS = 0;
    frameInfo.stVFrame.pPrivateData = nullptr;
    frameInfo.stVFrame.u32FrameFlag = 0;
    frameInfo.u32PoolId = 0;  // Adjust as necessary.
    
    return frameInfo;
}

class V4L2Camera {
public:
   explicit V4L2Camera(const std::string& device)
        : device_path(device), fd(-1) {}

    ~V4L2Camera() { stop(); }

    void start() {
        // Use device_path provided by the constructor.
        fd = open(device_path.c_str(), O_RDWR);
        if (fd < 0) {
            throw std::runtime_error("Failed to start camera");
        }
        // Set video format.
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = MIN_FRAME_WIDTH;
        format.fmt.pix.height = MIN_FRAME_HEIGHT;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            throw std::runtime_error("Failed to set camera format");
        }
        // Request buffers
        struct v4l2_requestbuffers req = {};
        req.count = 15; // Request 15 buffers
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
            close(fd);
            throw std::runtime_error("Failed to request buffers");
        }
        // Map and queue the buffers
        for (int i = 0; i < req.count; ++i) {
            struct v4l2_buffer buffer = {};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = i;
            //query buffer
            if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
                close(fd);
                throw std::runtime_error("Failed to query buffer");
            }
            //map buffer
            void* buffer_start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer.m.offset);
            if (buffer_start == MAP_FAILED) {
                close(fd);
                throw std::runtime_error("Failed to map buffer");
            }
            //queue buffer
            if (ioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
                close(fd);
                throw std::runtime_error("Failed to queue buffer");
            }
        }
        // Start streaming
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            close(fd);
            throw std::runtime_error("Failed to start streaming");
        }
        printf("Camera started successfully\n");
    }

    cv::Mat capture() {
        // Dequeue a filled buffer.
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            throw std::runtime_error("Failed to dequeue buffer");
        }
        // Use the mapped pointer corresponding to the dequeued buffer.
        // Here we assume the captured image has dimensions MIN_FRAME_WIDTH x MIN_FRAME_HEIGHT.
        // Adjust these if needed.
        cv::Mat yuyv(MIN_FRAME_HEIGHT, MIN_FRAME_WIDTH, CV_8UC2, buffers[buf.index]);
        if (yuyv.empty()) {
            std::fprintf(stderr, "Captured frame is empty\n");
            // Requeue the buffer before returning.
            ioctl(fd, VIDIOC_QBUF, &buf);
            throw std::runtime_error("Captured frame is empty");
        }
        // Convert from YUYV to BGR format.
        cv::Mat bgr;
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
        if (bgr.empty()) {
            ioctl(fd, VIDIOC_QBUF, &buf);
            throw std::runtime_error("BGR image is empty after conversion");
        }
        #ifdef DEBUG
        // Save the converted frame to disk for verification.
        std::string filename = "bgr.jpg";
        if (!cv::imwrite(filename, bgr)) {
            ioctl(fd, VIDIOC_QBUF, &buf);
            return false;
        }
        std::cout << "Saved frame to " << filename << std::endl;
        #endif
        // Requeue the buffer.
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            throw std::runtime_error("Failed to requeue buffer");
        }
        return bgr;
    }

    void setResolution(int width, int height, const char *pixelFormat) {
        struct v4l2_format format;
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = width;
        format.fmt.pix.height = height;
        format.fmt.pix.pixelformat = v4l2_fourcc(pixelFormat[0], pixelFormat[1], pixelFormat[2], pixelFormat[3]);
        format.fmt.pix.field = V4L2_FIELD_INTERLACED;
        //call ioctl
        if (ioctl(fd, VIDIOC_S_FMT, &format) == -1) {
            perror("Failed to set resolution");
            throw std::runtime_error("Failed to set resolution");
        }
    }

    void stop() {
        if (fd >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd, VIDIOC_STREAMOFF, &type);
            // Unmap all buffers.
            for (size_t i = 0; i < buffers.size(); ++i) {
                if (buffers[i] && buffers[i] != MAP_FAILED) {
                    munmap(buffers[i], buffer_lengths[i]);
                }
            }
            close(fd);
            fd = -1;
        }
    }

private:
    std::string device_path;
    int fd;
    std::vector<void*> buffers;         // Store pointers for each mapped buffer.
    std::vector<size_t> buffer_lengths;   // Store corresponding buffer lengths.
};

void initModel() {
    printf("init model\n");
    int vpssgrp_width = MAX_FRAME_WIDTH;
    int vpssgrp_height = MAX_FRAME_HEIGHT;
    CVI_S32 ret = MMF_INIT_HELPER2(vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1,
                                   vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1);
    if (ret != CVI_TDL_SUCCESS) { 
        throw std::runtime_error("VPSS open failed with error code: " + std::to_string(ret));
    }
    ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Create TDL handle failed with error code: " + std::to_string(ret));
    }
    // Setup preprocess.
    InputPreParam preprocess_cfg = CVI_TDL_GetPreParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    for (int i = 0; i < 3; i++) {
        preprocess_cfg.factor[i] = 0.003922;
        preprocess_cfg.mean[i] = 0.0;
    }
    preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;
    ret = CVI_TDL_SetPreParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Cannot set yolov8 preprocess parameters, error: " + std::to_string(ret));
    }
    cvtdl_det_algo_param_t yolov8_param = CVI_TDL_GetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;
    ret = CVI_TDL_SetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Cannot set yolov8 algorithm parameters, error: " + std::to_string(ret));
    }
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_FILE_PATH);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Open model failed with error code: " + std::to_string(ret));
    }
    printf("model loaded\n");
}

void sendImage() {
    camera.setResolution(MAX_FRAME_WIDTH, MAX_FRAME_HEIGHT, "BA10");
    cv::Mat frame = camera.capture();
    camera.setResolution(MIN_FRAME_WIDTH, MIN_FRAME_HEIGHT, "YUYV");
}

void loop() {
    while(1) {
        cv::Mat bgr = camera.capture();
        cv::Mat resized;
        cv::resize(bgr, resized, cv::Size(MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT));
        VIDEO_FRAME_INFO_S frameInfo = convertBGRMatToVideoFrameInfo(resized);
        cvtdl_object_t obj_meta = {0};
        CVI_TDL_Detection(tdl_handle, frameInfo, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, &obj_meta);
        std::printf("Detected %d objects\n", obj_meta.size);
        if (obj_meta.size > 0){ 
            sendImage();
        }
    }
}

int main() {
    try {
        camera.start();
        initModel();
        loop();
    }
    catch (const std::exception& ex) {
        camera.stop();
        std::cerr << "FATAL ERROR: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
