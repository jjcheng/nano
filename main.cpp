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
#include <regex>

// Custom includes
#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"

// Constants (using constexpr rather than #define)
constexpr size_t BUFFER_SIZE = 4096;
constexpr const char* WIFI_CONFIG_FILE_PATH = "/root/wifi_config";
constexpr const char* SAVE_IMAGE_PATH = "/root/captured.jpg";
constexpr double CONF_THRESHOLD = 0.5;
constexpr double IOU_THRESHOLD = 0.5;
constexpr int NO_CHANGE_FRAME_LIMIT = 10;
constexpr double CHANGE_THRESHOLD_PERCENT = 0.10;
constexpr const char* INTERFACE_NAME = "wlan0";  // Change as needed

// YOLO defines
constexpr const char* MODEL_FILE_PATH = "/root/detect.cvimodel";
constexpr int MODEL_CLASS_CNT = 3;  // underline, highlight, pen
constexpr double MODEL_THRESH = 0.1;
constexpr double MODEL_NMS_THRESH = 0.1;
constexpr int INPUT_WIDTH = 320;
constexpr int INPUT_HEIGHT = 320;

// Global variables
std::string remoteBaseUrl = "";
cv::VideoCapture cap;
cv::QRCodeDetector qrDecoder;
cvitdl_handle_t tdl_handle = nullptr;

// Use volatile sig_atomic_t for safe signal flag updates.
volatile sig_atomic_t interrupted = 0;

struct HttpResponse {
    std::string body;
    long statusCode;
};

// Helper function to trim whitespace from both ends of a string.
std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos || end == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// Forward declarations
bool connect();
bool connectToWifi(const std::string& ssid, const std::string& password);
std::string getIPAddress();
HttpResponse httpGet(const std::string& url);
HttpResponse postHttp(const std::string& url);
std::string detectQR();
void openCamera();
bool connectToRemote();
bool runSystemCommand(const std::string& command);
void initModel();
void sendImage();
void sendErrorToRemote(const std::string& error);
void loop();
void cleanUp();
void testDetect();
void testCamera();

// Signal handler (only sets flag)
void interruptHandler(int signum) {
    std::printf("Received signal: %d\n", signum);
    interrupted = 1;
}

// Clean up resources gracefully.
void cleanUp() {
    std::cout << "Cleaning up resources..." << std::endl;
    if (cap.isOpened()) {
        cap.release();
    }
    if (tdl_handle != nullptr) {
        CVI_TDL_DestroyHandle(tdl_handle);
        tdl_handle = nullptr;
    }
    curl_global_cleanup();
}

// Read WiFi configuration file, parse key-value pairs, and attempt to connect.
bool connect() {
    std::string ssid, password;
    bool isConnected = false;
    
    std::ifstream file(WIFI_CONFIG_FILE_PATH);
    if (file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        std::stringstream ss(buffer.str());
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
        isConnected = connectToWifi(ssid, password);
        isConnected = connectToRemote();
    }
    // If not connected, scan for QR code to get credentials.
    if (!isConnected) {
        openCamera();
        int qrTimes = 0;
        while (!interrupted) {
            qrTimes++;
            if (qrTimes >= 5) {
                sendErrorToRemote("Unable to detect WIFI QR Code");
                return false;
            }
            std::string qrContent = detectQR();
            if (qrContent.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            std::cout << "QR Content: " << qrContent << std::endl;
            std::stringstream ss(qrContent);
            std::string line;
            while (std::getline(ss, line)) {
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
    // Save configuration for future use.
    if (!ssid.empty() && !password.empty() && !remoteBaseUrl.empty()) {
        std::string wifiConfig = "ssid:" + ssid + "\npassword:" + password + "\nremoteBaseUrl:" + remoteBaseUrl;
        std::ofstream newFile(WIFI_CONFIG_FILE_PATH, std::ios::trunc);
        if (newFile.is_open()) {
            newFile << wifiConfig;
            newFile.close();
        } else {
            //no need to treat it as error since all 3 variables are ready
            std::cerr << "Unable to open file for writing: " << WIFI_CONFIG_FILE_PATH << std::endl;
        }
        return true;
    }
    return false;
}

// Use OpenCV's QRCodeDetector to detect and decode a QR code from the current frame.
std::string detectQR() {
    std::cout << "Detecting QR code..." << std::endl;
    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) return "";
    cv::Mat bbox, rectifiedImage;
    std::string data = qrDecoder.detectAndDecode(frame, bbox, rectifiedImage);
    return data;
}

// Open the default camera and warm it up by skipping initial frames.
void openCamera() {
    while (!interrupted && !cap.isOpened()) {
        std::cout << "Opening camera..." << std::endl;
        cap.set(cv::CAP_PROP_FRAME_WIDTH, INPUT_WIDTH);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, INPUT_HEIGHT);
        cap.open(0);
        if (!cap.isOpened()) {
            std::cerr << "Failed to open camera; retrying in 3 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        } else {
            cv::Mat dummy;
            for (int i = 0; i < 30 && !interrupted; ++i)
                cap >> dummy;
            std::cout << "Camera opened successfully." << std::endl;
        }
    }
}

// Set camera resolution. If max==true, set to 2560x1440; otherwise, set to default INPUT dimensions.
bool setCameraResolution(bool max) {
    if (max) {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 2560);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1440);
    } else {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, INPUT_WIDTH);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, INPUT_HEIGHT);
    }
    int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    if (max && (width != 2560 || height != 1440))
        return false;
    if (!max && (width != INPUT_WIDTH || height != INPUT_HEIGHT))
        return false;
    return true;
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
        std::cout << "Connected with IP: " << myIp << std::endl;
        return true;
    }
    return false;
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

// Connect to a remote server by sending a ping request.
bool connectToRemote() {
    std::string myIp = getIPAddress();
    std::string url = remoteBaseUrl + "/ping?ip=" + myIp;
    std::cout << "Pinging remote URL: " << url << std::endl;
    HttpResponse response = httpGet(url);
    if (response.statusCode == 200) {
        std::cout << "Remote connection successful." << std::endl;
        return true;
    } else {
        std::cerr << "Remote connection failed." << std::endl;
        return false;
    }
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

// Main processing loop: compare frames and trigger detection if no significant change.
void loop() {
    int changedThreshold = 0;
    int noChangeCount = 0;
    cv::Mat previousNoChangeFrame;

    openCamera();
    initModel();
    
    while (!interrupted) {
        cv::Mat img;
        //will return VIDEO_FRAME_INFO_S*
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
            if (noChangeCount >= NO_CHANGE_FRAME_LIMIT) {
                std::cout << "No significant change detected." << std::endl;
                //convert image_ptr to VIDEO_FRAME_INFO_S*
                VIDEO_FRAME_INFO_S *frameInfo = reinterpret_cast<VIDEO_FRAME_INFO_S*>(image_ptr);
                if (frameInfo == nullptr) {
                    std::cerr << "frameInfo is nullptr" << std::endl;
                    cap.releaseImagePtr();
                    image_ptr = nullptr;
                    continue;
                }
                std::cout << "Performing YOLO detection..." << std::endl;
                cvtdl_object_t obj_meta = {0};
                CVI_TDL_YOLOV8_Detection(tdl_handle, frameInfo, &obj_meta);
                //release image_ptr
                cap.releaseImagePtr();
                image_ptr = nullptr;
                frameInfo= nullptr;
                //check for detections
                if (obj_meta.size > 0) {
                    for (uint32_t i = 0; i < obj_meta.size; i++) {
                        std::printf("Detected class %d!\n", obj_meta.info[i].classes);
                    }
                    sendImage();
                } else {
                    std::cout << "No objects detected." << std::endl;
                }
            }
        } else {
            int percent = static_cast<int>((static_cast<float>(nonZeroCount) / (img.cols * img.rows)) * 100);
            std::cout << "Change detected: " << percent << "%" << std::endl;
            previousNoChangeFrame = grayFrame.clone();
            noChangeCount = 0;
        }
    }
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
    std::cout << "Sending image to remote server..." << std::endl;
    if (!setCameraResolution(true))
        return;

    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) {
        std::cerr << "Captured empty frame!" << std::endl;
        return;
    }
    std::vector<uchar> buffer;
    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
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

// Test detection using a saved image (for debugging).
void testDetect() {
    CVI_S32 ret = MMF_INIT_HELPER2(INPUT_WIDTH, INPUT_HEIGHT, PIXEL_FORMAT_RGB_888, 1,
                                   INPUT_WIDTH, INPUT_HEIGHT, PIXEL_FORMAT_RGB_888, 1);
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
    openCamera();
    initModel();
    while (!interrupted) {
        cv::Mat img;
        cap >> img;
        if (img.empty()) {
            std::printf("Empty image captured\n");
            continue;
        }
        // WARNING: Unsafe reinterpret_cast—ensure image.data is compatible.
        VIDEO_FRAME_INFO_S* frame_ptr = reinterpret_cast<VIDEO_FRAME_INFO_S*>(img.data);
        if (frame_ptr == nullptr) {
            std::printf("frame_ptr is nullptr\n");
            cap.release();
            return;
        }
        std::printf("Detecting on camera frame...\n");
        cvtdl_object_t obj_meta = {0};
        CVI_TDL_YOLOV8_Detection(tdl_handle, frame_ptr, &obj_meta);
        if (obj_meta.size > 0) {
            std::printf("Detection found!\n");
            sendImage();
        } else {
            std::printf("No detection!\n");
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    cap.release();
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL); 
    signal(SIGINT, interruptHandler);
    try {
        #ifdef NO_WIFI
            testCamera();
        #else 
            if(connect()) {
                loop();
            }
        #endif
    } 
    catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        sendErrorToRemote(ex.what());
        cleanUp();
        return EXIT_FAILURE;
    }
    cleanUp();
    return EXIT_SUCCESS;
}
