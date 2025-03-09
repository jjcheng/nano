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
#include "core/cvi_tdl_types_mem.h" 
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

#define BUFFER_SIZE 4096
#define WIFI_CONFIG_FILE_PATH "/root/wifi_config"
#define SAVE_IMAGE_PATH "/root/captured.jpg"
#define CONF_THRESHOLD 0.5
#define IOU_THRESHOLD 0.5
#define NO_CHANGE_FRAME_LIMIT 30
#define CHANGE_THRESHOLD_PERCENT 0.10

// YOLO defines
#define MODEL_SCALE 0.0039216
#define MODEL_MEAN 0.0
#define MODEL_CLASS_CNT 4
#define MODEL_THRESH 0.5
#define MODEL_NMS_THRESH 0.5
#define MODEL_FILE_PATH "/root/models/model.cvimodel"
#define BLUE_MAT cv::Scalar(255, 0, 0)
#define RED_MAT cv::Scalar(0, 0, 255)
cvitdl_handle_t tdl_handle = NULL;

int main() {
    CVI_S32 ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        printf("Create tdl handle failed with %#x!\n", ret);
        return false;
    }
    // Setup preprocessing parameters.
    YoloPreParam preprocess_cfg = CVI_TDL_Get_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    for (int i = 0; i < 3; i++) {
        printf("assign val %d \n", i);
        preprocess_cfg.factor[i] = MODEL_SCALE;
        preprocess_cfg.mean[i] = MODEL_MEAN;
    }
    preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;
    printf("Setting YOLOv8 preprocess parameters\n");
    ret = CVI_TDL_Set_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
    if (ret != CVI_SUCCESS) {
        printf("Cannot set YOLOv8 preprocess parameters %#x\n", ret);
        return false;
    }
    // Setup algorithm parameters.
    YoloAlgParam yolov8_param = CVI_TDL_Get_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;
    printf("Setting YOLOv8 algorithm parameters\n");
    ret = CVI_TDL_Set_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS) {
        printf("Cannot set YOLOv8 algorithm parameters %#x\n", ret);
        return false;
    }
    // Set detection thresholds.
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);
    printf("YOLOv8 parameters setup success!\n");
    // Open the model.
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_FILE_PATH);
    if (ret != CVI_SUCCESS) {
        printf("Open model failed with %#x!\n", ret);
        return 0;
    }
}
