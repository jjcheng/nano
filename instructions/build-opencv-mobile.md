## Cross compile in ubuntu

### Build custom opencv mobile

1. mkdir libs
2. cd libs
3. wget https://github.com/nihui/opencv-mobile/releases/latest/download/opencv-mobile-4.10.0.zip
4. unzip opencv-mobile-4.10.0.zip
5. rm opencv-mobile-4.10.0.zip
6. cd opencv-mobile-4.10.0
7. patch -p1 -i ../../patches/opencv-4.10.0-no-atomic.patch
8. patch -p1 -i ../../patches/pins.patch
9. cd ../
10. cp ../patches/options.txt opencv-mobile-4.10.0/options.txt
11. wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz
12. tar xvf host-tools.tar.gz
13. rm host-tools.tar.gz
14. sed -i '9s/^/\/\//' host-tools/gcc/riscv64-linux-musl-x86_64/sysroot/usr/include/sys/ioctl.h
15. wget https://raw.githubusercontent.com/nihui/opencv-mobile/refs/heads/master/toolchains/riscv64-unknown-linux-musl.toolchain.cmake
16. export RISCV_ROOT_PATH=/Users/jj/Desktop/Development/nano/libs/host-tools/gcc/riscv64-linux-musl-x86_64
17. cd opencv-mobile-4.10.0
18. mkdir build
19. cd build
20. cmake \
    -DCMAKE_TOOLCHAIN_FILE=../riscv64-unknown-linux-musl.toolchain.cmake \
    -DCMAKE_C_FLAGS="-fno-rtti -fno-exceptions" \
    -DCMAKE_CXX_FLAGS="-fno-rtti -fno-exceptions" \
    -DCMAKE_INSTALL_PREFIX=install \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    `cat ../options.txt` \
    -DBUILD_opencv_world=OFF \
    -DOPENCV_DISABLE_FILESYSTEM_SUPPORT=ON ..
21. make -j18
22. fix all the UMat errors
23. make install

## Build main app

1. mkdir build
2. cd build
3. export COMPILER=/Users/jj/Desktop/Development/host-tools/gcc/riscv64-linux-musl-x86_64/bin
4. cmake ..
5. make
6. scp Jotter root@<nano ip address>:/root

## clean make

cmake --build <build-dir> --target clean
rm -r <build dir>
