#ifndef DETECT_H
#define DETECT_H

#include <opencv2/core.hpp>

#ifdef __cplusplus
extern "C" {
#endif

bool run_detection(cv::Mat image, const char* model_path);

#ifdef __cplusplus
}
#endif

#endif // DETECT_H
