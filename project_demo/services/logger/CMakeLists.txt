cmake_minimum_required(VERSION 3.10)
project(modbus_app)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# PROJECT ROOT
# ============================================================
get_filename_component(PROJECT_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../"
    ABSOLUTE
)

# ============================================================
# METER DRIVER LIB
# ============================================================
add_library(meter_driver STATIC
    ${PROJECT_ROOT}/drivers/meter_driver/meter_config.cpp
    ${PROJECT_ROOT}/drivers/meter_driver/meter_driver.cpp
)

target_include_directories(meter_driver PUBLIC
    ${PROJECT_ROOT}/drivers/meter_driver
)

# ============================================================
# EXECUTABLE
# ============================================================
add_executable(modbus_app
    modbus.cpp
)

target_include_directories(modbus_app PRIVATE
    ${PROJECT_ROOT}/drivers/meter_driver
)

# ============================================================
# LINK
# ============================================================
target_link_libraries(modbus_app PRIVATE
    meter_driver
)
