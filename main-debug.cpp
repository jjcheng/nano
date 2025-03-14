#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <functional>
#include <map>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>       // For memset
#include <sys/ioctl.h>   // For ioctl
#include <net/if.h>      // For ifreq
#include <arpa/inet.h>   // For inet_ntoa
#include <unistd.h>      // For close
#include <curl/curl.h>
// #include <opencv2/core.hpp>
// #include <opencv2/highgui.hpp>
// #include <opencv2/imgproc.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
// custom includes
#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

#define MODEL_CLASS_CNT 3
#define INPUT_WIDTH 640
#define INPUT_HEIGHT 640
#define MODEL_THRESH 0.4
#define MODEL_NMS_THRESH 0.4
#define MODEL_FILE_PATH "/root/detect.cvimodel"
#define TEST_IMAGE_PATH "/root/test.jpg"


int main() {
    cvitdl_handle_t tdl_handle = NULL;
    // int vpssgrp_width = INPUT_WIDTH;
    // int vpssgrp_height = INPUT_HEIGHT;
    // CVI_S32 ret = MMF_INIT_HELPER2(vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1,
    //                                vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1);
    // if (ret != CVI_TDL_SUCCESS) {
    //     printf("Init sys failed with %#x!\n", ret);
    //     return -1;
    // }
    CVI_S32 ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        //printf("Create tdl handle failed with %#x!\n", ret);
        return -1;
    }
    // preprocess
    YoloPreParam preprocess_cfg = CVI_TDL_Get_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    for (int i = 0; i < 3; i++) {
        preprocess_cfg.factor[i] = 0.0039216;
        preprocess_cfg.mean[i] = 0;
    }
    preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;
    //printf("setup yolov8 param \n");
    ret = CVI_TDL_Set_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
    if (ret != CVI_SUCCESS) {
        //printf("Can not set yolov8 preprocess parameters %#x\n", ret);
        return -1;
    }
    // setup yolo algorithm preprocess
    YoloAlgParam yolov8_param = CVI_TDL_Get_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;
    //printf("setup yolov8 algorithm param \n");
    ret = CVI_TDL_Set_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS) {
       //printf("Can not set yolov8 algorithm parameters %#x\n", ret);
        return -1;
    }
    // set theshold
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);
    //printf("yolov8 algorithm parameters setup success!\n");
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_FILE_PATH);
    if (ret != CVI_SUCCESS) {
        printf("open model failed with %#x!\n", ret);
        return -1;
    }
    //printf("model opened successfully!\n");
    // start camera
    cv::VideoCapture cap;
    cap.set(cv::CAP_PROP_FRAME_WIDTH, INPUT_WIDTH);
	cap.set(cv::CAP_PROP_FRAME_HEIGHT, INPUT_HEIGHT);
    cap.open(0);
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open camera" << std::endl;
        return -1;
    }
    cv::Mat frame;
    // warm up the camera
    for (int i = 0; i < 5; i++) cap >> frame;
    // Capture a new frame
    cap >> frame;
    if (frame.empty()) {
        std::cerr << "Error: Captured empty frame" << std::endl;
        return -1;
    }
    // Convert the frame to a vector of uchar
    std::vector<uchar> buffer;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 90}; // JPEG quality
    //get frame buffer
    if (!cv::imencode(".jpg", frame, buffer, params)) {
        std::cerr << "Error: Failed to encode image" << std::endl;
        return -1;
    }
    std::vector<uchar> imgData;
    //std::string imagePath = TEST_IMAGE_PATH;
    //cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (!cv::imencode(".jpg", frame, imgData)) {
        std::cerr << "Error: Could not encode image!" << std::endl;
        return -1;
    }
    //printf("converting mat to frame_ptr\n");
    VIDEO_FRAME_INFO_S* frame_ptr = reinterpret_cast<VIDEO_FRAME_INFO_S*>(frame.data);
    if(frame_ptr == NULL) {
        std::printf("failed to get frame_ptr\n");
        return -1;
    }
    cvtdl_object_t obj_meta = {0};
    //printf("detecting...\n");
    CVI_TDL_YOLOV8_Detection(tdl_handle, frame_ptr, &obj_meta);
    //printf("detection completed\n");
    // Draw bounding boxes on the image.
    // for (uint32_t i = 0; i < obj_meta.size; i++) {
    //     std::printf("detected %d\n", i);
    //     cv::Rect r(static_cast<int>(obj_meta.info[i].bbox.x1),
    //                static_cast<int>(obj_meta.info[i].bbox.y1),
    //                static_cast<int>(obj_meta.info[i].bbox.x2 - obj_meta.info[i].bbox.x1),
    //                static_cast<int>(obj_meta.info[i].bbox.y2 - obj_meta.info[i].bbox.y1));
    //     if (obj_meta.info[i].classes == 0)
    //         cv::rectangle(frame, r, BLUE_MAT, 1, 8, 0);
    //     else if (obj_meta.info[i].classes == 1)
    //         cv::rectangle(frame, r, RED_MAT, 1, 8, 0);
    // }
    if (obj_meta.size == 0) {
        printf("no detection!!!\n");
    }
    return 0;
}
