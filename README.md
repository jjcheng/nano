## CROSS COMPILE IN UBUNTU

### OPENCV MOBILE

export ROOT_PATH=/root/Development/nano
chmod +x build-opencv-mobile.sh
apt install zip
apt install cmake
./build-opencv-mobile.sh

## BUILD MAIN

chmod +x build-main.sh
./build-main.sh

## DEBUG MAIN ON HOST MACHINE

g++ -std=c++11 -g main.cpp \
-Ilibs/cvitek_tdl_sdk/include -Ilibs/cvitek_tdl_sdk/include/cvi_tdl \
-Ilibs/cvitek_tdl_sdk/sample/3rd/middleware/v2/include \
-Ilibs/cvitek_tdl_sdk/sample/3rd/middleware/v2/include/linux \
-o main `pkg-config --cflags --libs opencv4`

## CONVERT YOLO MODEL

https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/maixcam.html

1. sudo apt-get update
2. sudo apt install curl apt-transport-https ca-certificates software-properties-common -y
3. curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /usr/share/keyrings/docker-archive-keyring.gpg
4. echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/docker-archive-keyring.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
5. sudo apt update
6. sudo apt install docker-ce -y
7. sudo systemctl status docker
8. docker pull sophgo/tpuc_dev:latest
9. mkdir data
10. cd data
11. docker run --privileged --name tpu-env -v /home/$USER/data:/home/$USER/data -it sophgo/tpuc_dev
12. [subsequent start] docker start tpu-env && docker attach tpu-env
13. wget https://github.com/sophgo/tpu-mlir/releases/download/v1.16/tpu_mlir-1.16-py3-none-any.whl
14. pip install tpu_mlir-1.16-py3-none-any.whl
15. scp /Users/jj/Desktop/Development/nano/files/convert-yolo11.sh root@<server ip>:/home/root/data/
16. scp ~/Desktop/test.jpg root@<server ip>:/home/root/data/
17. scp ~/Desktop/images.zip root@<server ip>:/home/root/data/
18. scp ~/Desktop/yolo11s.onnx root@<server ip>:/home/root/data/
19. chmod +x convert-yolo11.sh
20. ./convert-yolo11.sh
21. cd workspace
22. scp root@47.84.46.173:/home/root/data/workspace/yolo11s_int8.cvimodel ~/Desktop/
23. scp root@47.84.46.173:/home/root/data/workspace/yolo11s_bf16.cvimodel ~/Desktop/

## CLEAN MAKE

cd ../
cmake --build <build-dir> --target clean
rm -r <build dir>
