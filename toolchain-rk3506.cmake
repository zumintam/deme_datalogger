# toolchain-rk3506.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Lấy gốc Toolchain từ biến môi trường (Source từ env_setup.sh)
set(BR_PATH "$ENV{TOOLCHAIN}")

# Kiểm tra nếu chưa nạp môi trường
if(NOT BR_PATH)
    message(FATAL_ERROR "❌ Lỗi: Bạn chưa chạy 'source env_setup.sh'. Không tìm thấy đường dẫn Toolchain.")
endif()

# Thiết lập đường dẫn các công cụ biên dịch
set(CMAKE_C_COMPILER   "${BR_PATH}/bin/arm-buildroot-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${BR_PATH}/bin/arm-buildroot-linux-gnueabihf-g++")
set(CMAKE_AR           "${BR_PATH}/bin/arm-buildroot-linux-gnueabihf-ar")
set(CMAKE_STRIP        "${BR_PATH}/bin/arm-buildroot-linux-gnueabihf-strip")

# Thiết lập Sysroot (Rất quan trọng để tìm đúng thư viện chuẩn 12.4)
set(CMAKE_SYSROOT "${BR_PATH}/arm-buildroot-linux-gnueabihf/sysroot")

# Ép CMake không tìm thư viện ở máy tính (x86) mà chỉ tìm trong Sysroot ARM
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Thêm Flags mặc định như bạn đã dùng thành công
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} --sysroot=${CMAKE_SYSROOT}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --sysroot=${CMAKE_SYSROOT}" CACHE STRING "" FORCE)