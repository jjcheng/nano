## Cross compile in ubuntu

### Build custom opencv mobile

export ROOT_PATH=/root/Development/nano
cd <folder path>
./build-opencv-mobile.sh

1. mkdir libs
2. cd libs
3. wget https://github.com/nihui/opencv-mobile/releases/latest/download/opencv-mobile-4.10.0.zip
4. unzip opencv-mobile-4.10.0.zip
5. rm opencv-mobile-4.10.0.zip
6. cd opencv-mobile-4.10.0
7. patch -p1 -i ../../files/opencv-4.10.0-no-atomic.patch
8. cd ../
9. cp ../files/options.txt opencv-mobile-4.10.0/options.txt
10. cp ../files/capture_cvi.cpp opencv-mobile-4.10.0/modules/highgui/src/capture_cvi.cpp
11. cp ../files/aruco_board.cpp opencv-mobile-4.10.0/modules/objdetect/src/aruco/aruco_board.cpp
12. cp ../files/aruco_utils.cpp opencv-mobile-4.10.0/modules/objdetect/src/aruco/aruco_utils.cpp
13. cp ../files/cascadedetect.cpp opencv-mobile-4.10.0/modules/objdetect/src/cascadedetect.cpp
14. cp ../files/cascadedetect.hpp opencv-mobile-4.10.0/modules/objdetect/src/cascadedetect.hpp
15. cp ../files/chessboard.cpp opencv-mobile-4.10.0/modules/calib3d/src/chessboard.cpp
16. cp ../files/hog.cpp opencv-mobile-4.10.0/modules/objdetect/src/hog.cpp
17. cp ../files/matchers.cpp opencv-mobile-4.10.0/modules/features2d/src/matchers.cpp
18. cp ../files/objdetect.hpp opencv-mobile-4.10.0/modules/objdetect/include/opencv2/objdetect.hpp
19. wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz
20. tar xvf host-tools.tar.gz
21. rm host-tools.tar.gz
22. sed -i '9s/^/\/\//' host-tools/gcc/riscv64-linux-musl-x86_64/sysroot/usr/include/sys/ioctl.h
23. wget https://raw.githubusercontent.com/nihui/opencv-mobile/refs/heads/master/toolchains/riscv64-unknown-linux-musl.toolchain.cmake
24. export RISCV_ROOT_PATH=/Users/jj/Desktop/Development/nano/libs/host-tools/gcc/riscv64-linux-musl-x86_64
25. cd opencv-mobile-4.10.0
26. mkdir build
27. cd build
28. cmake \
    -DCMAKE_TOOLCHAIN_FILE=../riscv64-unknown-linux-musl.toolchain.cmake \
    -DCMAKE_C_FLAGS="-fno-rtti -fno-exceptions" \
    -DCMAKE_CXX_FLAGS="-fno-rtti -fno-exceptions" \
    -DCMAKE_INSTALL_PREFIX=install \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    `cat ../options.txt` \
    -DBUILD_opencv_world=OFF \
    -DOPENCV_DISABLE_FILESYSTEM_SUPPORT=ON ..
29. make -j18
30. make install

## clean make

cmake --build <build-dir> --target clean
rm -r <build dir>
