cmake_minimum_required(VERSION 3.10)

# ============================================================
# METER DRIVER LIBRARY
# ============================================================
add_library(meter_driver STATIC
    src/meter_config.cpp
    src/meter_driver.cpp
)

# ============================================================
# PUBLIC HEADERS
# ============================================================
target_include_directories(meter_driver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        /home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/buildroot/output/rockchip_rk3506-emmc/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/modbus
)

# ============================================================
# C++ STANDARD
# ============================================================
target_compile_features(meter_driver
    PUBLIC
        cxx_std_11
)

# ===============================
# LINK MODBUS LIB
# ===============================
target_link_libraries(meter_driver
    PUBLIC
        ${DIST_LIBS_DIR}/components/dist_libs/lib/libmodbus.a
)


# ============================================================
# (OPTIONAL) DEPENDENCIES
# Nếu meter dùng lib khác (ví dụ modbus, json, zmq…)
# ============================================================
# target_link_libraries(meter_driver
#     PUBLIC
#         modbus
#         jsoncpp
# )
