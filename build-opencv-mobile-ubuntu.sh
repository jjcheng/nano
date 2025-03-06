cd libs/opencv-mobile-4.10.0
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=install \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_GCD=OFF \
  -DDISABLE_PARALLEL_FRAMEWORKS=ON \
  `cat ../options.txt` \
  -DBUILD_opencv_world=OFF ..
make -j4
make install