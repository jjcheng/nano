## Cross compile opencv mobile with object detect

1. cd ~/Desktop/Development
2. mkdir opencv-mobile
3. cd opencv-mobile
4. wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz
5. tar xvf host-tools.tar.gz
6. wget https://github.com/nihui/opencv-mobile/blob/master/toolchains/riscv64-unknown-linux-musl.toolchain.cmake
7. wget https://github.com/nihui/opencv-mobile/releases/latest/download/opencv-mobile-4.10.0.zip
8. unzip opencv-mobile-4.10.0 -d ~/Desktop/Development/opencv-mobile
9. cd opencv-mobile-4.10.0
10. open options.txt, edit
    -DBUILD_opencv_calib3d=ON
    -DBUILD_opencv_flann=ON
    -DBUILD_opencv_objdetect=ON
    -DWITH_OPENMP=OFF
11. export RISCV_ROOT_PATH=~/Desktop/Development/host-tools/gcc/riscv64-linux-musl-x86_64
12. mkdir build
13. cd build
14. cmake \
    -DCMAKE_TOOLCHAIN_FILE=~/Desktop/Development/opencv-mobile/riscv64-unknown-linux-musl.toolchain.cmake \
    -DCMAKE_C_FLAGS="-fno-rtti -fno-exceptions" \
    -DCMAKE_CXX_FLAGS="-fno-rtti -fno-exceptions" \
    -DCMAKE_INSTALL_PREFIX=install \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    `cat ~/Desktop/Development/opencv-mobile/opencv-mobile-4.10.0/options.txt` \
    -DBUILD_opencv_world=OFF \
    -DOPENCV_DISABLE_FILESYSTEM_SUPPORT=ON ..
15. make -j4
16. fix all the UMat errors
17. make install
18. cd install
19. check for the folders: bin, include, lib, share
20. cd ~/Desktop/Development/opencv-mobile
21. mkdir test
22. cd test
23. mkdir libs
24. cd libs
25. mkdir opencv-mobile-4.10.0-build
26. cd ~/Desktop/Development/opencv-mobile/opencv-mobile-4.10.0/build/install
27. cp -R \* ~/Desktop/Development/opencv-mobile/test/libs/opencv-mobile-4.10.0-build
28. copy main.cpp, CMakeLists.txt to ~/Desktop/Development/opencv-mobile/test
29. export COMPILER=~/Desktop/Development/host-tools/gcc/riscv64-linux-musl-x86_64/bin
30. edit CMakeLists.txt set(OpenCV_DIR "~/Desktop/Development/opencv-mobile/test/libs/opencv-mobile-4.10.0-build/lib/cmake/opencv4")
31. cd ~/Desktop/Development/opencv-mobile/test/
32. mkdir build
33. cd build
34. cmake ..
35. make
36. scp XXX root@<nano ip address>:/root

## #to clean make

cmake --build <build-dir> --target clean
rm -r <build dir>
