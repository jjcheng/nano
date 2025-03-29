#include <linux/videodev2.h>
#include <opencv2/opencv.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <chrono>

#define DEBUG
#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 480
#define DEVICE "/dev/video0"

// Custom includes
#include "cvi_tdl.h"

// YOLO defines
constexpr const char* MODEL_FILE_PATH = "/root/detect.cvimodel";
constexpr int MODEL_CLASS_CNT = 3;  // underline, highlight, pen
constexpr double MODEL_THRESH = 0.5;
constexpr double MODEL_NMS_THRESH = 0.5;
constexpr int INPUT_FRAME_WIDTH = 640;
constexpr int INPUT_FRAME_HEIGHT = 640;

cvitdl_handle_t tdl_handle = nullptr;

struct Buffer
{
	void *start;
	size_t length;
};
#define CLEAR(x) memset(&(x), 0, sizeof(x))

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

void initModel() {
    CVI_S32 ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Create TDL handle failed with error code: " + std::to_string(ret));
    }
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
    // setup yolo algorithm preprocess
    cvtdl_det_algo_param_t yolov8_param = CVI_TDL_GetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;
    ret = CVI_TDL_SetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS) {
      throw std::runtime_error("Can not set yolov8 algorithm parameters" + std::to_string(ret));
    }
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_FILE_PATH);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Open model failed with error code: " + std::to_string(ret));
    }
}

int loop(){
	printf("loop");
	const char *device = DEVICE;
	printf(device);
	int fd = open(device, O_RDWR);
	if (fd == -1) {
		perror("Opening video device");
		return -1;
	}
	// Query device capabilities
	struct v4l2_capability cap;
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
		perror("Querying Capabilities");
		close(fd);
		return -1;
	}
#ifdef DEBUG
	printf("Driver: %s\nCard: %s\nVersion: %d.%d.%d", cap.driver, cap.card,
		   ((cap.version >> 16) & 0xFF), ((cap.version >> 8) & 0xFF), (cap.version & 0xFF));
#endif

	struct v4l2_format fmt;
	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = IMAGE_WIDTH;   // Image width
	fmt.fmt.pix.height = IMAGE_HEIGHT; // Image height
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

	if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
		perror("Setting Pixel Format");
		close(fd);
		return -1;
	}

	struct v4l2_requestbuffers req;
	CLEAR(req);
	req.count = 1;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
		perror("Requesting Buffer");
		close(fd);
		return -1;
	}
	// Query buffer to map memory
	struct v4l2_buffer buf;
	CLEAR(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
		perror("Querying Buffer");
		close(fd);
		return -1;
	}
	Buffer buffer;
	buffer.length = buf.length;
	buffer.start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
	if (buffer.start == MAP_FAILED) {
		perror("Mapping Buffer");
		close(fd);
		return -1;
	}
	// Start streaming
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
		perror("Starting Stream");
		close(fd);
		return -1;
	}
	// Capture loop
	cv::Mat yuyv(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC2, buffer.start);
	// cv::Mat bgr;
	// auto last = std::chrono::steady_clock::now();
	// auto curr = std::chrono::steady_clock::now();
	while (true) {
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
			perror("Queue Buffer");
			break;
		}
		if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
			perror("Dequeue Buffer");
			break;
		}
		//cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
		//curr = std::chrono::steady_clock::now();
		//printf("Frame latency: %lf\n", std::chrono::duration<double>(curr - last).count());
		//last = curr;
		// cv::imwrite("res.jpg", bgr);
		//resize to 640 640
		cv::Mat resized_image;
		cv::resize(yuyv, resized_image, cv::Size(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT));
		//convert yuyv to VIDEO_FRAME_INFO_S
		VIDEO_FRAME_INFO_S frameInfo = convertYUYVMatToVideoFrameInfo(resized_image);
		cvtdl_object_t obj_meta = {0};
		CVI_TDL_Detection(tdl_handle, &frameInfo, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, &obj_meta);
		//check for detections
		std::printf("Detected %d objects\n", obj_meta.size);
	}
	return 0;
}

int main()
{
	initModel();
	return loop();
}