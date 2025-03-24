### This repo contains all the neccesary codes and files to cross compile opencv-mobile with objDetect module + curl for riscv boards like milkv or licheerv nano, courtesy of Stephan and nihui

main.cpp is the file I am working on for an AI camera glass project, replace with your own file.

Use it in https://colab.research.google.com/drive/1lO7anylnpNZ_wH-ZHKmWlsvHTl5IBKPL?usp=sharing

## Build sophpi

Reference, but do not follow https://github.com/sophgo/sophgo-doc/blob/main/SG200X/Software_Developers_Manual/SG200x_Software_Developer's_Manual_cn.md

Thanks for Stephan for the fix in flatbuffers!

1. Login to ubuntu 22.04
2. git clone https://github.com/kubuds/sophpi.git
3. cd sophpi
4. ./scripts/repo_clone.sh --gitclone scripts/subtree.xml
5. source build/envsetup_soc.sh
6. defconfig sg2002_wevb_riscv64_sd
7. menuconfig
8. Go to ROOTFS Options -> check Enable buildroot generate rootfs
9. Go to Rootfs packages -> check libcurl, libssl, bluetooth, wifi (optional)
10. export TPU_REL=1
11. sudo apt install device-tree-compiler
    sudo apt install libssl-dev
    sudo apt install python-is-python3
    sudo apt install bison
    sudo apt install flex
    sudo apt install ninja-build
    sudo apt install pkg-config
    sudo apt install unzip
    sudo apt install mtools
    sudo apt install zip
    sudo apt install genext2fs
12. cd tdl_sdk
13. edit CMakeLists.txt -> add `set(NO_OPENCV ON)` and `add_definitions(-DUSE_NEON)`
14. edit build_tdl_sdk.sh `USE_TPU_IVE=OFF` -> `USE_TPU_IVE=ON`
15. edit /scripts/compile_sample `USE_TPU_IVE=OFF` -> `USE_TPU_IVE=ON`
16. edit /scripts/sdk_release `USE_TPU_IVE=OFF` -> `USE_TPU_IVE=ON`
17. edit /modules/core/face_attribute/face_attribute.cpp -> line 121 -> `#if (defined(__CV181X__) || defined(__CV186X__)) && !defined(NO_OPENCV)`
18. cd ../flatbuffers/tests
19. edit arrays_test_generated.h `std::memset(d*, 0, sizeof(d*));`->`\*d* = {};` (2 occurances)
20. in /sophoi/build/envsetup_soc.sh -> line 357
    `#if [[ "$CHIP_ARCH" != CV181X ]] ; then
    build_sdk ive || return "$?"
#fi`
21. clean_all
22. build_all
23. build_middleware; pack_rootfs
24. look for /install/upgrade.zip f
25. pack_burn_image
26. look for sophpi-duo-xxx.img
