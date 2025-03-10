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
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#define BUFFER_SIZE 4096
#define WIFI_CONFIG_FILE_PATH "/tmp/wifi_config"
#define SAVE_IMAGE_PATH "/tmp/captured.jpg"
#define CONF_THRESHOLD 0.5
#define IOU_THRESHOLD 0.5
#define NO_CHANGE_FRAME_LIMIT 30
#define CHANGE_THRESHOLD_PERCENT 0.10

// Global variables
std::string wifiSSID = "";
std::string wifiPassword = "";
std::string remoteBaseUrl = "";
std::string myIp = "";

// Use sig_atomic_t for safe signal handling
volatile sig_atomic_t interrupted = 0;

// Forward declarations
std::string getIPAddress();
void sendImage();
bool runCommand(const std::string& command);
bool httpGetRequest(const std::string &host, const std::string &path);
void setWifiCredentialFromText(const std::string& text);
void cleanUp();
void connectToDevice();
void loop();

// Signal handler: only sets the flag.
void interruptHandler(int signum) {
    std::printf("Signal: %d\n", signum);
    interrupted = 1;
}

// Clean up resources before exit.
void cleanUp() {
    std::printf("Exiting...\n");
    exit(0);
}

// Read WiFi config from file and set credentials.
void setWifiCredentials() {
    std::ifstream file(WIFI_CONFIG_FILE_PATH);
    //if no such file, return
    if (!file.good()) {
        std::printf("no wifi_config file\n");
        return;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    std::printf("wifi_config: %s\n", text);
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

// Connect to WiFi using system commands.
void connectToWifi() {
    std::cout << "Connecting to " << wifiSSID << std::endl;
    std::string configCmd = "echo 'network={\n    ssid=\"" + wifiSSID + "\"\n    psk=\"" + wifiPassword + "\"\n}' > /etc/wpa_supplicant.conf";
    if (!runCommand(configCmd) ||
        !runCommand("ifconfig wlan0 up") ||
        !runCommand("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf") ||
        !runCommand("udhcpc -i wlan0")) {
        wifiSSID = "";
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
    // Read WiFi credentials and remote base URL.
    setWifiCredentials();
    // Connect to WiFi and remote server.
    connectToWifi();
    // Connect to device using wifi
    connectToDevice();
    return 0;
}
