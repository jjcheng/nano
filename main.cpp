#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
// #include <opencv2/objdetect.hpp>
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

// custom includes
#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

// YOLO defines
#define MODEL_FILE_PATH "/root/detect.cvimodel"
#define MODEL_CLASS_CNT 3 //underline, highlight, pen
#define MODEL_THRESH 0.5
#define MODEL_NMS_THRESH 0.5
#define BLUE_MAT cv::Scalar(255, 0, 0)
#define RED_MAT cv::Scalar(0, 0, 255)

// Global variables
std::string wifiSSID = "";
std::string wifiPassword = "";
std::string remoteBaseUrl = "";
std::string myIp = "";
cv::VideoCapture cap;
// cv::QRCodeDetector qrDecoder;
cvitdl_handle_t tdl_handle = NULL;

// Use sig_atomic_t for safe signal handling
volatile sig_atomic_t interrupted = 0;

struct HttpResponse {
    std::string body;       // Response body
    long statusCode;        // HTTP status code
};

// Forward declarations
std::string getIPAddress();
bool detect(cv::Mat &image);
void sendImage();
bool runCommand(const std::string& command);
HttpResponse http_get(const std::string& url);
void setWifiCredentialFromText(const std::string& text);
std::string detectQR();
void openCamera();
void cleanUp();
bool initModel();
void connectToDevice();
bool checkIsConnectedToWifi();
void loop();
void testDetect();

// Signal handler: only sets the flag.
void interruptHandler(int signum) {
    std::printf("Signal: %d\n", signum);
    interrupted = 1;
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

// Read WiFi config from file and set credentials.
void setWifiCredentials() {
    std::ifstream file(WIFI_CONFIG_FILE_PATH);
    if (!file) {
        std::printf("no wifi_config file\n");
        return;
    }
    //read file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    file.close();
    setWifiCredentialFromText(text);
}

// Parse WiFi credentials from text and update the config file.
void setWifiCredentialFromText(const std::string& text) {
    std::stringstream ss(text);
    std::string line;
    while (!interrupted && std::getline(ss, line)) {
        size_t pos = line.find(':');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            if (key == "ssid") {
                wifiSSID = value;
            } else if (key == "password") {
                wifiPassword = value;
            } else if (key == "remoteBaseUrl") {
                remoteBaseUrl = value;
            }
        }
    }
    std::cout << "SSID: " << wifiSSID << " PASSWORD: " << wifiPassword << "REMOTEBASEURL: " << remoteBaseUrl << std::endl;
    std::string wifiConfig = "ssid:" + wifiSSID + "\npassword:" + wifiPassword + "\nremoteBaseUrl:" + remoteBaseUrl;
    //create new file if doestn't exist
    std::ofstream file(WIFI_CONFIG_FILE_PATH, std::ios::trunc);
    if (file.is_open()) {
        file << wifiConfig;
        file.close();
    } else {
        std::cerr << "Unable to open file: " << WIFI_CONFIG_FILE_PATH << std::endl;
    }
}

// Scan for a QR code and set WiFi credentials.
void getWifiQR() {
    if (wifiSSID.empty()) {
        openCamera();
    }
    while (wifiSSID.empty() && !interrupted) {
        std::string content = detectQR();
        if (content.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        std::cout << "QR Content: " << content << std::endl;
        setWifiCredentialFromText(content);
    }
    if (interrupted) {
        cleanUp();
    }
}

// Detect and decode a QR code from the current camera frame.
std::string detectQR() {
    // std::printf("Detecting QR code for WiFi\n");
    // cv::Mat frame;
    // cap >> frame;
    // if (frame.empty()) return "";
    // cv::Mat bbox, rectifiedImage;
    // std::string data = qrDecoder.detectAndDecode(frame, bbox, rectifiedImage);
    // return data;
    return "";
}

// Open the default camera and warm it up.
void openCamera() {
    while (!interrupted && !cap.isOpened()) {
        std::cout << "Opening camera..." << std::endl;
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
    if (max) {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 2560);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1440);
    } else {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 320);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 320);
    }
    return true;
}

// Connect to WiFi using system commands.
void connectToWifi() {
    //since connecting to wifi requires writing data to /etc/wpa_supplicant.conf, at this point maybe it's already connected to wifi, check for that first
    if(checkIsConnectedToWifi()) {
        std::cout << "Connected to " << wifiSSID << " with IP " << myIp << std::endl;
    }
    std::cout << "Connecting to " << wifiSSID << std::endl;
    std::string configCmd = "echo 'network={\n    ssid=\"" + wifiSSID + "\"\n    psk=\"" + wifiPassword + "\"\n}' > /etc/wpa_supplicant.conf";
    if (!runCommand(configCmd) ||
        !runCommand("ifconfig wlan0 up") ||
        !runCommand("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf") ||
        !runCommand("udhcpc -i wlan0")) {
        wifiSSID = "";
        getWifiQR();
        return;
    }
    myIp = getIPAddress();
    std::cout << "Connected to " << wifiSSID << " with IP " << myIp << std::endl;
}

bool checkIsConnectedToWifi() {
    // std::string cmd = "ip addr show " + INTERFACE_NAME;
    // FILE* pipe = popen(cmd.c_str(), "r");
    // if (!pipe) {
    //     std::cerr << "Failed to run command: " << cmd << "\n";
    //     return false;
    // }
    // char buffer[128];
    // bool connected = false;
    // while (fgets(buffer, sizeof(buffer), pipe)) {
    //     if (strstr(buffer, "inet ")) { // found an IP address
    //         connected = true;
    //         break;
    //     }
    // }
    // pclose(pipe);
    // return connected;
    return getIPAddress() != "";
}

// Get the IP address of interface "wlan0".
std::string getIPAddress() {
    struct ifaddrs *ifaddr, *ifa;
    char ip[INET_ADDRSTRLEN] = {0};
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs failed");
        return "";
    }
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr)
            continue;
        if (ifa->ifa_addr->sa_family == AF_INET && strcmp(ifa->ifa_name, INTERFACE_NAME) == 0) {
            struct sockaddr_in *sin = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
            inet_ntop(AF_INET, &sin->sin_addr, ip, INET_ADDRSTRLEN);
            break;
        }
    }
    freeifaddrs(ifaddr);
    return std::string(ip);
}

// Connect to the remote server by pinging a URL.
void connectToDevice() {
    // while (!interrupted) {
    //     std::string url = remoteBaseUrl + "/ping?ip=" + myIp;
    //     std::cout << "Connecting to remote URL " << url << std::endl;
    //     HttpResponse response = http_get(url);
    //     if (response.statusCode == 200) {
    //         std::cout << "Successfully connected to remote" << std::endl;
    //         break;
    //     } else {
    //         std::cerr << "Failed to connect to remote" << std::endl;
    //         if (getIPAddress().empty()) {
    //             connectToWifi();
    //         }
    //         std::this_thread::sleep_for(std::chrono::seconds(3));
    //     }
    // }
}

// Run a system command and return true if successful.
bool runCommand(const std::string& command) {
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
HttpResponse http_get(const std::string& url) {
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
                std::cout << "No significant change" << std::endl;
                if (detect(img)) {
                    sendImage();
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
    printf("init mmf\n");
    int vpssgrp_width = 320;
    int vpssgrp_height = 320;
    CVI_S32 ret = MMF_INIT_HELPER2(vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1,
                                   vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1);
    if (ret != CVI_TDL_SUCCESS) {
        printf("Init sys failed with %#x!\n", ret);
        return false;
    }
    ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        printf("Create tdl handle failed with %#x!\n", ret);
        return false;
    }
    // setup preprocess
    YoloPreParam preprocess_cfg = CVI_TDL_Get_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    for (int i = 0; i < 3; i++) {
        preprocess_cfg.factor[i] = 0.0039216;
        preprocess_cfg.mean[i] = 0;
    }
    preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;
    printf("setup yolov8 param \n");
    ret = CVI_TDL_Set_YOLO_Preparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
    if (ret != CVI_SUCCESS) {
        printf("Can not set yolov8 preprocess parameters %#x\n", ret);
        return false;
    }
    // setup yolo algorithm preprocess
    YoloAlgParam yolov8_param = CVI_TDL_Get_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;
    printf("setup yolov8 algorithm param \n");
    ret = CVI_TDL_Set_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS) {
        printf("Can not set yolov8 algorithm parameters %#x\n", ret);
        return false;
    }
    // set theshold
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);
    printf("yolov8 algorithm parameters setup success!\n");
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_FILE_PATH);
    if (ret != CVI_SUCCESS) {
        printf("open model failed with %#x!\n", ret);
        return ret;
    }
    return true;
}

// Perform object detection on the image using YOLOv8.
// Returns true if one or more objects are detected.
bool detect(cv::Mat &image) {
    printf("Performing detection...\n");
    VIDEO_FRAME_INFO_S* frame_ptr = reinterpret_cast<VIDEO_FRAME_INFO_S*>(image.data);
    cvtdl_object_t obj_meta = {0};
    CVI_TDL_YOLOV8_Detection(tdl_handle, frame_ptr, &obj_meta);
    // Draw bounding boxes on the image.
    for (uint32_t i = 0; i < obj_meta.size; i++) {
        cv::Rect r(static_cast<int>(obj_meta.info[i].bbox.x1),
                   static_cast<int>(obj_meta.info[i].bbox.y1),
                   static_cast<int>(obj_meta.info[i].bbox.x2 - obj_meta.info[i].bbox.x1),
                   static_cast<int>(obj_meta.info[i].bbox.y2 - obj_meta.info[i].bbox.y1));
        if (obj_meta.info[i].classes == 0)
            cv::rectangle(image, r, BLUE_MAT, 1, 8, 0);
        else if (obj_meta.info[i].classes == 1)
            cv::rectangle(image, r, RED_MAT, 1, 8, 0);
    }
    return (obj_meta.size > 0);
}

// Upload an image by saving, encoding to JPEG, and sending via HTTP POST.
void sendImage() {
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
    std::vector<uchar> imgData;
    std::string imagePath = "files/test.jpg";
    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (!cv::imencode(".jpg", image, imgData)) {
        std::cerr << "Error: Could not encode image!" << std::endl;
        return;
    }
    // Convert the OpenCV Mat image to a buffer (binary data)
    std::vector<uchar> buffer;
    if (!cv::imencode(".jpg", image, buffer)) {  // Encode the image as JPEG <button class="citation-flag" data-index="8">
        std::cerr << "Failed to encode the image." << std::endl;
        return;
    }
    CURL* curl;
    CURLcode res;
    // Initialize libcurl
    curl = curl_easy_init();
    if (curl) {
        // Set the URL
        std::string url = remoteBaseUrl + "/upload";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // Set the HTTP method to POST
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        // Set the raw binary data as the POST body
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, buffer.size());

        // Set the Content-Type header to application/octet-stream
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Perform the request
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            std::cout << "HTTP Status Code: " << httpCode << std::endl;
        }

        // Cleanup
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "Failed to initialize libcurl." << std::endl;
        return;
    }
}

void testDetect() {
    std::vector<uchar> imgData;
    std::string imagePath = "/root/files/test.jpg";
    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (!cv::imencode(".jpg", image, imgData)) {
        std::cerr << "Error: Could not encode image!" << std::endl;
        return;
    }
    printf("converting mat to frame_ptr\n");
    VIDEO_FRAME_INFO_S* frame_ptr = reinterpret_cast<VIDEO_FRAME_INFO_S*>(image.data);
    if(frame_ptr == NULL) {
        std::printf("failed to get frame_ptr\n");
        return;
    }
    cvtdl_object_t obj_meta = {0};
    printf("detecting...\n");
    CVI_TDL_YOLOV8_Detection(tdl_handle, frame_ptr, &obj_meta);
    printf("detection completed\n");
    // Draw bounding boxes on the image.
    for (uint32_t i = 0; i < obj_meta.size; i++) {
        std::printf("detected %d\n", i);
        cv::Rect r(static_cast<int>(obj_meta.info[i].bbox.x1),
                   static_cast<int>(obj_meta.info[i].bbox.y1),
                   static_cast<int>(obj_meta.info[i].bbox.x2 - obj_meta.info[i].bbox.x1),
                   static_cast<int>(obj_meta.info[i].bbox.y2 - obj_meta.info[i].bbox.y1));
        if (obj_meta.info[i].classes == 0)
            cv::rectangle(image, r, BLUE_MAT, 1, 8, 0);
        else if (obj_meta.info[i].classes == 1)
            cv::rectangle(image, r, RED_MAT, 1, 8, 0);
    }
    if (obj_meta.size == 0) {
        printf("no detection!!!\n");
    }
    // Return true if any object was detected.
   // return (obj_meta.size > 0);
}

int main() {
    // Set up signal handler.
    signal(SIGINT, interruptHandler);
    // Initialize YOLOv8 model before detection.
    if (!initModel()) {
        cleanUp();
    }
    //testDetect();
    // Read WiFi credentials and remote base URL.
    setWifiCredentials();
    // If no SSID is set, scan QR code.
    getWifiQR();
    // Connect to WiFi and remote server.
    connectToWifi();
    // Connect to device using wifi
    connectToDevice();
    // Start the main processing loop.
    loop();
    return 0;
}
