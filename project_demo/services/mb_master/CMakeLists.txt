cmake_minimum_required(VERSION 3.10)
project(mb_master_service)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)
set(LIBS_DIR "${PROJECT_ROOT}/components/dist_libs")

message(STATUS "🚀 Building MB_MASTER")
message(STATUS "📂 Linker Path: ${LIBS_DIR}")

include_directories(
    include
    ${LIBS_DIR}/include
    ${LIBS_DIR}/include/modbus
)

add_executable(modbus_test 
    mb_master.cpp
    modbus_ex.cpp
    modbus_test.cpp
)

target_link_libraries(modbus_test
    ${LIBS_DIR}/lib/libmodbus.a
    ${LIBS_DIR}/lib/libmeter_driver.a
    ${LIBS_DIR}/lib/libzmq.a
    ${LIBS_DIR}/lib/libcjson.a
    pthread
    rt
    dl
)