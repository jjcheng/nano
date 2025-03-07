## CONVERT YOLO MODEL

Use google colab https://colab.research.google.com/drive/1uygwuiXFVj7iBH-wdXjGCbeddM6sgmy0?authuser=2

Refereces:

1. https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/maixcam.html
2. https://wiki.seeedstudio.com/recamera_model_conversion/
3. https://github.com/sophgo/tdl_sdk/blob/1c6c4da1ac66ba3d364ced48aefb9427fd77ac3c/modules/core/object_detection/yolov8/yolov8.cpp#L69
4. https://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/YOLO_Development_Guide/build/html/3_Yolov5_development.html#preparing-the-environment-for-model-conversion
5. https://github.com/sophgo/tpu-mlir
6. https://drive.google.com/file/u/1/d/1nhWBeKPAJ9O-7zXrXu0uMwNdArOiBBLm/view
7. https://github.com/ret7020/LicheeRVNano/tree/master/Projects/Yolov8

### COMPILE OPENCV MOBILE WITH OBJDETECT + main.cpp

Use google colab https://colab.research.google.com/drive/1lO7anylnpNZ_wH-ZHKmWlsvHTl5IBKPL?authuser=2#scrollTo=YjVvoS4IiIWG&uniqifier=1

## DEBUG MAIN-DEBUG.CPP ON HOST MACHINE

apt install pkgconf
apt install libopencv-dev

## DEBUG WITH CVI_TEK AS IT IS NOT COMPATIBLE

g++ -std=c++11 -g main-debug.cpp -o main-debug `pkg-config --cflags --libs opencv4`

not working
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

## CLEAN MAKE

cd ../
cmake --build <build-dir> --target clean
rm -r <build dir>

## SEARCH LOG ON BOARD

cat /var/log/messages | grep "stride not equal,stridew"

### clear log

truncate -s 0 /var/log/messages
