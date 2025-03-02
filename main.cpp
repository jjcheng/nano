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

#define BUFFER_SIZE 4096
#define WIFI_CONFIG_FILE_PATH "/root/wifi_config"
#define MODEL_FILE_PATH "/root/models/model.mud"
#define SAVE_IMAGE_PATH "/root/captured.jpg"
#define CONF_THRESHOLD 0.5
#define IOU_THRESHOLD 0.5
#define NO_CHANGE_FRAME_LIMIT 30
#define CHANGE_THRESHOLD_PERCENT 0.10

std::string wifiSSID = "";
std::string wifiPassword = "";
std::string remoteBaseUrl = "";
std::string myIp = "";
cv::VideoCapture cap;

volatile uint8_t interrupted = 0;

// Signal handler to update the interrupted flag
void interrupt_handler(int signum) {
    printf("Signal: %d\n", signum);
    interrupted = 1;
}

// Forward declarations for functions called before they are defined
bool runCommand(const std::string& command);
bool http_get_request(const std::string &host, const std::string &path);
void setWifiCredentialFromText(const std::string& text);
std::string detectQR();
void openCamera();

// Read wifi config from file and set credentials
void setWifiConfidentials() {
    std::ifstream file(WIFI_CONFIG_FILE_PATH);
    if (!file.good()) {
        return;
    }
    // Read entire file content into a string
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    file.close();
    setWifiCredentialFromText(text);
}

// Parse wifi credentials from text and write to file
void setWifiCredentialFromText(const std::string& text) {
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {  // Read line by line
        size_t pos = line.find(':');  // Find the first ':'
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            if (key == "ssid") {
                wifiSSID = value;
            } else if (key == "password") {
                wifiPassword = value;
            } else {
                remoteBaseUrl = value;
            }
        }
    }
    std::cout << "SSID: " << wifiSSID << std::endl;
    std::cout << "PASSWORD: " << wifiPassword << std::endl;
    std::cout << "REMOTEBASEURL: " << remoteBaseUrl << std::endl;
    
    // Prepare configuration text
    std::string wifiConfig = "ssid:" + wifiSSID + "\npassword:" + wifiPassword + "\nremoteBaseUrl:" + remoteBaseUrl;
    
    // Create the config file if it doesn't exist
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
    
    // Write updated configuration to file using C-style I/O
    FILE *fp = fopen(WIFI_CONFIG_FILE_PATH, "w");
    if (!fp) {
        std::cerr << "Failed to open file: " << WIFI_CONFIG_FILE_PATH << std::endl;
        return;
    }
    fwrite(wifiConfig.c_str(), 1, wifiConfig.size(), fp);
    fclose(fp);
}

// Use the camera to scan for a QR code and set wifi credentials from its content
void getWifiQR() {
    openCamera();
    while (wifiSSID == "") {
        std::cout << "Detecting QR code for wifi" << std::endl;
        std::string content = detectQR();
        if (content == "") {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        std::cout << content << std::endl;
        setWifiCredentialFromText(content);
    }
}

// Detect and decode a QR code from the current camera frame
std::string detectQR() {
    printf("Start CSIRead and init QR code detector\n");
    cv::QRCodeDetector qrDecoder;
    printf("QRCode decoder initialized\n");
    cv::Mat bgr;
    cap >> bgr;
    cv::Mat bbox, rectifiedImage;
    std::string data = qrDecoder.detectAndDecode(bgr, bbox, rectifiedImage);
    return data;
}

// Open the default camera and skip a few frames to allow it to warm up
void openCamera() {
    if (!cap.isOpened()) {
        std::cout << "Opening camera" << std::endl;
        cap.open(0); // Open the default camera
        if (cap.isOpened()) {
            cv::Mat bgr;
            for (int i = 0; i < 30; i++) {
                cap >> bgr;
            }
        }
    }
    if (cap.isOpened()) {
        std::cout << "Camera opened successfully!" << std::endl;
    } else {
        std::cerr << "Camera open failed!" << std::endl;
        sleep(3);
        openCamera();  // Retry opening the camera
    }
}

// Set camera resolution. If max is true, set to high resolution; otherwise, set to low resolution.
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

// Connect to the WiFi using system commands and update the IP address.
void connectToWifi() {
    std::cout << "Connecting to " << wifiSSID << std::endl;
    std::string configCmd = "echo 'network={\n    ssid=\"" + wifiSSID + "\"\n    psk=\"" + wifiPassword + "\"\n}' > /etc/wpa_supplicant.conf";
    if (!runCommand(configCmd)) {
        wifiSSID = "";
        getWifiQR();
        return;
    }
    if (!runCommand("ifconfig wlan0 up")) {
        wifiSSID = "";
        getWifiQR();
        return;
    }
    if (!runCommand("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf")) {
        wifiSSID = "";
        getWifiQR();
        return;
    }
    if (!runCommand("udhcpc -i wlan0")) {
        wifiSSID = "";
        getWifiQR();
        return;
    }
    myIp = getIPAddress();
    std::cout << "Connected to " << wifiSSID << " with IP " << myIp << std::endl;
}

// Get the IP address of interface "wlan0"
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

// Connect to the remote server by pinging a URL; retry on failure.
void connectToRemote() {
    std::string url = remoteBaseUrl + "/ping?id=" + myIp;
    std::cout << "Connecting to remote URL " << url << std::endl;
    if (!http_get_request(remoteBaseUrl, "/ping?id=" + myIp)) {
        std::cerr << "Failed to connect to remote" << std::endl;
        if (getIPAddress() == "") {
            connectToWifi();
        }
        sleep(3);
        connectToRemote();
    } else {
        std::cout << "Successfully connected to remote" << std::endl;
    }
}

// Run a system command and return true if it executes successfully.
bool runCommand(const std::string& command) {
    int status = system(command.c_str());
    if (status != 0) {
        std::cerr << "Error executing: " << command << std::endl;
        return false;
    }
    return true;
}

// Make an HTTP GET request to the specified host and path.
bool http_get_request(const std::string &host, const std::string &path) {
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
    while ((bytesReceived = recv(sockfd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        std::cout << buffer;
    }
    close(sockfd);
    return true;
}

// Main processing loop: compares frames and triggers sending image if change is detected.
void loop() {
    int count = 0;
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
            changedThreshold = int(img.cols * img.rows * CHANGE_THRESHOLD_PERCENT);
        }
        cv::Mat frame;
        cv::cvtColor(img, frame, cv::COLOR_BGR2GRAY);
        if (previousNoChangeFrame.empty()) {
            previousNoChangeFrame = frame;
            continue;
        }
        cv::Mat diff;
        cv::absdiff(frame, previousNoChangeFrame, diff);
        cv::Mat thresh;
        cv::threshold(diff, thresh, 30, 255, cv::THRESH_BINARY);
        int nonZeroCount = cv::countNonZero(thresh);
        if (nonZeroCount < changedThreshold) {
            noChangeCount++;
            if (noChangeCount > NO_CHANGE_FRAME_LIMIT) {
                continue;
            } else if (noChangeCount == NO_CHANGE_FRAME_LIMIT) {
                count++;
                std::cout << "No significant change" << std::endl;
                if (detect(img)) {
                    // Trigger sending image if detection returns true
                    // (Note: detect() currently always returns false)
                    sendImage();
                }
            }
        } else {
            int percent = int(float(nonZeroCount) / float(img.cols * img.rows) * 100);
            std::cout << "Change detected: " << percent << "%" << std::endl;
            previousNoChangeFrame = frame;
            noChangeCount = 0;
        }
    }
    cap.release();
}

// Dummy detect function (currently always returns false)
bool detect(cv::Mat image) {
    return false;
}

// Upload an image by saving it, encoding it to JPEG, and sending via HTTP POST.
void sendImage() {
    std::cout << "Sending image to remote" << std::endl;
    if (!setCameraResolution(true)) {
        return;
    }
    cv::Mat frame;
    cap >> frame;
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
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    // NOTE: inet_addr expects an IP address string. Ensure remoteBaseUrl is an IP address.
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

// Sleep for a given number of seconds (wrapper for std::this_thread::sleep_for)
void sleep(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

int main() {
    signal(SIGINT, interrupt_handler);

    // Read wifi credentials and remote base URL from saved file
    setWifiConfidentials();
    // If no SSID is set, scan QR code
    getWifiQR();
    // Connect to wifi and remote server
    connectToWifi();
    connectToRemote();
    // Start the main processing loop
    loop();
    return 0;
}
