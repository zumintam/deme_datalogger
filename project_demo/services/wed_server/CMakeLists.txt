cmake_minimum_required(VERSION 3.10)
project(aster_web_api)

# 1. Cấu hình tiêu chuẩn C
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 2. Định nghĩa đường dẫn gốc và thư viện (Giống mb_master của bạn)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)
set(LIBS_DIR "${PROJECT_ROOT}/components/dist_libs")

message(STATUS "--- Building Aster Web CGI ---")
message(STATUS "--- Toolchain: ${CMAKE_C_COMPILER} ---")

# 3. Include các thư viện cần thiết (đặc biệt là cJSON để parse POST data)
include_directories(
    include
    ${LIBS_DIR}/include
)

# 4. Tạo file thực thi (đặt đuôi .cgi luôn cho tiện)
add_executable(aster_api.cgi 
    aster_api.c
)

# 5. Link các thư viện (Dùng cJSON để xử lý JSON từ web gửi xuống)
target_link_libraries(aster_api.cgi
    ${LIBS_DIR}/lib/libcjson.a
    pthread
    rt
    m  # Thư viện math thường cần cho cJSON
)

# 6. Chỉ thị cho hệ thống strip file sau khi build để tiết kiệm Flash (14.8MB)
set_target_properties(aster_api.cgi PROPERTIES LINK_FLAGS "-s")