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

## CLEAN MAKE

cd ../
cmake --build <build-dir> --target clean
rm -r <build dir>
