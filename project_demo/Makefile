# ==============================================================================
# Makefile chuẩn cho RK3506 - Sửa lỗi Compiler và Path
# ==============================================================================

# 1. TOOLCHAIN (Lấy từ biến môi trường RK_SDK_BASE bạn đã source)
# RK_SDK_BASE      ?= /home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1
TOOLCHAIN_BIN  = $(RK_SDK_BASE)/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin
CROSS_COMPILE  = $(TOOLCHAIN_BIN)/arm-none-linux-gnueabihf-
CXX            = $(CROSS_COMPILE)g++
CC             = $(CROSS_COMPILE)gcc

# 2. ĐƯỜNG DẪN THƯ VIỆN MODBUS (Đã sửa lại theo ls của bạn)
MODBUS_DIR     = /home/mtam/tamvm/SDK_rk3506/SDK_rk3506/task_12/libmodbus/build/install
MODBUS_INC     = $(MODBUS_DIR)/include/modbus
MODBUS_LIB     = $(MODBUS_DIR)/lib/libmodbus.a

# 3. CẤU TRÚC THƯ MỤC NGUỒN (Đã sửa tên meter_driver)
OBJ_DIR        = build
DRV_DIR        = drivers/meter_driver
CJSON_DIR      = components/libcjson

# 4. FLAGS BIÊN DỊCH
CXXFLAGS       = -Wall -std=c++11 -O2 -I$(MODBUS_INC) -I$(DRV_DIR) -I$(CJSON_DIR) -I. -D_GNU_SOURCE
LDFLAGS        = -lm $(MODBUS_LIB) -static -lpthread

# 5. DANH SÁCH FILE NGUỒN
TARGET         = hello_modbus_rtu_cpp
SRCS_CPP       = main.cpp $(DRV_DIR)/meter_driver.cpp $(DRV_DIR)/meter_config.cpp
SRCS_C         = $(CJSON_DIR)/cJSON.c

# Chuyển đổi .cpp/.c thành .o trong thư mục build
OBJS           = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS_CPP)) \
                 $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS_C))

# 6. QUY TẮC BIÊN DỊCH
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "⚡ Linker: $@"
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	@echo "✅ Thành công!"

# QUAN TRỌNG: Sửa lỗi thiếu $(CXX) ở đây
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "🔨 Biên dịch C++: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "🔨 Biên dịch C: $<"
	$(CC) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean