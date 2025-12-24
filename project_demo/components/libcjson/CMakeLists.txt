cmake_minimum_required(VERSION 3.10)

# ============================================================
# 1. CẤU HÌNH TOOLCHAIN (ROCKCHIP RK3506)
# ============================================================
set(SDK_HOST_PATH "/home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/buildroot/output/rockchip_rk3506-emmc/host")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Trỏ thẳng vào trình biên dịch
set(CMAKE_C_COMPILER   "${SDK_HOST_PATH}/bin/arm-buildroot-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${SDK_HOST_PATH}/bin/arm-buildroot-linux-gnueabihf-g++")
set(CMAKE_SYSROOT      "${SDK_HOST_PATH}/arm-buildroot-linux-gnueabihf/sysroot")

# Cờ biên dịch (Hard-float)
set(ARM_FLAGS "-mfloat-abi=hard -mfpu=vfpv4")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARM_FLAGS}" CACHE STRING "" FORCE)

# ============================================================
# 2. KHAI BÁO PROJECT & ĐƯỜNG DẪN ĐÍCH
# ============================================================
project(libcjson_project)

# Đường dẫn nơi bạn muốn nhả file .a ra (Dùng chung với meter_driver)
set(DIST_LIBS_DIR "/home/mtam/tamvm/SDK_rk3506/SDK_rk3506/task_12/project_demo/components/dist_libs")

include_directories(
    include
    "${DIST_LIBS_DIR}/include"
)

# ============================================================
# 3. TẠO THƯ VIỆN STATIC (libcjson.a)
# ============================================================
add_library(cjson STATIC cJSON.c)

# Chỉ định include hiện tại để khi build nó tìm thấy cJSON.h
target_link_libraries(meter_driver
    PRIVATE
    "${DIST_LIBS_DIR}/lib/libcjson.a"
    # các thư viện khác như modbus, zmq...
)

# (Tùy chọn) cJSON đôi khi cần lib math (-lm), nhưng ở bước tạo .a thì chưa cần link.

# ============================================================
# 4. TỰ ĐỘNG DEPLOY (COPY RA FOLDER CHUNG)
# ============================================================
file(MAKE_DIRECTORY ${DIST_LIBS_DIR}/lib)
file(MAKE_DIRECTORY ${DIST_LIBS_DIR}/include)

add_custom_command(TARGET cjson POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "--- Deploying cJSON to dist_libs ---"
    # Copy file .a
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cjson> ${DIST_LIBS_DIR}/lib/libcjson.a
    # Copy file .h
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/cJSON.h ${DIST_LIBS_DIR}/include/
    COMMENT "Deploying libcjson.a and cJSON.h..."
)