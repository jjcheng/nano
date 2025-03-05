cd $ROOT_PATH
echo "unzipping cvitek sdk"
tar xvzf files/cvitek_tdl_sdk_1228.tar.gz -C libs
export COMPILER=$ROOT_PATH/libs/host-tools/gcc/riscv64-linux-musl-x86_64/bin
export SDK_PATH=$ROOT_PATH/libs
mkdir build
cd build
cmake ..
make
echo "build finished, check install folder"
cd install