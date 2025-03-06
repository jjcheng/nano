mkdir libs
cd libs
echo "downloading opencv mobile source code"
wget https://github.com/nihui/opencv-mobile/releases/latest/download/opencv-mobile-4.10.0.zip
unzip opencv-mobile-4.10.0.zip
rm opencv-mobile-4.10.0.zip
cd opencv-mobile-4.10.0
echo "patching opencv-4.10.0-no-atomic.patch"
patch -p1 -i ../../files/opencv-4.10.0-no-atomic.patch
echo "replacing files"
cd ../
cp ../files/options.txt opencv-mobile-4.10.0/options.txt
cp ../files/capture_cvi.cpp opencv-mobile-4.10.0/modules/highgui/src/capture_cvi.cpp
cp ../files/aruco_board.cpp opencv-mobile-4.10.0/modules/objdetect/src/aruco/aruco_board.cpp
cp ../files/aruco_utils.cpp opencv-mobile-4.10.0/modules/objdetect/src/aruco/aruco_utils.cpp
cp ../files/cascadedetect.cpp opencv-mobile-4.10.0/modules/objdetect/src/cascadedetect.cpp
cp ../files/cascadedetect.hpp opencv-mobile-4.10.0/modules/objdetect/src/cascadedetect.hpp
cp ../files/chessboard.cpp opencv-mobile-4.10.0/modules/calib3d/src/chessboard.cpp
cp ../files/hog.cpp opencv-mobile-4.10.0/modules/objdetect/src/hog.cpp
cp ../files/matchers.cpp opencv-mobile-4.10.0/modules/features2d/src/matchers.cpp
cp ../files/objdetect.hpp opencv-mobile-4.10.0/modules/objdetect/include/opencv2/objdetect.hpp
echo "downloading host-tools"
wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz
tar xvf host-tools.tar.gz
rm host-tools.tar.gz
echo "patching ioctl.h"
sed -i '9s/^/\/\//' host-tools/gcc/riscv64-linux-musl-x86_64/sysroot/usr/include/sys/ioctl.h
echo "downloading toolchain.cmake"
wget https://raw.githubusercontent.com/nihui/opencv-mobile/refs/heads/master/toolchains/riscv64-unknown-linux-musl.toolchain.cmake
export RISCV_ROOT_PATH=$ROOT_PATH/libs/host-tools/gcc/riscv64-linux-musl-x86_64
cd opencv-mobile-4.10.0
mkdir build
cd build
echo "cmaking"
cmake \
    -DCMAKE_TOOLCHAIN_FILE=../riscv64-unknown-linux-musl.toolchain.cmake \
    -DCMAKE_C_FLAGS="-fno-rtti -fno-exceptions" \
    -DCMAKE_CXX_FLAGS="-fno-rtti -fno-exceptions" \
    -DCMAKE_INSTALL_PREFIX=install \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    `cat ../options.txt` \
    -DBUILD_opencv_world=OFF \
    -DOPENCV_DISABLE_FILESYSTEM_SUPPORT=ON ..
make -j8
echo "making"
make install