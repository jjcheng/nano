## CROSS COMPILE IN UBUNTU

### OPENCV MOBILE

### clone the nano project, go cd into it

export ROOT_PATH=/root/Development/nano
chmod +x build-opencv-mobile.sh
apt install zip
apt install cmake
./build-opencv-mobile.sh

## BUILD MAIN

chmod +x build-main.sh
./build-main.sh

## DEBUG MAIN ON HOST MACHINE

apt install pkgconf
apt install libopencv-dev

## DEBUG WITH CVI_TEK AS IT IS NOT COMPATIBLE

g++ -std=c++11 -g main-debug.cpp -o main-debug `pkg-config --cflags --libs opencv4`

g++ -std=c++11 -g main.cpp \
-Ilibs/cvitek_tdl_sdk/include \
-Ilibs/cvitek_tdl_sdk/include/cvi_tdl \
-Ilibs/cvitek_tdl_sdk/sample/3rd/middleware/v2/include \
-Ilibs/cvitek_tdl_sdk/sample/3rd/middleware/v2/include/linux \
-Ilibs/cvitek_tdl_sdk/sample/3rd/middleware/v2/lib \
-Ilibs/cvitek_tdl_sdk/sample/3rd/middleware/v2/lib/3rd \
-Ilibs/cvitek_tdl_sdk/sample/3rd/opencv/lib \
-Ilibs/cvitek_tdl_sdk/sample/3rd/tpu/lib \
-Ilibs/cvitek_tdl_sdk/sample/3rd/ive/lib \
-Ilibs/cvitek_tdl_sdk/lib \
-Ilibs/cvitek_tdl_sdk/sample/3rd/lib \
-Llibs//cvitek_tdl_sdk/sample/3rd/middleware/v2/lib \
-Llibs/cvitek_tdl_sdk/sample/3rd/middleware/v2/lib/3rd \
-lini -lsns_full -lsample -lisp -lvdec -lvenc -lawb -lae -laf -lcvi_bin -lcvi_bin_isp -lmisc -lisp_algo -lsys -lvpu \
-Llibs/cvitek_tdl_sdk/sample/3rd/opencv/lib \
-lopencv_core -lopencv_imgproc -lopencv_imgcodecs \
-Llibs/cvitek_tdl_sdk/sample/3rd/tpu/lib \
-lcnpy -lcvikernel -lcvimath -lcviruntime -lz -lm \
-Llibs/cvitek_tdl_sdk/sample/3rd/ive/lib \
-lcvi_ive_tpu \
-Llibs/cvitek_tdl_sdk/lib \
-lcvi_tdl \
-Llibs/cvitek_tdl_sdk/sample/3rd/lib \
-lpthread -latomic \
-o main `pkg-config --cflags --libs opencv4`

## CONVERT YOLO MODEL

https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/maixcam.html

export IP=<ubuntu IP address>

1. sudo apt-get update
2. sudo apt-get install apt-transport-https ca-certificates curl gnupg-agent software-properties-common
3. curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
4. sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
5. sudo apt update
6. sudo apt-get install docker-ce docker-ce-cli containerd.io
7. docker pull sophgo/tpuc_dev:latest
8. docker run --privileged --name tpu-env -v /home/data:/home/data -it sophgo/tpuc_dev
9. [subsequent start] docker start tpu-env && docker attach tpu-env
10. cd /home/data
11. wget https://github.com/sophgo/tpu-mlir/releases/download/v1.16/tpu_mlir-1.16-py3-none-any.whl
12. pip install tpu_mlir-1.16-py3-none-any.whl
13. scp files/convert-model.sh root@$IP:/home/data/
14. scp ~/Desktop/test.jpg root@$IP:/home/data/
15. scp ~/Desktop/images.zip root@$IP:/home/data/
16. scp ~/Desktop/convert.onnx root@$IP:/home/data/
17. chmod +x convert-model.sh
18. ./convert-model.sh
19. cd workspace
20. scp root@$IP:/home/data/workspace/detect_int8.cvimodel ~/Desktop/
21. scp root@$IP:/home/data/workspace/detect_bf16.cvimodel ~/Desktop/

## CLEAN MAKE

cd ../
cmake --build <build-dir> --target clean
rm -r <build dir>
