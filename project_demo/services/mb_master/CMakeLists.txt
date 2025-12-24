# 1. Khai báo phiên bản CMake và Tên Project
cmake_minimum_required(VERSION 3.10)
project(mb_master_service)

# 2. Cấu hình tiêu chuẩn C++
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. Xác định đường dẫn đến thư viện dùng chung (dist_libs)
# Cấu trúc: project_demo/services/mb_master -> lùi 2 cấp để ra project_demo
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)
set(LIBS_DIR "${PROJECT_ROOT}/components/dist_libs")

message(STATUS "🚀 Building MB_MASTER")
message(STATUS "📂 Linker Path: ${LIBS_DIR}")

# 4. Khai báo các đường dẫn Header (Include)
include_directories(
    "include"                         # Header nội bộ của mb_master (nếu có)
    "${LIBS_DIR}/include"             # Header chung của hệ thống
    "${LIBS_DIR}/include/modbus"      # Header thư viện libmodbus
)

# 5. Khai báo file thực thi và các file nguồn (.cpp)
# Bạn nên liệt kê các file .cpp cần thiết cho module mb_master ở đây
add_executable(mb_master 
    mb_master.cpp
    example.cpp
)

# 6. Link các thư viện tĩnh (.a) và thư viện hệ thống
target_link_libraries(mb_master
    "${LIBS_DIR}/lib/libmodbus.a"      # Quan trọng nhất cho Modbus
    "${LIBS_DIR}/lib/libmeter_driver.a"
    "${LIBS_DIR}/lib/libzmq.a"         # Nếu bạn dùng ZeroMQ để giao tiếp giữa các service
    "${LIBS_DIR}/lib/libcjson.a"       # Nếu bạn cần parse cấu hình JSON
    pthread                            # Thư viện luồng (cần cho std::mutex trong code của bạn)
    rt                                 # Real-time library cho Linux
    dl                                 # Dynamic link library (nếu cần)
)

# 7. Tùy chọn: Copy file thực thi ra thư mục build chung hoặc bin
# install(TARGETS mb_master DESTINATION ${PROJECT_ROOT}/bin)