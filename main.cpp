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
#include <future>
#include <thread>
#include <cstring>

// Custom includes
#include "cvi_tdl.h"

// Forward declarations
void sendMat(cv::Mat image);

// Constants
constexpr const char* WIFI_CONFIG_FILE_NAME = "wifi_config";
constexpr const int NO_CHANGE_FRAME_LIMIT = 10;
constexpr const double CHANGE_THRESHOLD_PERCENT = 0.10;
constexpr const char* INTERFACE_NAME = "wlan0";
constexpr const char* USER_LED_PATH = "/sys/class/leds/led-user";

// YOLO defines
constexpr const char* MODEL_FILE_NAME = "detect.cvimodel";
constexpr const int MODEL_CLASS_CNT = 3;  // underline, highlight, pen
constexpr const double MODEL_THRESH = 0.5;
constexpr const double MODEL_NMS_THRESH = 0.5;
constexpr const int INPUT_FRAME_WIDTH = 320;
constexpr const int INPUT_FRAME_HEIGHT = 320;
constexpr const int MAX_FRAME_WIDTH = 2560;
constexpr const int MAX_FRAME_HEIGHT = 1440;

// Use volatile sig_atomic_t for safe signal flag updates.
volatile sig_atomic_t interrupted = 0;

// Global variables
std::string remoteBaseUrl = "";
cv::VideoCapture cap;
cv::QRCodeDetector qrDecoder;
cvitdl_handle_t tdl_handle = nullptr;
std::string modelFilePath = "";
std::string wifiConfigFilePath = "";

// For http requests
struct HttpResponse {
    std::string body;
    long statusCode;
};

// Utilities
std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos || end == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

void sleepSeconds(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

void interruptHandler(int signum) {
    std::printf("Received signal: %d\n", signum);
    interrupted = 1;
}

bool runSystemCommand(const std::string& command) {
    int status = std::system(command.c_str());
    if (status != 0) {
        std::cerr << "Error executing command: " << command << std::endl;
        return false;
    }
    return true;
}

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

std::string getExecutableDirectory() {
    char buffer[4096];
    ssize_t count = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (count != -1) {
        std::string exePath(buffer, count);
        size_t pos = exePath.find_last_of("/\\");
        if (pos != std::string::npos) {
            return exePath.substr(0, pos);
        }
    }
    return "";
}

// Callback for libcurl to collect HTTP response data.
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

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

// Set user LED on|off
void setUserLEDTrigger(const std::string& trigger) {
    std::ofstream fs(std::string(USER_LED_PATH) + "/trigger");
    if (!fs) {
        std::cerr << "Error: Unable to set trigger" << std::endl;
        return;
    }
    fs << trigger;
    fs.close();
}

// Set User LED brightness 0|1
void setUserLEDBrightness(int brightness) {
    std::ofstream fs(std::string(USER_LED_PATH) + "/brightness");
    if (!fs) {
        std::cerr << "Error: Unable to set brightness" << std::endl;
        return;
    }
    fs << brightness;
    fs.close();
}

// command: on|off|flash, flashInterval 300|1000
void controlUserLED(const char* command, int flashInterval) {
    if (command == "on") {
        setUserLEDTrigger("none"); // Disable any active triggers
        setUserLEDBrightness(1);   // Turn the LED on
    } else if (command == "off") {
        setUserLEDTrigger("none"); // Disable any active triggers
        setUserLEDBrightness(0);   // Turn the LED off
    } else if (command == "flash") {
        setUserLEDTrigger("timer"); // Enable timer trigger for flashing
        std::ofstream fs(std::string(USER_LED_PATH) + "/delay_on");
        fs << flashInterval; // Set ON duration to 500ms
        fs.close();
        fs.open(std::string(USER_LED_PATH) + "/delay_off");
        fs << flashInterval; // Set OFF duration to 500ms
        fs.close();
    }
    else {
        std::cerr << "Invalid command. Use [on|off|flash]." << std::endl;
    }
}

// Flash user led for x times with x internal ms
void flashUserLED(int times, int interval_ms) {
    for (int i = 0; i < times; ++i) {
        controlUserLED("on", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        controlUserLED("off", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

// Clean up resources gracefully.
void cleanUp() {
    cap.release();
    if (tdl_handle != nullptr) {
        CVI_TDL_DestroyHandle(tdl_handle);
        tdl_handle = nullptr;
    }
    curl_global_cleanup();
    controlUserLED("off", 0);
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
    try {
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
    }
    return "";
}

// Open the default camera and warm it up by skipping initial frames.
void openCamera(int width, int height) {
    int retries = 0;
    while (!interrupted) {
        if (cap.isOpened()) {
            break;
        }
        retries ++;
        if (retries > 5) {
            throw std::runtime_error("Failed to open camera after 5 tries");
        }
        cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        cap.open(0);
        if (!cap.isOpened()) {
            std::cerr << "Failed to open camera; retrying in 3 seconds..." << std::endl;
            //sleepSeconds(3);
            flashUserLED(3, 1000);
        } else {
            cv::Mat dummy;
            for (int i = 0; i < 15 && !interrupted; ++i)
                cap >> dummy;
        }
    }
}

// Get currently connected ssid
std::string getConnectedSSID() {
    std::string cmd = "iw dev " + std::string(INTERFACE_NAME) + " link | grep ssid | awk '{print substr($0, index($0,$2))}'";
    char buffer[128];
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Failed to run command" << std::endl;
        return "";
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    // Trim any trailing newline or spaces
    result.erase(result.find_last_not_of(" \n\r\t") + 1);
    return result.empty() ? "" : result;
}

// Connect to WiFi using system commands via wpa_cli
bool connectToWifi(const std::string& ssid, const std::string& password) {
    // Read the existing configuration file if it exists.
    std::ifstream existingConfig("/etc/wpa_supplicant.conf");
    std::string fileContents;
    if (existingConfig) {
        std::stringstream buffer;
        buffer << existingConfig.rdbuf();
        fileContents = buffer.str();
        existingConfig.close();
        // Create search strings to check for the given ssid and password.
        std::string ssidLine = "ssid=\"" + ssid + "\"";
        //std::string pskLine = "psk=\"" + password + "\"";
        // If both the ssid and psk lines are found in the file, do not overwrite.
        if (fileContents.find(ssidLine) != std::string::npos) {
            //try 5 times to connect to ssid
            for (int i = 0; i < 5; i++) {
                if (!getIPAddress().empty()){
                    std::cout << "SSID exists and is connected" << std::endl;
                    return true;
                }
                std::cout << "SSID exists but not connected, retry after 3 seconds" << std::endl;
                flashUserLED(3, 500);
                sleepSeconds(3);
            }
        }
    }
    if (ssid.empty() || password.empty()) {
        return false;
    }
    // Step 1: Create WPA Supplicant Configuration File
    std::ofstream configFile("/etc/wpa_supplicant.conf");
    if (!configFile) {
        std::cerr << "Error: Unable to write /etc/wpa_supplicant.conf" << std::endl;
        return false;
    }
    configFile << "ctrl_interface=/var/run/wpa_supplicant\n"
               << "network={\n"
               << "    ssid=\"" << ssid << "\"\n"
               << "    psk=\"" << password << "\"\n"
               << "    key_mgmt=WPA-PSK\n"
               << "}\n";
    configFile.close();
    // Step 2: Start WPA Supplicant
    std::string cmd = "wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf";
    if (system(cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to start wpa_supplicant" << std::endl;
        return false;
    }
    //try 5 times to connect to wifi
    for (int i = 0; i < 5; i++) {
        if (!getIPAddress().empty()){
            std::cout << "wifi connected" << std::endl;
            return true;
        }
        std::cout << "wifi not connected, retry after 3 seconds" << std::endl;
        flashUserLED(3, 500);
        sleepSeconds(3);
    }
    return false;
}

// Connect to a remote server by sending a ping request.
bool connectToRemote() {
    if (remoteBaseUrl.empty()) {
        printf("no remoteBaseUrl\n");
        return false;
    }
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
            flashUserLED(3, 500);
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
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, modelFilePath.c_str());
    if (ret != CVI_SUCCESS) {
        throw std::runtime_error("Open model failed with error code: " + std::to_string(ret));
    }
}

cv::Mat convertNV21FrameToBGR(const VIDEO_FRAME_INFO_S &stFrameInfo, int output_width, int output_height, bool gray)
{
    const VIDEO_FRAME_S &vf = stFrameInfo.stVFrame;
    // -------------------
    // Map the Y plane
    // -------------------
    CVI_U64 phyaddr_y = vf.u64PhyAddr[0];
    const int stride_y = vf.u32Stride[0];
    const int length_y = vf.u32Length[0];
    void* mapped_ptr_y = CVI_SYS_MmapCache(phyaddr_y, length_y);
    if (!mapped_ptr_y)
        throw std::runtime_error("Failed to map Y plane.");
    // -------------------
    // Map the UV plane
    // -------------------
    CVI_U64 phyaddr_uv = vf.u64PhyAddr[1];
    const int stride_uv = vf.u32Stride[1];
    const int length_uv = vf.u32Length[1];
    void* mapped_ptr_uv = CVI_SYS_MmapCache(phyaddr_uv, length_uv);
    if (!mapped_ptr_uv) {
        CVI_SYS_Munmap(mapped_ptr_y, length_y);
        throw std::runtime_error("Failed to map UV plane.");
    }
    // -------------------
    // Determine cropping offsets.
    // For the Y plane, use the full-resolution offsets.
    // For the UV plane (subsampled vertically by 2), use border_top/2.
    // -------------------
    int border_top   = vf.s16OffsetTop;
    int border_left  = vf.s16OffsetLeft;
    // (We ignore border_bottom and border_right here.)
    // -------------------
    // Allocate a contiguous buffer for the NV21 data.
    // NV21 consists of: 
    //   - Y plane: output_height rows, each output_width bytes.
    //   - UV plane: output_height/2 rows, each output_width bytes.
    // Total size = output_height + output_height/2 rows * output_width.
    // -------------------
    int nv21_rows = output_height + output_height / 2;
    int nv21_size = nv21_rows * output_width;
    unsigned char* nv21_buffer = new unsigned char[nv21_size];
    // Pointer to the destination buffer.
    unsigned char* dst = nv21_buffer;
    // -------------------
    // Copy Y plane:
    // For each row in the cropped Y region:
    //   - The source row starts at mapped_ptr_y plus (border_top + i)*stride_y + border_left.
    //   - Copy output_width bytes.
    // -------------------
    const unsigned char* src_y = static_cast<const unsigned char*>(mapped_ptr_y);
    for (int i = 0; i < output_height; i++)
    {
        const unsigned char* src_row = src_y + (border_top + i) * stride_y + border_left;
        memcpy(dst, src_row, output_width);
        dst += output_width;
    }
    // -------------------
    // Copy UV plane:
    // NV21 has the UV plane at half the vertical resolution.
    // For the cropped region, we use (border_top/2) as the starting row.
    // Each row in the UV plane has output_width bytes.
    // -------------------
    const unsigned char* src_uv = static_cast<const unsigned char*>(mapped_ptr_uv);
    int uv_border_top = border_top / 2; // assuming even values for proper alignment
    for (int i = 0; i < output_height / 2; i++)
    {
        const unsigned char* src_row = src_uv + (uv_border_top + i) * stride_uv + border_left;
        memcpy(dst, src_row, output_width);
        dst += output_width;
    }
    // -------------------
    // Create an OpenCV Mat from the NV21 buffer.
    // The Mat has (output_height + output_height/2) rows and output_width columns.
    // -------------------
    cv::Mat nv21(nv21_rows, output_width, CV_8UC1, nv21_buffer);
    // Convert NV21 to BGR.
    cv::Mat bgr;
    if (gray) {
        cv::cvtColor(nv21, bgr, cv::COLOR_YUV2GRAY_NV21);
    }
    else {
        cv::cvtColor(nv21, bgr, cv::COLOR_YUV2BGR_NV21);
    }
    // Clean up:
    delete[] nv21_buffer;
    CVI_SYS_Munmap(mapped_ptr_y, length_y);
    CVI_SYS_Munmap(mapped_ptr_uv, length_uv);
    return bgr;
}

void sendMat(cv::Mat image) {
    // if (getIPAddress().empty()){
    //     printf("no ip address\n");
    //     return;
    // }
    printf("encoding image\n");
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<uchar> buffer;
    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
    if (!cv::imencode(".jpg", image, buffer, params)) {
        std::cerr << "Failed to encode image." << std::endl;
        return;
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    std::cout << "execution time: " << duration.count() << " seconds" << std::endl;
    // std::vector<int> params = { cv::IMWRITE_WEBP_QUALITY, 90 };
    // if (!cv::imencode(".webp", image, buffer, {})) {
    //     std::cerr << "Failed to encode image to WebP format." << std::endl;
    //     return;
    // }
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize libcurl" << std::endl;
        return;
    }
    printf("sending image now\n");
    start = std::chrono::high_resolution_clock::now();
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
        std::cout << "image sent: " << httpCode << std::endl;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    end = std::chrono::high_resolution_clock::now();
    duration = end - start;
    std::cout << "execution time: " << duration.count() << " seconds" << std::endl;
}

// Capture an image, encode it to JPEG, and send it via HTTP POST.
void sendImage() {
    flashUserLED(2, 150);
    cv::Mat frame;
    std::pair<void*, void*> imagePtrs = cap.capture(frame);
    if (frame.empty()) {
        cap.releaseImagePtr();
        std::cerr << "Captured empty frame!" << std::endl;
        return;
    }
    if (imagePtrs.second == nullptr) {
        printf("sengImage() imagePtrs.second is nullptr\n");
        cap.releaseImagePtr();
        return;
    }
    void* original_image_ptr = imagePtrs.second;
    VIDEO_FRAME_INFO_S *frameInfo = reinterpret_cast<VIDEO_FRAME_INFO_S*>(original_image_ptr);
    if (frameInfo == nullptr) {
        std::cerr << "sendImage() frameInfo is nullptr" << std::endl;
        cap.releaseImagePtr();
        original_image_ptr = nullptr;
        return;
    }
    // Convert the NV21 frame to BGR cv::Mat.
    printf("converting frame info to bgr\n");
    auto start = std::chrono::high_resolution_clock::now();
    cv::Mat image = convertNV21FrameToBGR(*frameInfo, MAX_FRAME_WIDTH, MAX_FRAME_HEIGHT, false);
    if (image.empty()) {
        std::cerr << "sendImage() image is empty" << std::endl;
        cap.releaseImagePtr();
        original_image_ptr = nullptr;
        return;
    }
    cap.releaseImagePtr();
    original_image_ptr = nullptr;
    printf("conversion is done\n");
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    // Print the duration in seconds
    std::cout << "execution time: " << duration.count() << " seconds" << std::endl;
    sendMat(image);
}

// Setup before running main logics
void setup() {
    //start connections
    std::string ssid, password;
    bool isConnected = false;
    std::ifstream file(wifiConfigFilePath);
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
        isConnected = connectToWifi(ssid, password);
        if (isConnected) {
            printf("connected to wifi\n");
            isConnected = connectToRemote();
            if (isConnected) {
                printf("connected to remote\n");
            }
        }
    }
    // If not connected, scan for QR code to get credentials.
    if (!isConnected) {
        //controlUserLED("flash", 250);
        openCamera(INPUT_FRAME_WIDTH, INPUT_FRAME_HEIGHT);
        while (!interrupted) {
            std::string qrContent = detectQR();
            if (qrContent.empty()) {
                flashUserLED(6, 250);
                //sleepSeconds(3);
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
    std::ofstream newFile(wifiConfigFilePath, std::ios::trunc);
    if (newFile.is_open()) {
        newFile << wifiConfig;
        newFile.close();
    } else {
        //no need to treat it as error since all 3 variables are ready
        std::cerr << "Unable to open file for writing: " << wifiConfigFilePath << std::endl;
    }
    flashUserLED(6, 150);
    controlUserLED("off", 0);
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
        //capture() method will set image_ptr and original_image_ptr
        std::pair<void*, void*> imagePtrs = cap.capture(img);
        if (img.empty()) {
            printf("loop img is empty\n");
            cap.releaseImagePtr();
            continue;
        }
        if (imagePtrs.first == nullptr) {
            std::cerr << "main.cpp imagePtrs.first is nullptr" << std::endl;
            cap.releaseImagePtr();
            continue;
        }
        void* image_ptr = imagePtrs.first;
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
            CVI_TDL_Detection(tdl_handle, frameInfo, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, &obj_meta);
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
    modelFilePath = getExecutableDirectory() + "/" + std::string(MODEL_FILE_NAME);
    wifiConfigFilePath = getExecutableDirectory() + "/" + std::string(WIFI_CONFIG_FILE_NAME);
    controlUserLED("on", 0);
    if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        std::cerr << "curl_global_init() failed" << std::endl;
        return -1;
    }
    signal(SIGINT, interruptHandler);
    try {
        setup();
        loop();
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
