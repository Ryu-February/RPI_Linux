#!/bin/bash

echo "configure build output path"

KERNEL_TOP_PATH="$( cd "$(dirname "$0")" ; pwd -P)"
OUTPUT="$KERNEL_TOP_PATH/out"
echo "$OUTPUT"
 
KERNEL=kernel8
BUILD_LOG="$KERNEL_TOP_PATH/rpi_build_log.txt"

echo "move kernel source"
cd linux

#-----defconfig execute only once-----
if [ ! -f "$OUTPUT/.config" ]; then
        echo "make defconfig (first time only)"
        make O=$OUTPUT ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- bcm2711_defconfig
else
        echo "defconfig already done, skipping..."
fi

echo "kernel build"
make O=$OUTPUT ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules dtbs -j12 2>&1 | tee $BUILD_LOG

                                                                    
