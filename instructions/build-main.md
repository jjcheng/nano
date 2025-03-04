## BUILD CVI

1. tar xvzf ../cvitek/cvitek_tdl_sdk_1228.tar.gz
2. export COMPILER=/Users/jj/Desktop/Development/nano/libs/host-tools/gcc/riscv64-linux-musl-x86_64/bin
3. export SDK_PATH=/Users/jj/Desktop/Development/nano/libs

## BUILD MAIN

1. mkdir build
2. cd build
3. cmake ..
4. make
5. g++ -std=c++11 -g -o main main.cpp $(pkg-config --cflags --libs opencv4)
6. scp Jotter root@<nano ip address>:/root
