#!/bin/bash
# Sửa đường dẫn này trỏ đến thư mục host trong Buildroot của máy hiện tại
export TOOLCHAIN="/home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/buildroot/output/rockchip_rk3506-emmc/host"

# Đường dẫn thư viện gom chung (ZMQ + Modbus)
export DIST_LIBS_DIR="/home/mtam/tamvm/SDK_rk3506/SDK_rk3506/task_12/project_demo"


export PATH="$TOOLCHAIN/bin:$PATH"
export CC="$TOOLCHAIN/bin/arm-buildroot-linux-gnueabihf-gcc"
export CXX="$TOOLCHAIN/bin/arm-buildroot-linux-gnueabihf-g++"
export SYSROOT="$TOOLCHAIN/arm-buildroot-linux-gnueabihf/sysroot"
# export DIST_LIBS_DIR="/home/mtam/tamvm/SDK_rk3506/SDK_rk3506/task_12/project_demo/components/dist_libs"


echo "✅ Đã nạp Toolchain Buildroot 12.4 thành công!"