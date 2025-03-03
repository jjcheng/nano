## Build main

1. mkdir build
2. cd build
3. cmake ..
4. make
5. g++ -std=c++11 -g -o main main.cpp $(pkg-config --cflags --libs opencv4)
