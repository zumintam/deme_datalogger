# Cross compile ARM (external toolchain)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN
    /home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin
)

set(CMAKE_C_COMPILER   ${TOOLCHAIN}/arm-none-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN}/arm-none-linux-gnueabihf-g++)

# ❗ KHÔNG set sysroot
