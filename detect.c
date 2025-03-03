#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"
#include <stdio.h>

#define MODEL_SCALE 0.0039216
#define MODEL_MEAN 0.0
#define MODEL_CLASS_CNT 80
#define MODEL_THRESH 0.8
#define MODEL_NMS_THRESH 0.8

// set preprocess and algorithm param for yolov8 detection
// if use official model, no need to change param (call this function)
CVI_S32 init_param(const cvitdl_handle_t tdl_handle)
{
    // setup preprocess
    YoloPreParam preprocess_cfg =
        CVI_TDL_Get_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);

    for (int i = 0; i < 3; i++)
    {
        printf("asign val %d \n", i);
        preprocess_cfg.factor[i] = MODEL_SCALE;
        preprocess_cfg.mean[i] = MODEL_MEAN;
    }
    preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;

    printf("setup yolov8 param \n");
    CVI_S32 ret = CVI_TDL_Set_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                                            preprocess_cfg);
    if (ret != CVI_SUCCESS)
    {
        printf("Can not set yolov8 preprocess parameters %#x\n", ret);
        return ret;
    }

    // setup yolo algorithm preprocess
    YoloAlgParam yolov8_param =
        CVI_TDL_Get_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;

    printf("setup yolov8 algorithm param \n");
    ret =
        CVI_TDL_Set_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS)
    {
        printf("Can not set yolov8 algorithm parameters %#x\n", ret);
        return ret;
    }

    // set theshold
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);

    printf("yolov8 algorithm parameters setup success!\n");
    return ret;
}

bool run_detection(cv::Mat image, const char* model_path) {
    int vpssgrp_width = image.cols;
    int vpssgrp_height = image.rows;

    CVI_S32 ret = MMF_INIT_HELPER2(vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1,
                                   vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1);
    if (ret != CVI_TDL_SUCCESS) {
        printf("Init sys failed with %#x!\n", ret);
        return false;
    }

    cvitdl_handle_t tdl_handle = NULL;
    ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        printf("Create tdl handle failed with %#x!\n", ret);
        return false;
    }

    // Initialize YOLOv8 parameters
    ret = init_param(tdl_handle);
    if (ret != CVI_SUCCESS) {
        printf("Failed to initialize YOLOv8 parameters\n");
        return false;
    }

    // Load the YOLOv8 model
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, model_path);
    if (ret != CVI_SUCCESS) {
        printf("Open model failed with %#x!\n", ret);
        return false;
    }

    imgprocess_t img_handle;
    CVI_TDL_Create_ImageProcessor(&img_handle);

    // Convert cv::Mat to VIDEO_FRAME_INFO_S
    VIDEO_FRAME_INFO_S bg;
    memset(&bg, 0, sizeof(VIDEO_FRAME_INFO_S));
    bg.stVFrame.u32Width = image.cols;
    bg.stVFrame.u32Height = image.rows;
    bg.stVFrame.enPixelFormat = PIXEL_FORMAT_RGB_888_PLANAR;  // Ensure correct format

    // Copy image data from cv::Mat to VIDEO_FRAME_INFO_S
    memcpy(bg.stVFrame.pu8VirAddr, image.data, image.total() * image.elemSize());

    printf("Image width: %d, height: %d\n", bg.stVFrame.u32Width, bg.stVFrame.u32Height);

    // Run detection
    cvtdl_object_t obj_meta = {0};
    CVI_TDL_YOLOV8_Detection(tdl_handle, &bg, &obj_meta);

    printf("\n\n----------\nDetected objects count: %d\n\n", obj_meta.size);

    for (uint32_t i = 0; i < obj_meta.size; i++) {
        printf("x1 = %lf, y1 = %lf, x2 = %lf, y2 = %lf, cls: %d, score: %lf\n", 
               obj_meta.info[i].bbox.x1, obj_meta.info[i].bbox.y1, 
               obj_meta.info[i].bbox.x2, obj_meta.info[i].bbox.y2, 
               obj_meta.info[i].classes, obj_meta.info[i].bbox.score);
    }

    // Cleanup
    CVI_TDL_DestroyHandle(tdl_handle);
    CVI_TDL_Destroy_ImageProcessor(img_handle);

    return obj_meta.size > 0;
}
