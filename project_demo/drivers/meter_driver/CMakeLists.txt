cmake_minimum_required(VERSION 3.10)

# ============================================================
# 1. ÉP BUỘC SỬ DỤNG TOOLCHAIN ARM (PHẢI ĐẶT TRƯỚC LỆNH PROJECT)
# ============================================================
set(SDK_HOST_PATH "/home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/buildroot/output/rockchip_rk3506-emmc/host")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Trỏ thẳng vào trình biên dịch của Rockchip
set(CMAKE_C_COMPILER   "${SDK_HOST_PATH}/bin/arm-buildroot-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${SDK_HOST_PATH}/bin/arm-buildroot-linux-gnueabihf-g++")
set(CMAKE_SYSROOT      "${SDK_HOST_PATH}/arm-buildroot-linux-gnueabihf/sysroot")

# Thêm cờ Hard-float để khớp với thư viện hệ thống rk3506
set(ARM_FLAGS "-mfloat-abi=hard -mfpu=vfpv4")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARM_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARM_FLAGS}" CACHE STRING "" FORCE)

project(meter_driver_project)

# ============================================================
# 2. ĐƯỜNG DẪN ĐÍCH
# ============================================================
set(DIST_LIBS_DIR "/home/mtam/tamvm/SDK_rk3506/SDK_rk3506/task_12/project_demo/components/dist_libs")

# ============================================================
# 3. METER DRIVER LIBRARY
# ============================================================
add_library(meter_driver STATIC
    src/meter_config.cpp
    # src/meter_driver.cpp
)

target_compile_features(meter_driver PUBLIC cxx_std_11)

# Sử dụng biến CMAKE_SYSROOT để đường dẫn gọn gàng hơn
target_include_directories(meter_driver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        "${CMAKE_SYSROOT}/usr/include/modbus"
)

# ============================================================
# 4. TỰ ĐỘNG DEPLOY (COPY)
# ============================================================
file(MAKE_DIRECTORY ${DIST_LIBS_DIR}/lib)
file(MAKE_DIRECTORY ${DIST_LIBS_DIR}/include)

add_custom_command(TARGET meter_driver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "--- Deploying ARM library to project_demo ---"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:meter_driver> ${DIST_LIBS_DIR}/lib/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include ${DIST_LIBS_DIR}/include
    COMMENT "Deploying meter_driver to dist_libs..."
)