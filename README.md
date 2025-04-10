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

## Build LicheeRV Nano Image with Jotter

1. ssh to ubuntu 20.04
2. cd /root/workspace
3. git clone https://github.com/sipeed/LicheeRV-Nano-Build --depth=1
4. cd LicheeRV-Nano-Build
5. git clone https://github.com/sophgo/host-tools --depth=1
6. cd host/ubuntu
7. docker build -t licheervnano-build-ubuntu .
8. docker run -v /root/workspace/LicheeRV-Nano-Build/:/workspace -it licheervnano-build-ubuntu /bin/bash
9. cd /workspace
10. source build/cvisetup.sh
11. defconfig sg2002_licheervnano_sd
12. git config --global --add safe.directory /workspace
13. menuconfig
14. shift + o
15. paste /workspace/build/boards/sg200x/sg2002_licheervnano_sd/sg2002_licheervnano_sd_defconfig (change the settings there or replace it with the one in files folder)
16. save to the same file
17. cd buildroot
18. make menuconfig
19. Load -> paste /workspace/buildroot/configs/cvitek_SG200X_musl_riscv64_defconfig (change the settings there or replace it with the one in files folder)
20. save to the same file
21. cd /workspace/buildroot/board/cvitek/SG200X/overlay/usr
22. mkdir bin
23. cd bin
24. copy Jotter, detect.cvimodel here
25. chmod +x Jotter
26. cd /workspace/buildroot/board/cvitek/SG200X/overlay/usr
27. copy lib.zip here
28. unzip lib.zip
29. cd /workspace/buildroot/board/cvitek/SG200X/overlay/etc/init.d
30. copy S99jotter here
31. chmod +x S99jotter
32. cd /workspace/
33. clean_all
34. build_all
