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

#define DEVICE_DEFAULT "/dev/video0"

// YOLO defines
constexpr const char* MODEL_FILE_PATH = "/root/detect.cvimodel";
constexpr int MODEL_CLASS_CNT = 3; 
constexpr double MODEL_THRESH = 0.5;
constexpr double MODEL_NMS_THRESH = 0.5;
constexpr int MODEL_INPUT_WIDTH = 320;
constexpr int MODEL_INPUT_HEIGHT = 320;
constexpr int MIN_FRAME_WIDTH = 640;
constexpr int MIN_FRAME_HEIGHT = 480;
constexpr int MAX_FRAME_WIDTH = 2592;
constexpr int MAX_FRAME_HEIGHT = 1944;

cvitdl_handle_t tdl_handle = nullptr;
int FD = -1;

// Renamed function: now converts a BGR cv::Mat to VIDEO_FRAME_INFO_S.
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
        : device_path(device), fd(-1), buffer(nullptr), buffer_length(0) {}
    
    ~V4L2Camera() { stop(); }

    bool start() {
        // Use device_path provided by the constructor.
        fd = open(device_path.c_str(), O_RDWR);
        if (fd < 0) {
            std::cerr << "Failed to open video device " << device_path << std::endl;
            return false;
        }
        // Set video format.
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = MIN_FRAME_WIDTH;
        format.fmt.pix.height = MIN_FRAME_HEIGHT;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            std::cerr << "Failed to set format" << std::endl;
            return false;
        }
        // Request a buffer.
        v4l2_requestbuffers req{};
        req.count = 1;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
            std::cerr << "Failed to request buffer" << std::endl;
            return false;
        }
        // Query the buffer.
        v4l2_buffer buffer_info{};
        buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer_info.memory = V4L2_MEMORY_MMAP;
        buffer_info.index = 0;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buffer_info) < 0) {
            std::cerr << "Failed to query buffer" << std::endl;
            return false;
        }
        buffer_length = buffer_info.length;
        buffer = mmap(nullptr, buffer_info.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer_info.m.offset);
        if (buffer == MAP_FAILED) {
            std::cerr << "Failed to mmap buffer" << std::endl;
            return false;
        }
        // Queue the buffer.
        if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
            std::cerr << "Failed to queue buffer" << std::endl;
            return false;
        }
        // Start streaming.
        int type = buffer_info.type;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            std::cerr << "Failed to start stream" << std::endl;
            return false;
        }
        printf("camera started\n");
        return true;
    }

    bool captureFrame() {
        // Dequeue the buffer containing the captured frame.
        struct v4l2_buffer buffer_info = {};
        for (int i = 0; i < 3; ++i) {
            buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer_info.memory = V4L2_MEMORY_MMAP;
            buffer_info.index = i;
            if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
                std::fprintf(stderr, "Failed to queue buffer %d\n", i);
                return false;
            }
        }

        std::cout << "Captured frame of size: " << buffer_info.bytesused << " bytes" << std::endl;
        // Construct cv::Mat from the raw buffer.
        // Note: The image is YUYV format, so rows=height, cols=width.
        cv::Mat yuyv(MIN_FRAME_HEIGHT, MIN_FRAME_WIDTH, CV_8UC2, buffer);
        if (yuyv.empty()) {
            std::fprintf(stderr, "Captured frame is empty\n");
            ioctl(fd, VIDIOC_QBUF, &buffer_info);  // Requeue the buffer.
            return false;
        }

        // Convert from YUYV to BGR format.
        cv::Mat bgr;
        try {
            cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
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

        // Save the converted frame to disk for verification.
        std::string filename = "bgr.jpg";
        if (!cv::imwrite(filename, bgr)) {
            std::fprintf(stderr, "Failed to save frame to %s\n", filename.c_str());
            ioctl(fd, VIDIOC_QBUF, &buffer_info);
            return false;
        }
        std::cout << "Saved frame to " << filename << std::endl;

        // Convert the BGR image to VIDEO_FRAME_INFO_S format.
        VIDEO_FRAME_INFO_S frameInfo = convertBGRMatToVideoFrameInfo(bgr);
        std::cout << "Converted to VIDEO_FRAME_INFO_S" << std::endl;

        // Perform object detection using YOLOv8.
        cvtdl_object_t obj_meta = {0};
        CVI_TDL_Detection(tdl_handle, &frameInfo, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, &obj_meta);
        std::printf("Detected %d objects\n", obj_meta.size);

        // Requeue the buffer.
        // if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
        //     std::fprintf(stderr, "Failed to requeue buffer\n");
        //     return false;
        // }
        if (ioctl(fd, VIDIOC_DQBUF, &buffer_info) < 0) {
            std::fprintf(stderr, "Failed to dequeue buffer\n");
            return false;
        }

        return true;
    }

    void stop() {
        if (fd >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd, VIDIOC_STREAMOFF, &type);
            if (buffer && buffer != MAP_FAILED) {
                munmap(buffer, buffer_length);
            }
            close(fd);
            fd = -1;
        }
    }

private:
    std::string device_path;
    int fd;
    void* buffer;
    size_t buffer_length;
};

void initModel() {
    printf("init model\n");
    int vpssgrp_width = MIN_FRAME_WIDTH;
    int vpssgrp_height = MIN_FRAME_HEIGHT;
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

bool startCamera() {
    // Open the camera device
    int fd = open(DEVICE_DEFAULT, O_RDWR);
    if (fd < 0) {
        perror("Failed to open video device");
        return false;
    }

    // Set the video format
    struct v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = MIN_FRAME_WIDTH;
    format.fmt.pix.height = MIN_FRAME_HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; // Use YUYV format
    format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        perror("Failed to set video format");
        close(fd);
        return false;
    }

    // Request buffers
    struct v4l2_requestbuffers req = {};
    req.count = 4; // Request 4 buffers
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Failed to request buffers");
        close(fd);
        return false;
    }

    // Map and queue the buffers
    for (int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buffer = {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            perror("Failed to query buffer");
            close(fd);
            return false;
        }

        void* buffer_start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, buffer.m.offset);
        if (buffer_start == MAP_FAILED) {
            perror("Failed to map buffer");
            close(fd);
            return false;
        }

        if (ioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
            perror("Failed to queue buffer");
            close(fd);
            return false;
        }
    }

    // Start streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to start streaming");
        close(fd);
        return false;
    }

    printf("Camera started successfully\n");

    // Close the device after use (for demonstration purposes)
    //close(fd);
    FD = fd;
    return true;
}

bool captureFrame(int fd, void* buffer_start[], size_t buffer_length[], int buffer_count) {
    struct v4l2_buffer buffer_info = {};
    buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_info.memory = V4L2_MEMORY_MMAP;

    // Dequeue a filled buffer
    if (ioctl(fd, VIDIOC_DQBUF, &buffer_info) < 0) {
        perror("Failed to dequeue buffer");
        return false;
    }

    // Access the captured frame data
    printf("Captured frame of length: %u bytes\n", buffer_info.bytesused);

    // Save the frame to a file (optional)
    FILE* file = fopen("frame.raw", "wb");
    if (file) {
        fwrite(buffer_start[buffer_info.index], buffer_info.bytesused, 1, file);
        fclose(file);
        printf("Frame saved to 'frame.raw'\n");
    } else {
        perror("Failed to save frame");
    }

    // Requeue the buffer for further use
    if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
        perror("Failed to requeue buffer");
        return false;
    }

    return true;
}

int main() {
    try {
        // V4L2Camera camera(DEVICE_DEFAULT);
        // if (!camera.start()) {
        //     return -1;
        // }
        initModel();
        bool cameraStarted = startCamera();
        // printf("warming up for 30 frames\n");
        // for (int i = 0; i < 30; i++) {
        //     camera.captureFrame();
        // }
        printf("start capturing\n");
        const int buffer_count = 4;
        void* buffer_start[buffer_count];
        size_t buffer_length[buffer_count];
        while (1) {
            if (!captureFrame(FD, buffer_start, buffer_length, buffer_count)) {
                fprintf(stderr, "Failed to capture frame\n");
                close(FD);
                return EXIT_FAILURE;
            }
        }
        
        // Stop streaming and cleanup
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(FD, VIDIOC_STREAMOFF, &type);
        for (int i = 0; i < buffer_count; ++i) {
            munmap(buffer_start[i], buffer_length[i]);
        }
        
        close(FD);
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "FATAL ERROR: " << ex.what() << std::endl;
        return -1;
    }
}
