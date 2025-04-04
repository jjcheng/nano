#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>
#include <opencv2/opencv.hpp>
#include <cstring>
#include <iostream>

int main() {
    const char* device = "/dev/video0";
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Error opening device");
        return 1;
    }

    // Query device capabilities
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return 1;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::cerr << "Device does not support video capture" << std::endl;
        close(fd);
        return 1;
    }

    // Set format to YUYV 640x480
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        close(fd);
        return 1;
    }

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        std::cerr << "Failed to set YUYV format" << std::endl;
        close(fd);
        return 1;
    }

    // Request buffers
    struct v4l2_requestbuffers req = {};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return 1;
    }

    // Map buffer
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
        perror("VIDIOC_QUERYBUF");
        close(fd);
        return 1;
    }

    void* buffer = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    // Queue buffer and start streaming
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // Wait for frame
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {2, 0}; // 2-second timeout
    int r = select(fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) {
        std::cerr << "Timeout or error waiting for frame" << std::endl;
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // Dequeue buffer
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // Convert YUYV to BGR using OpenCV
    cv::Mat yuyv_image(fmt.fmt.pix.height, fmt.fmt.pix.width, CV_8UC2, buffer);
    cv::Mat bgr_image;
    cv::cvtColor(yuyv_image, bgr_image, cv::COLOR_YUV2BGR_YUYV);

    // Save image using OpenCV
    if (!cv::imwrite("bgr.jpg", bgr_image)) {
        std::cerr << "Failed to save image" << std::endl;
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // Cleanup
    munmap(buffer, buf.length);
    close(fd);

    std::cout << "Image saved as bgr.jpg" << std::endl;
    return 0;
}