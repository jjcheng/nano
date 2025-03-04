## CROSS COMPILE IN UBUNTU

### OPENCV MOBILE

export ROOT_PATH=/root/Development/nano
chmod +x build-opencv-mobile.sh
apt install zip
apt install cmake
./build-opencv-mobile.sh

## BUILD MAIN

1. cd $ROOT_PATH
2. tar xvzf files/cvitek_tdl_sdk_1228.tar.gz -C libs
3. export COMPILER=$ROOT_PATH/libs/host-tools/gcc/riscv64-linux-musl-x86_64/bin
4. export SDK_PATH=$ROOT_PATH/libs
5. mkdir build
6. cd build
7. cmake ..
8. make
9. cd install

## COPY FILE TO HOST MACHINE

scp root@<nano ip address>:/root/Development/nano/build/bin/Jotter ~/Desktop

## DEBUG MAIN ON HOST MACHINE

1. g++ -std=c++11 -g -o main main.cpp $(pkg-config --cflags --libs opencv4)

## CONVERT YOLO MODEL

https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/maixcam.html

1. sudo apt-get update
2. sudo apt-get install apt-transport-https ca-certificates curl gnupg-agent software-properties-common
3. curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
4. sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
5. sudo apt-get update
6. sudo apt-get install docker-ce docker-ce-cli containerd.io
7. docker pull sophgo/tpuc_dev:latest
8. mkdir data
9. cd data
10. docker run --privileged --name tpu-env -v /home/$USER/data:/home/$USER/data -it sophgo/tpuc_dev
11. [subsequent start] docker start tpu-env && docker attach tpu-env
12. wget https://github.com/sophgo/tpu-mlir/releases/download/v1.16/tpu_mlir-1.16-py3-none-any.whl
13. pip install tpu_mlir-1.16-py3-none-any.whl
14. copy files/convert-yolo11.sh into container's data folder
15. copy test.jpg (random test image from training data) to container's data folder
16. copy images folder (from training folder) to container's data folder
17. chmod +x convert-yolo11.sh
18. ./convert-yolo11.sh
19. cd workspace
20. scp yolo11s_bf16.cvimodel root@<board ip>:/root/detect_yolo11s_model.cvimodel
21. look for /data/workspace yolo11s_bf16.cvimodel and detect_yolo11s.cvimodel

## CLEAN MAKE

cd ../
cmake --build <build-dir> --target clean
rm -r <build dir>
