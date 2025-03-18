#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
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
#include <curl/curl.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <cstdio>
#include <regex>

// custom includes
#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

#define BUFFER_SIZE 4096
#define WIFI_CONFIG_FILE_PATH "/root/wifi_config"
#define SAVE_IMAGE_PATH "/root/captured.jpg"
#define CONF_THRESHOLD 0.5
#define IOU_THRESHOLD 0.5
#define NO_CHANGE_FRAME_LIMIT 10
#define CHANGE_THRESHOLD_PERCENT 0.10
#define INTERFACE_NAME "wlan0" //eth0

// YOLO defines
#define MODEL_FILE_PATH "/root/detect.cvimodel"
#define MODEL_CLASS_CNT 3 //underline, highlight, pen
#define MODEL_THRESH 0.1
#define MODEL_NMS_THRESH 0.1
#define INPUT_WIDTH 320
#define INPUT_HEIGHT 320
//#define NO_WIFI

// Global variables
// std::string wifiSSID = "";
// std::string wifiPassword = "";
std::string remoteBaseUrl = "";
std::string myIp = "";
cv::VideoCapture cap;
cv::QRCodeDetector qrDecoder;
cvitdl_handle_t tdl_handle = NULL;

//Use sig_atomic_t for safe signal handling
volatile sig_atomic_t interrupted = 0;

struct HttpResponse {
    std::string body;       // Response body
    long statusCode;        // HTTP status code
};

//Forward declarations
//connect to wifi and remote
void connect();
bool connectToWifi(std::string ssid, std::string password);
std::string getIPAddress();
HttpResponse httpGet(const std::string& url);
std::string detectQR();
void openCamera();
bool connectToRemote();
bool runSystemCommand(const std::string& command);
//main logic
bool initModel();
void sendImage();
void loop();
//others
void cleanUp();
void testDetect();
void testCamera();

// Signal handler: only sets the flag.
void interruptHandler(int signum) {
    std::printf("Signal: %d\n", signum);
    interrupted = 1;
    cleanUp();
}

// Clean up resources before exit.
void cleanUp() {
    printf("cleanning up...\n");
    if (cap.isOpened()) {
        cap.release();
    }
    if (tdl_handle != NULL) {
        CVI_TDL_DestroyHandle(tdl_handle);
    }
    std::printf("Exiting...\n");
    exit(0);
}

// Scan for a QR code and set WiFi credentials.
void connect() {
    //get wifi_config file
    std::string ssid;
    std::string password;
    bool isConnected = false;
    std::ifstream file(WIFI_CONFIG_FILE_PATH);
    //read file
    if(file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string text = buffer.str();
        file.close();
        std::stringstream ss(text);
        std::string line;
        while (!interrupted && std::getline(ss, line)) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                if (key == "remoteBaseUrl") {
                    remoteBaseUrl = value;
                }
                else if (key == "ssid") {
                    ssid = value;
                }
                else if(key == "password") {
                    password = value;
                }
            }
        }
        //try to connect
        isConnected = connectToWifi(ssid, password);
        bool canConnectToRemote = connectToRemote();
        //if cannot connect to remote, wifi info maybe wront, just reset
        if (!canConnectToRemote) {
            isConnected = false;
        }
    }
    //if has ip, it's connected
    //if not connected, scan for qr
    if (!isConnected) {
        openCamera();
        while (!interrupted) {
            std::string qrContent = detectQR(); 
            if (qrContent.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            std::cout << "QR Content: " << qrContent << std::endl;
            std::stringstream ss(qrContent);
            std::string line;
            while (std::getline(ss, line)) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string key = line.substr(0, pos);
                    std::string value = line.substr(pos + 1);
                    if (key == "ssid") {
                        ssid = value;
                    } else if (key == "password") {
                        password = value;
                    } else if (key == "remoteBaseUrl") {
                        remoteBaseUrl = value;
                    }
                }
            }
            if(ssid.empty() || password.empty() || remoteBaseUrl.empty()) {
                continue;
            }
            bool isConnected = connectToWifi(ssid, password);
            if(!isConnected) {
                continue;
            }
            //now wifi is connected, connect to remote
            bool canConnectRemote = connectToRemote();
            if (!canConnectRemote) {
                continue;
            }
            break;
        }
    }
    //now everything is up, save the file
    std::string wifiConfig = "ssid:" + ssid + "\npassword:" + password + "\nremoteBaseUrl:" + remoteBaseUrl;
    std::ofstream newFile(WIFI_CONFIG_FILE_PATH, std::ios::trunc);
    if (newFile.is_open()) {
        newFile << wifiConfig;
        newFile.close();
    } else {
        std::cerr << "Unable to open file: " << WIFI_CONFIG_FILE_PATH << std::endl;
        //no need to continue scan or return false
    }
}

// Detect and decode a QR code from the current camera frame.
std::string detectQR() {
    std::printf("Detecting QR code for WiFi\n");
    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) return "";
    cv::Mat bbox, rectifiedImage;
    std::string data = qrDecoder.detectAndDecode(frame, bbox, rectifiedImage);
    return data;
}

// Open the default camera and warm it up.
void openCamera() {
    while (!interrupted && !cap.isOpened()) {
        std::cout << "Opening camera..." << std::endl;
        cap.set(cv::CAP_PROP_FRAME_WIDTH, INPUT_WIDTH);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, INPUT_HEIGHT);
        cap.open(0);
        if (!cap.isOpened()) {
            std::cerr << "Camera open failed, retrying in 3 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        } else {
            cv::Mat dummy;
            // Warm up: skip initial frames.
            for (int i = 0; i < 30 && !interrupted; ++i)
                cap >> dummy;
            std::cout << "Camera opened successfully!" << std::endl;
        }
    }
}

// Set camera resolution.
bool setCameraResolution(bool max) {
    int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    if (max) {
        if (width != 2560 || height != 1440) {
            return false;
        }
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 2560);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1440);
    } else {
        if (width != INPUT_WIDTH || height != INPUT_HEIGHT) {
            return false;
        }
        cap.set(cv::CAP_PROP_FRAME_WIDTH, INPUT_WIDTH);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, INPUT_HEIGHT);
    }
    return true;
}

// Connect to WiFi using system commands.
bool connectToWifi(std::string ssid, std::string password) {
    std::cout << "Connecting to " << ssid << std::endl;
    std::string cmd = "wpa_cli -i wlan0 add_network";
    //int network_id = std::system(cmd.c_str()); // Returns network ID (e.g., 0)
    cmd = "wpa_cli -i wlan0 set_network 0 ssid '\"" + ssid + "\"'";
    std::system(cmd.c_str());
    cmd = "wpa_cli -i wlan0 set_network 0 psk '\"" + password + "\"'";
    std::system(cmd.c_str());
    cmd = "wpa_cli -i wlan0 enable_network 0";
    std::system(cmd.c_str());
    cmd = "wpa_cli -i wlan0 save_config"; // Persist configuration [[4]]
    myIp = getIPAddress();
    if (!myIp.empty()) {
        std::cout << "Connected to " << ssid << " with IP " << myIp << std::endl;
        return true;
    }
    return false;
}

// Get the IP address of interface "wlan0".
std::string getIPAddress() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return "";
    struct ifreq ifr{};
    strncpy(ifr.ifr_name, INTERFACE_NAME, IFNAMSIZ);
    if (ioctl(fd, SIOCGIFADDR, &ifr) == -1) {
        close(fd);
        return "";
    }
    close(fd);
    return inet_ntoa(((sockaddr_in*)&ifr.ifr_addr)->sin_addr);
}

// Connect to the remote server by pinging a URL.
bool connectToRemote() {
    std::string url = remoteBaseUrl + "/ping?ip=" + myIp;
    std::cout << "Connecting to remote URL " << url << std::endl;
    HttpResponse response = httpGet(url);
    if (response.statusCode == 200) {
        std::cout << "Successfully connected to remote" << std::endl;
        return true;
    } else {
        std::cerr << "Failed to connect to remote" << std::endl;
        return false;
    }
}

// Run a system command and return true if successful.
bool runSystemCommand(const std::string& command) {
    int status = system(command.c_str());
    if (status != 0) {
        std::cerr << "Error executing: " << command << std::endl;
        return false;
    }
    return true;
}

// Callback function to handle the response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Function to perform an HTTP GET request using libcurl
HttpResponse httpGet(const std::string& url) {
    CURL* curl;
    CURLcode res;
    HttpResponse response;
    // Initialize libcurl
    curl = curl_easy_init();
    if (curl) {
        // Set the URL
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        // Set the callback function to handle the response
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        // Perform the request
        res = curl_easy_perform(curl);
        // Check for errors
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            // Retrieve the HTTP status code
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
        }
        // Cleanup
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "Failed to initialize libcurl." << std::endl;
    }
    return response;
}

// Main processing loop: compares frames and triggers sending image if change is detected.
void loop() {
    int changedThreshold = 0;
    int noChangeCount = 0;
    cv::Mat previousNoChangeFrame;
    openCamera();
    if (!initModel()) {
        cleanUp();
        return;
    }
    while (!interrupted) {
        cv::Mat img;
        cap >> img;
        if (img.empty())
            continue;
        if (changedThreshold == 0) {
            changedThreshold = static_cast<int>(img.cols * img.rows * CHANGE_THRESHOLD_PERCENT);
        }
        cv::Mat grayFrame;
        cv::cvtColor(img, grayFrame, cv::COLOR_BGR2GRAY);
        if (previousNoChangeFrame.empty()) {
            previousNoChangeFrame = grayFrame.clone();
            continue;
        }
        cv::Mat diff, thresh;
        cv::absdiff(grayFrame, previousNoChangeFrame, diff);
        cv::threshold(diff, thresh, 30, 255, cv::THRESH_BINARY);
        int nonZeroCount = cv::countNonZero(thresh);
        if (nonZeroCount < changedThreshold) {
            noChangeCount++;
            if (noChangeCount == NO_CHANGE_FRAME_LIMIT) {
                std::cout << "No significant change\n" << std::endl;
                VIDEO_FRAME_INFO_S *frame_ptr = (VIDEO_FRAME_INFO_S *)cap.getFrameInfo();
                if (frame_ptr == nullptr) {
                    printf("frame_ptr is nullptr\n");
                    cap.release();
                    return;
                }
                printf("start detecting...\n");
                cvtdl_object_t obj_meta = {0};
                CVI_TDL_YOLOV8_Detection(tdl_handle, frame_ptr, &obj_meta);
                if (obj_meta.size > 0) {
                    for (uint32_t i = 0; i < obj_meta.size; i++) {
                        // cv::Rect r(static_cast<int>(obj_meta.info[i].bbox.x1),
                        //            static_cast<int>(obj_meta.info[i].bbox.y1),
                        //            static_cast<int>(obj_meta.info[i].bbox.x2 - obj_meta.info[i].bbox.x1),
                        //            static_cast<int>(obj_meta.info[i].bbox.y2 - obj_meta.info[i].bbox.y1));
                        printf("detected class %d!!!\n", obj_meta.info[i].classes);
                    }
                    sendImage();
                }
                else {
                    printf("no detection!\n");
                }
            } else if (noChangeCount > NO_CHANGE_FRAME_LIMIT) {
                continue;
            }
        } else {
            int percent = static_cast<int>((static_cast<float>(nonZeroCount) / (img.cols * img.rows)) * 100);
            std::cout << "Change detected: " << percent << "%" << std::endl;
            previousNoChangeFrame = grayFrame.clone();
            noChangeCount = 0;
        }
    }
    cap.release();
}

// Initialize YOLOv8 model and set parameters.
bool initModel() {
    CVI_S32 ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        printf("Create tdl handle failed with %#x!\n", ret);
        return false;
    }
    // setup preprocess
    // will cause 2 enter CVI_TDL_Get_YOLO_Preparam... in log
    // YoloPreParam preprocess_cfg = CVI_TDL_Get_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    // for (int i = 0; i < 3; i++) {
    //     preprocess_cfg.factor[i] = 0.0039216;
    //     preprocess_cfg.mean[i] = 0;
    // }
    // preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;
    // ret = CVI_TDL_Set_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
    // if (ret != CVI_SUCCESS) {
    //     printf("Can not set yolov8 preprocess parameters %#x\n", ret);
    //     return false;
    // }
    // setup yolo algorithm preprocess
    YoloAlgParam yolov8_param = CVI_TDL_Get_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;
    ret = CVI_TDL_Set_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS) {
        printf("Can not set yolov8 algorithm parameters %#x\n", ret);
        return false;
    }
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_FILE_PATH);
    if (ret != CVI_SUCCESS) {
        printf("open model failed with %#x!\n", ret);
        return ret;
    }
    return true;
}

// Upload an image by saving, encoding to JPEG, and sending via HTTP POST.
void sendImage() {
    std::cout << "Sending image to remote" << std::endl;
    if (!setCameraResolution(true)) {
        return;
    }
    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) {
        std::cerr << "Captured empty frame!" << std::endl;
        return;
    }
    // Encode image to buffer
    std::vector<uchar> buffer;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 90}; // Adjust format and quality as needed
    if (!cv::imencode(".jpg", frame, buffer, params)) {
        std::cerr << "Failed to encode image." << std::endl;
        return;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize libcurl" << std::endl;
        return;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    std::string url = remoteBaseUrl + "/upload";
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, buffer.size());
    res = curl_easy_perform(curl);
     if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        std::cout << "HTTP Status Code: " << httpCode << std::endl;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // std::cout << "Sending image to remote" << std::endl;
    // if (!setCameraResolution(true)) {
    //     return;
    // }
    // cv::Mat frame;
    // cap >> frame;
    // if (frame.empty()) {
    //     std::cerr << "Captured empty frame!" << std::endl;
    //     return;
    // }
    // if (!cv::imwrite(SAVE_IMAGE_PATH, frame)) {
    //     std::cerr << "Failed to save image to path" << std::endl;
    //     return;
    // }
    // // std::vector<uchar> imgData;
    // // std::string imagePath = "files/test.jpg";
    // // cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    // // if (!cv::imencode(".jpg", image, imgData)) {
    // //     std::cerr << "Error: Could not encode image!" << std::endl;
    // //     return;
    // // }
    // // Convert the OpenCV Mat image to a buffer (binary data)
    // std::vector<uchar> buffer;
    // if (!cv::imencode(".jpg", frame, buffer)) {  // Encode the image as JPEG <button class="citation-flag" data-index="8">
    //     std::cerr << "Failed to encode the image." << std::endl;
    //     return;
    // }
    // CURL* curl;
    // CURLcode res;
    // // Initialize libcurl
    // curl = curl_easy_init();
    // if (curl) {
    //     std::string url = remoteBaseUrl + "/upload";
    //     curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    //     curl_easy_setopt(curl, CURLOPT_POST, 1L);
    //     curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer.data());
    //     curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, buffer.size());
    //     struct curl_slist* headers = nullptr;
    //     headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    //     curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    //     res = curl_easy_perform(curl);
    //     if (res != CURLE_OK) {
    //         std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
    //     } else {
    //         long httpCode = 0;
    //         curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    //         std::cout << "HTTP Status Code: " << httpCode << std::endl;
    //     }
    //     curl_slist_free_all(headers);
    //     curl_easy_cleanup(curl);
    // } else {
    //     std::cerr << "Failed to initialize libcurl" << std::endl;
    //     return;
    // }
}

void testDetect() {
    CVI_S32 ret = MMF_INIT_HELPER2(INPUT_WIDTH, INPUT_HEIGHT, PIXEL_FORMAT_RGB_888, 1,
                                   INPUT_WIDTH, INPUT_HEIGHT, PIXEL_FORMAT_RGB_888, 1);
    if (ret != CVI_TDL_SUCCESS) {
        printf("Init sys failed with %#x!\n", ret);
        return;
    }
     if (!initModel()) {
        cleanUp();
        return;
    }
    std::vector<uchar> imgData;
    std::string imagePath = "/root/test.jpg";
    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (!cv::imencode(".jpg", image, imgData)) {
        std::cerr << "Error: Could not encode image!" << std::endl;
        return;
    }
    printf("converting mat to frame_ptr\n");
    if (image.data == nullptr) {
        printf("image.data is nullptr\n");
        return;
    }
    VIDEO_FRAME_INFO_S* frame_ptr = reinterpret_cast<VIDEO_FRAME_INFO_S*>(image.data);
    //VIDEO_FRAME_INFO_S *frame_ptr = (VIDEO_FRAME_INFO_S *)cap.image_ptr;
    if(frame_ptr == nullptr) {
        std::printf("frame_ptr is nullptr\n");
        return;
    }
    cvtdl_object_t obj_meta = {0};
    printf("detecting...\n");
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
    //         cv::rectangle(image, r, BLUE_MAT, 1, 8, 0);
    //     else if (obj_meta.info[i].classes == 1)
    //         cv::rectangle(image, r, RED_MAT, 1, 8, 0);
    // }
    if (obj_meta.size == 0) {
        printf("no detection!!!\n");
    }
    else {
        printf("has detection!!!\n");
    }
    // Return true if any object was detected.
   // return (obj_meta.size > 0);
}

void testCamera() {
    openCamera();
    if (!initModel()) {
        cleanUp();
        return;
    }
    while (!interrupted) {
        cv::Mat img;
        cap >> img;
        if (img.empty()) {
            printf("img is empty\n");
            continue;
        }
        VIDEO_FRAME_INFO_S* frame_ptr = reinterpret_cast<VIDEO_FRAME_INFO_S*>(img.data);
        if (frame_ptr == nullptr) {
            printf("frame_ptr is nullptr\n");
            cap.release();
            return;
        }
        printf("start detecting...\n");
        cvtdl_object_t obj_meta = {0};
        CVI_TDL_YOLOV8_Detection(tdl_handle, frame_ptr, &obj_meta);
        if (obj_meta.size > 0) {
            printf("has detection!\n");
            sendImage();
        }
        else {
            printf("no detection!\n");
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    cap.release();
}

int main() {
    signal(SIGINT, interruptHandler);
    #ifdef NO_WIFI
        testCamera();
    #else 
        //testDetect();
        connect();
        loop();
    #endif
    return 0;
}
