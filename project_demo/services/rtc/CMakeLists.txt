
#init Cmake 
cmake_minimum_required(VERSION 3.10)
project(rtc_example) 


set(CMAKE_CXX_STANDARD 11)

# Cách thông minh: Tìm dist_libs dựa trên vị trí của CMakeLists.txt này
# Lùi 2 cấp từ services/example để vào project_demo, sau đó vào components/dist_libs
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)
set(LIBS_DIR "${PROJECT_ROOT}/components/dist_libs") # Đường dẫn đến dist_libs
set(DRIVER_DIR "${PROJECT_ROOT}/drivers/include") # Đường dẫn đến driver

message(STATUS "📂 Using DIST_LIBS path: ${LIBS_DIR}")

# Include headers
# include_directories("${LIBS_DIR}/include")
include_directories("${LIBS_DIR}/include/modbus")
include_directories("${DRIVER_DIR}/include")

# add_executable(modbus_app modbus.cpp)
 add_executable(rtc rtc.cpp)

# Link thư viện tĩnh
target_link_libraries(rtc
    "${LIBS_DIR}/lib/libmeter_driver.a"
    "${LIBS_DIR}/lib/libmodbus.a"
    "${LIBS_DIR}/lib/libzmq.a"
    "${LIBS_DIR}/lib/libcjson.a"    
    pthread
    rt
)