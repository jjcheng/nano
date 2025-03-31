#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <cstring>

class V4L2Camera {
public:
    V4L2Camera(const std::string& device) : device_path(device), fd(-1), buffer(nullptr) {}
    ~V4L2Camera() { stop(); }

    bool start() {
        fd = open(device_path.c_str(), O_RDWR);
        if (fd < 0) {
            perror("Failed to open video device");
            return false;
        }

        struct v4l2_format format = {};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = 640;
        format.fmt.pix.height = 480;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        
        if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            perror("Failed to set format");
            return false;
        }

        struct v4l2_requestbuffers req = {};
        req.count = 1;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        
        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
            perror("Failed to request buffer");
            return false;
        }

        struct v4l2_buffer buffer_info = {};
        buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer_info.memory = V4L2_MEMORY_MMAP;
        buffer_info.index = 0;
        
        if (ioctl(fd, VIDIOC_QUERYBUF, &buffer_info) < 0) {
            perror("Failed to query buffer");
            return false;
        }

        buffer = mmap(nullptr, buffer_info.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer_info.m.offset);
        if (buffer == MAP_FAILED) {
            perror("Failed to mmap buffer");
            return false;
        }

        if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
            perror("Failed to queue buffer");
            return false;
        }

        int type = buffer_info.type;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            perror("Failed to start stream");
            return false;
        }

        return true;
    }

    bool captureFrame() {
        struct v4l2_buffer buffer_info = {};
        buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer_info.memory = V4L2_MEMORY_MMAP;
        buffer_info.index = 0;

        if (ioctl(fd, VIDIOC_DQBUF, &buffer_info) < 0) {
            perror("Failed to dequeue buffer");
            return false;
        }

        // Process frame (buffer contains the image data)
        std::cout << "Captured frame of size: " << buffer_info.bytesused << " bytes" << std::endl;

        if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
            perror("Failed to requeue buffer");
            return false;
        }

        return true;
    }

    void stop() {
        if (fd >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd, VIDIOC_STREAMOFF, &type);
            munmap(buffer, 640 * 480 * 2);
            close(fd);
        }
    }

private:
    std::string device_path;
    int fd;
    void* buffer;
};

int main() {
    V4L2Camera camera("/dev/video0");
    if (!camera.start()) {
        return -1;
    }

    for (int i = 0; i < 10; ++i) {
        if (!camera.captureFrame()) {
            break;
        }
    }
    return 0;
}
