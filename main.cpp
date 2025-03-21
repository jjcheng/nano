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
#include <cstdlib>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <functional>
#include <map>
#include <curl/curl.h>
#include <sys/ioctl.h>
#include <linux/if.h>

// Custom includes
#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

// Constants
constexpr size_t BUFFER_SIZE = 4096;
constexpr const char* WIFI_CONFIG_FILE_PATH = "/root/wifi_config";
constexpr const char* SAVE_IMAGE_PATH = "/root/captured.jpg";
constexpr double CONF_THRESHOLD = 0.5;
constexpr double IOU_THRESHOLD = 0.5;
constexpr int NO_CHANGE_FRAME_LIMIT = 10;
constexpr double CHANGE_THRESHOLD_PERCENT = 0.10;
constexpr const char* INTERFACE_NAME = "wlan0";

// YOLO defines
constexpr const char* MODEL_FILE_PATH = "/root/detect.cvimodel";
constexpr int MODEL_CLASS_CNT = 3;  // underline, highlight, pen
constexpr double MODEL_THRESH = 0.5;
constexpr double MODEL_NMS_THRESH = 0.5;
constexpr int INPUT_FRAME_WIDTH = 320;
constexpr int INPUT_FRAME_HEIGHT = 320;
constexpr int MAX_FRAME_WIDTH = 2560;
constexpr int MAX_FRAME_HEIGHT = 1440;

// Global variables
std::string remoteBaseUrl = "";
cv::VideoCapture cap;
//cv::QRCodeDetector qrDecoder;
cvitdl_handle_t tdl_handle = nullptr;

// Use volatile sig_atomic_t for safe signal flag updates.
volatile sig_atomic_t interrupted = 0;

struct HttpResponse {
    std::string body;
    long statusCode;
};

// Utilities
// Helper function to trim whitespace from both ends of a string.
std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos || end == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

void sleepSeconds(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

// Signal handler (only sets flag)
void interruptHandler(int signum) {
    std::printf("Received signal: %d\n", signum);
    interrupted = 1;
}

// Run a system command and return true if the return code is 0.
bool runSystemCommand(const std::string& command) {
    int status = std::system(command.c_str());
    if (status != 0) {
        std::cerr << "Error executing command: " << command << std::endl;
        return false;
    }
    return true;
}

// Get the IP address of the specified network interface.
std::string getIPAddress() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return "";
    struct ifreq ifr{};
    strncpy(ifr.ifr_name, INTERFACE_NAME, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) == -1) {
        close(fd);
        return "";
    }
    close(fd);
    return inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr);
}

// Callback for libcurl to collect HTTP response data.
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Perform an HTTP GET request using libcurl.
HttpResponse httpGet(const std::string& url) {
    HttpResponse response{ "", 0 };
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize libcurl." << std::endl;
        return response;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "libcurl error: " << curl_easy_strerror(res) << std::endl;
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    }
    curl_easy_cleanup(curl);
    return response;
}

// Function to make an HTTP POST request with a body and return the response.
HttpResponse httpPost(const std::string& url, const std::string& postBody) {
    HttpResponse response { "", 0 };
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize libcurl." << std::endl;
        return response;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postBody.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "HTTP POST failed: " << curl_easy_strerror(res) << std::endl;
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    }
    curl_easy_cleanup(curl);
    return response;
}

// Clean up resources gracefully.
void cleanUp() {
    cap.release();
    if (tdl_handle != nullptr) {
        CVI_TDL_DestroyHandle(tdl_handle);
        tdl_handle = nullptr;
    }
    curl_global_cleanup();
}

void sendErrorToRemote(const std::string& error) {
    std::string url = remoteBaseUrl + "/error";
    std::string body = "{\"error\":\"" + error + "\"}";
    HttpResponse response = httpPost(url, body);
    if(response.statusCode == 200) {
        std::cout << "Error message sent to remote" << std::endl;
    }
    else {
        std::cerr << "Error sending error message to remote" << std::endl;
    }
}

// Use OpenCV's QRCodeDetector to detect and decode a QR code from the current frame.
std::string detectQR() {
    std::cout << "Detecting QR code" << std::endl;
    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) return "";
    //cv::Mat frame = cv::imread("/root/qr.jpg");
    cv::Mat point;
    try {
        // bool detected = qrDecoder.detect(frame, point);
        // if (detected) {
        //     printf("QR Code Detected\n");
        // }
        // else {
        //     printf("NO QR Code Detected\n");
        // }
        //std::string text = qrDecoder.decode(frame, point);
        //printf("detected text: %s\n", text.c_str());
        cv::QRCodeDetector qrDecoder;
        std::string data = qrDecoder.detectAndDecode(frame);
        if (data.empty()) {
            printf("NO QR Code Detected\n");
        }
        else {
            printf("QR Code Detected: %s\n", data.c_str());
        }
        return data;
    }
    catch (const cv::Exception& ex) {
        std::cerr << "detectQR error: " << ex.what() << std::endl;
        sendErrorToRemote(ex.what());
        cleanUp();
        return "";
    }
    return "";
}

// Open the default camera and warm it up by skipping initial frames.
void openCamera(int width, int height) {
    int retries = 0;
    while (!interrupted && !cap.isOpened()) {
        retries ++;
        if (retries > 5) {
            throw std::runtime_error("Failed to open camera after 5 tries");
        }
        cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        cap.open(0);
        if (!cap.isOpened()) {
            std::cerr << "Failed to open camera; retrying in 3 seconds..." << std::endl;
            sleepSeconds(3);
        } else {
            cv::Mat dummy;
            for (int i = 0; i < 15 && !interrupted; ++i)
                cap >> dummy;
        }
    }
}

// Set camera resolution. TODO: to optimize
void setCameraResolution(int width, int height) {
    cap.release();
    openCamera(width, height);
}

// Connect to WiFi using system commands via wpa_cli.
bool connectToWifi(const std::string& ssid, const std::string& password) {
    std::cout << "Connecting to WiFi network: " << ssid << std::endl;
    std::string cmd = "wpa_cli -i wlan0 add_network";
    if (!runSystemCommand(cmd)) return false;
    cmd = "wpa_cli -i wlan0 set_network 0 ssid '\"" + ssid + "\"'";
    if (!runSystemCommand(cmd)) return false;
    cmd = "wpa_cli -i wlan0 set_network 0 psk '\"" + password + "\"'";
    if (!runSystemCommand(cmd)) return false;
    cmd = "wpa_cli -i wlan0 enable_network 0";
    if (!runSystemCommand(cmd)) return false;
    cmd = "wpa_cli -i wlan0 save_config";
    if (!runSystemCommand(cmd)) return false;
    std::string myIp = getIPAddress();
    if (!myIp.empty()) {
        return true;
    }
    return false;
}

// Connect to a remote server by sending a ping request.
bool connectToRemote() {
    std::string myIp = getIPAddress();
    std::string url = remoteBaseUrl + "/ping?ip=" + myIp;
    int retries = 0;
    while(!interrupted) {
        retries++;
        if(retries > 5) {
            return false;
        }
        HttpResponse response = httpGet(url);
        if (response.statusCode == 200) {
            return true;
        } else {
            std::cerr << "Remote connection failed, retry after 3 seconds" << std::endl;
            sleepSeconds(3);
        }
    }
    return false;
}

// Initialize the YOLOv8 model and set algorithm parameters.
void initModel() {
    CVI_S32 ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Create TDL handle failed with error code: " + std::to_string(ret));
    }
    // Setup YOLO algorithm parameters.
    YoloAlgParam yolov8_param = CVI_TDL_Get_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = MODEL_CLASS_CNT;
    ret = CVI_TDL_Set_YOLO_Algparam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Failed to set YOLOv8 algorithm parameters: " + std::to_string(ret));
    }
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_THRESH);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_NMS_THRESH);
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, MODEL_FILE_PATH);
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Open model failed with error code: " + std::to_string(ret));
    }
}

// Capture an image, encode it to JPEG, and send it via HTTP POST.
void sendImage() {
    //std::cout << "Sending image to remote server..." << std::endl;
    setCameraResolution(MAX_FRAME_WIDTH, MAX_FRAME_HEIGHT);
    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) {
        setCameraResolution(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT);
        std::cerr << "Captured empty frame!" << std::endl;
        return;
    }
    std::vector<uchar> buffer;
    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 100 };
    if (!cv::imencode(".jpg", frame, buffer, params)) {
        setCameraResolution(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT);
        std::cerr << "Failed to encode image." << std::endl;
        return;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        setCameraResolution(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT);
        std::cerr << "Failed to initialize libcurl" << std::endl;
        return;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    std::string url = remoteBaseUrl + "/upload";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, buffer.size());
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Image upload failed: " << curl_easy_strerror(res) << std::endl;
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        std::cout << "HTTP Status Code: " << httpCode << std::endl;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    setCameraResolution(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT);
}

// Test detection using a saved image (for debugging).
void testDetect() {
    CVI_S32 ret = MMF_INIT_HELPER2(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT, PIXEL_FORMAT_RGB_888, 1,
                                   INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT, PIXEL_FORMAT_RGB_888, 1);
    if (ret != CVI_TDL_SUCCESS) {
        std::printf("Init sys failed with error %#x!\n", ret);
        return;
    }
    initModel();
    std::vector<uchar> imgData;
    std::string imagePath = "/root/test.jpg";
    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (!cv::imencode(".jpg", image, imgData)) {
        std::cerr << "Error: Could not encode image!" << std::endl;
        return;
    }
    std::printf("Converting Mat to frame pointer...\n");
    if (image.data == nullptr) {
        std::printf("image.data is nullptr\n");
        return;
    }
    VIDEO_FRAME_INFO_S* frame_ptr = reinterpret_cast<VIDEO_FRAME_INFO_S*>(image.data);
    if (frame_ptr == nullptr) {
        std::printf("frame_ptr is nullptr\n");
        return;
    }
    cvtdl_object_t obj_meta = {0};
    std::printf("Detecting...\n");
    CVI_TDL_YOLOV8_Detection(tdl_handle, frame_ptr, &obj_meta);
    if (obj_meta.size == 0) {
        std::printf("No detection found!\n");
    } else {
        std::printf("Detection found!\n");
    }
}

// Test camera capture and detection continuously (for debugging).
void testCamera() {
    //openCamera();
    initModel();
    while (!interrupted) {
        cv::Mat img;
        void* image_ptr = cap.capture(img);
        if (img.empty()) {
            std::printf("Empty image captured\n");
            continue;
        }
        VIDEO_FRAME_INFO_S* frame_ptr = reinterpret_cast<VIDEO_FRAME_INFO_S*>(image_ptr);
        if (frame_ptr == nullptr) {
            std::printf("frame_ptr is nullptr\n");
            cap.release();
            return;
        }
        cap.releaseImagePtr();
        std::printf("Detecting on camera frame...\n");
        cvtdl_object_t obj_meta = {0};
        CVI_TDL_YOLOV8_Detection(tdl_handle, frame_ptr, &obj_meta);
        if (obj_meta.size > 0) {
            std::printf("Detection found!\n");
        } else {
            std::printf("No detection!\n");
        }
        sleepSeconds(3);
    }
    cap.release();
}

// Read WiFi configuration file, parse key-value pairs, and attempt to connect.
void setup() {
    std::string ssid, password;
    bool isConnected = false;
    std::ifstream file(WIFI_CONFIG_FILE_PATH);
    if (file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        std::istringstream ss(buffer.str());
        std::string line;
        while (std::getline(ss, line)) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                if (key == "remoteBaseUrl") {
                    remoteBaseUrl = value;
                }
                else if (key == "ssid") {
                    ssid = value;
                }
                else if (key == "password") {
                    password = value;
                }
            }
        }
        // Try to connect using read credentials.
        isConnected = connectToWifi(ssid, password) && connectToRemote();
    }
    // If not connected, scan for QR code to get credentials.
    if (!isConnected) {
        openCamera(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT);
        int retries = 0;
        while (!interrupted) {
            retries++;
            if (retries > 5) {
                throw std::runtime_error("Unable to detect WIFI QR Code after 5 tries");
            }
            std::string qrContent = detectQR();
            if (qrContent.empty()) {
                sleepSeconds(3);
                continue;
            }
            printf("qr code detected: %s\n", qrContent.c_str());
            std::istringstream iss(qrContent);
            std::string line;
            while (std::getline(iss, line)) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string key = trim(line.substr(0, pos));
                    std::string value = trim(line.substr(pos + 1));
                    if (key == "ssid") {
                        ssid = value;
                    } else if (key == "password") {
                        password = value;
                    } else if (key == "remoteBaseUrl") {
                        remoteBaseUrl = value;
                    }
                }
            }
            if (ssid.empty() || password.empty() || remoteBaseUrl.empty()) {
                continue;
            }
            if (!connectToWifi(ssid, password)) {
                continue;
            }
            if (!connectToRemote()) {
                continue;
            }
            break;
        }
    }
    // now we have everything, save configuration to file
    std::string wifiConfig = "ssid:" + ssid + "\npassword:" + password + "\nremoteBaseUrl:" + remoteBaseUrl;
    std::ofstream newFile(WIFI_CONFIG_FILE_PATH, std::ios::trunc);
    if (newFile.is_open()) {
        newFile << wifiConfig;
        newFile.close();
    } else {
        //no need to treat it as error since all 3 variables are ready
        std::cerr << "Unable to open file for writing: " << WIFI_CONFIG_FILE_PATH << std::endl;
    }
}

// Main processing loop: compare frames and trigger detection if no significant change.
void loop() {
    int changedThreshold = 0;
    int noChangeCount = 0;
    int totalPixels = 0;
    cv::Mat previousNoChangeFrame;

    openCamera(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT);
    initModel();
    
    while (!interrupted) {
        cv::Mat img;
        //capture() method will return VIDEO_FRAME_INFO_S*
        void* image_ptr = cap.capture(img);
        if (img.empty()) {
            cap.releaseImagePtr();
            image_ptr = nullptr;
            continue;
        }
        if (image_ptr == nullptr) {
            std::cerr << "main.cpp image_ptr is nullptr" << std::endl;
            cap.releaseImagePtr();
            continue;
        }
        if (totalPixels == 0) {
            totalPixels = img.cols * img.rows;
        }
        if (changedThreshold == 0) {
            changedThreshold = static_cast<int>(totalPixels * CHANGE_THRESHOLD_PERCENT);
        }
        cv::Mat grayFrame;
        cv::cvtColor(img, grayFrame, cv::COLOR_BGR2GRAY);
        if (previousNoChangeFrame.empty()) {
            previousNoChangeFrame = grayFrame.clone();
            cap.releaseImagePtr();
            image_ptr = nullptr;
            continue;
        }
        cv::Mat diff, thresh;
        cv::absdiff(grayFrame, previousNoChangeFrame, diff);
        cv::threshold(diff, thresh, 30, 255, cv::THRESH_BINARY);
        int nonZeroCount = cv::countNonZero(thresh);
        if (nonZeroCount < changedThreshold) {
            noChangeCount++;
            if (noChangeCount != NO_CHANGE_FRAME_LIMIT) {
                cap.releaseImagePtr();
                image_ptr = nullptr;
                continue;
            }
            std::cout << "No significant change detected." << std::endl;
            //convert image_ptr to VIDEO_FRAME_INFO_S*
            VIDEO_FRAME_INFO_S *frameInfo = reinterpret_cast<VIDEO_FRAME_INFO_S*>(image_ptr);
            if (frameInfo == nullptr) {
                std::cerr << "frameInfo is nullptr" << std::endl;
                cap.releaseImagePtr();
                image_ptr = nullptr;
                continue;
            }
            cvtdl_object_t obj_meta = {0};
            CVI_TDL_YOLOV8_Detection(tdl_handle, frameInfo, &obj_meta);
            //release image_ptr
            cap.releaseImagePtr();
            image_ptr = nullptr;
            //check for detections
            if (obj_meta.size == 0) {
                continue;
            }
            std::printf("Detected %d objects\n", obj_meta.size);
            sendImage();
        } else {
            int percent = static_cast<int>((static_cast<float>(nonZeroCount) / totalPixels) * 100);
            std::cout << "Change detected: " << percent << "%" << std::endl;
            noChangeCount = 0;
            cap.releaseImagePtr();
            image_ptr = nullptr;
        }
        previousNoChangeFrame = grayFrame.clone();
    }
}

int main() {
    if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        std::cerr << "curl_global_init() failed" << std::endl;
        return -1;
    }
    signal(SIGINT, interruptHandler);
    try {
        #ifdef NO_WIFI
            testCamera();
        #else 
            setup();
            loop();
        #endif
    } 
    catch (const std::exception& ex) {
        std::cerr << "FATAL ERROR: " << ex.what() << std::endl;
        sendErrorToRemote(ex.what());
        cleanUp();
        return EXIT_FAILURE;
    }
    cleanUp();
    return EXIT_SUCCESS;
}
