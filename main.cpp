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
#define NO_CHANGE_FRAME_LIMIT 30
#define CHANGE_THRESHOLD_PERCENT 0.10

// YOLO defines
#define MODEL_SCALE 0.0039216
#define MODEL_MEAN 0.0
#define MODEL_CLASS_CNT 4
#define MODEL_THRESH 0.5
#define MODEL_NMS_THRESH 0.5
#define MODEL_FILE_PATH "/root/yolov8n_int8_2_class.cvimodel"
#define BLUE_MAT cv::Scalar(255, 0, 0)
#define RED_MAT cv::Scalar(0, 0, 255)

// Global variables
std::string wifiSSID = "";
std::string wifiPassword = "";
std::string remoteBaseUrl = "";
std::string myIp = "";
cv::VideoCapture cap;
cv::QRCodeDetector qrDecoder;
cvitdl_handle_t tdl_handle = NULL;

// Use sig_atomic_t for safe signal handling
volatile sig_atomic_t interrupted = 0;

// Forward declarations
std::string getIPAddress();
bool detect(cv::Mat &image);
void sendImage();
bool runCommand(const std::string& command);
bool httpGetRequest(const std::string &host, const std::string &path);
void setWifiCredentialFromText(const std::string& text);
std::string detectQR();
void openCamera();
void cleanUp();
bool initModel();
void connectToDevice();
void loop();

// Signal handler: only sets the flag.
void interruptHandler(int signum) {
    std::printf("Signal: %d\n", signum);
    interrupted = 1;
}

// Clean up resources before exit.
void cleanUp() {
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
void setWifiConfidentials() {
    std::ifstream file(WIFI_CONFIG_FILE_PATH);
    //if no such file, return
    if (!file.good()) {
        return;
    }
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
    std::cout << "SSID: " << wifiSSID << "\nPASSWORD: " << wifiPassword
              << "\nREMOTEBASEURL: " << remoteBaseUrl << std::endl;
    std::string wifiConfig = "ssid:" + wifiSSID + "\npassword:" + wifiPassword + "\nremoteBaseUrl:" + remoteBaseUrl;
    // Create the config file if it doesn't exist.
    std::ifstream infile(WIFI_CONFIG_FILE_PATH);
    if (!infile.good()) {
        std::ofstream outfile(WIFI_CONFIG_FILE_PATH);
        if (!outfile) {
            std::cerr << "Failed to create file: " << WIFI_CONFIG_FILE_PATH << std::endl;
            return;
        }
        outfile.close();
        std::cout << "File created successfully: " << WIFI_CONFIG_FILE_PATH << std::endl;
    }
    // Write updated configuration.
    std::ofstream ofs(WIFI_CONFIG_FILE_PATH, std::ios::trunc);
    if (!ofs) {
        std::cerr << "Failed to open file: " << WIFI_CONFIG_FILE_PATH << std::endl;
        return;
    }
    ofs << wifiConfig;
    ofs.close();
}

// Scan for a QR code and set WiFi credentials.
void getWifiQR() {
    openCamera();
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
        if (ifa->ifa_addr->sa_family == AF_INET && strcmp(ifa->ifa_name, "wlan0") == 0) {
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
    while (!interrupted) {
        std::string url = remoteBaseUrl + "/ping?id=" + myIp;
        std::cout << "Connecting to remote URL " << url << std::endl;
        if (httpGetRequest(remoteBaseUrl, "/ping?id=" + myIp)) {
            std::cout << "Successfully connected to remote" << std::endl;
            break;
        } else {
            std::cerr << "Failed to connect to remote" << std::endl;
            if (getIPAddress().empty()) {
                connectToWifi();
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
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

// Make an HTTP GET request.
bool httpGetRequest(const std::string &host, const std::string &path) {
    struct addrinfo hints{}, *res;
    int sockfd;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), "80", &hints, &res) != 0) {
        std::cerr << "Failed to resolve host: " << host << std::endl;
        return false;
    }
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed!" << std::endl;
        freeaddrinfo(res);
        return false;
    }
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        std::cerr << "Connection failed!" << std::endl;
        close(sockfd);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);
    
    std::string request = "GET " + path + " HTTP/1.1\r\n"
                          "Host: " + host + "\r\n"
                          "Connection: close\r\n\r\n";
    if (send(sockfd, request.c_str(), request.length(), 0) < 0) {
        std::cerr << "Send failed!" << std::endl;
        close(sockfd);
        return false;
    }
    char buffer[BUFFER_SIZE];
    ssize_t bytesReceived;
    while (!interrupted && (bytesReceived = recv(sockfd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        std::cout << buffer;
    }
    close(sockfd);
    return true;
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
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, "/root/yolov8n_int8_2_class.cvimodel");
    if (ret != CVI_SUCCESS) {
        printf("Open model failed with %#x!\n", ret);
        return false;
    }
    return true;
}

// Perform object detection on the image using YOLOv8.
// Returns true if one or more objects are detected.
bool detect(cv::Mat &image) {
    printf("Performing detection...\n");
    // Convert cv::Mat data pointer to VIDEO_FRAME_INFO_S pointer.
    // (Adjust this cast as required by your custom API.)
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
    // Return true if any object was detected.
    return (obj_meta.size > 0);
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
    if (!cv::imwrite(SAVE_IMAGE_PATH, frame)) {
        std::cerr << "Failed to save image to path" << std::endl;
        return;
    }
    std::vector<uchar> imgData;
    if (!cv::imencode(".jpg", frame, imgData)) {
        std::cerr << "Error: Could not encode image!" << std::endl;
        return;
    }
    if (imgData.empty()) return;
    std::string uploadUrl = remoteBaseUrl + "/upload";
    std::ostringstream request;
    request << "POST " << uploadUrl << " HTTP/1.1\r\n"
            << "Host: " << myIp << "\r\n"
            << "Content-Type: application/octet-stream\r\n"
            << "Content-Length: " << imgData.size() << "\r\n"
            << "Connection: close\r\n\r\n";
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Error: Cannot create socket!" << std::endl;
        return;
    }
    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    // Ensure remoteBaseUrl is an IP address; otherwise, use DNS resolution.
    serverAddr.sin_addr.s_addr = inet_addr(remoteBaseUrl.c_str());
    if (connect(sock, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "Error: Cannot connect to server!" << std::endl;
        close(sock);
        return;
    }
    std::string header = request.str();
    send(sock, header.c_str(), header.length(), 0);
    send(sock, reinterpret_cast<const char*>(imgData.data()), imgData.size(), 0);
    char buffer[BUFFER_SIZE];
    int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        std::cout << "Server Response:\n" << buffer << std::endl;
    }
    close(sock);
}

int main() {
    // Set up signal handler.
    signal(SIGINT, interruptHandler);
    // Initialize YOLOv8 model before detection.
    if (!initModel()) {
        printf("Yolo model initialization failed\n");
        cleanUp();
    }
    // Read WiFi credentials and remote base URL.
    setWifiConfidentials();
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
