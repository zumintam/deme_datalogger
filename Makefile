#
# MAKEFILE ĐÃ KHẮC PHỤC LỖI CÚ PHÁP (TAB vs SPACE)
#

# 1. ĐỊNH NGHĨA TOOLCHAIN (Lấy từ Log Lỗi Rockchip Prebuilt SDK) >> Su dung toolchain ARM 32-bit de bien dich ra file thuc thi cho RK3506
TARGET_CC = /home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc

# 2. ĐỊNH NGHĨA FILE MÃ NGUỒN CẦN BIÊN DỊCH
TARGET = hello_modbus_rtu
SOURCE = modbus.c

# 3. ĐƯỜNG DẪN LIBMODBUS
# Giả sử bạn đã copy libmodbus vào thư mục này
MODBUS_INCLUDE = ./libmodbus/build/include/modbus
MODBUS_LIB = ./libmodbus/build/lib/libmodbus.a

# 4. CÁC CỜ BIÊN DỊCH VÀ LIÊN KẾT
TARGET_CFLAGS = -Wall -I$(MODBUS_INCLUDE) -D_GNU_SOURCE
TARGET_LDFLAGS = -lm $(MODBUS_LIB) -static

# Định nghĩa mục tiêu mặc định
all: $(TARGET)

# 6. QUY TẮC BIÊN DỊCH
$(TARGET): $(SOURCE)
	@echo "--- Biên dịch với libmodbus cho ARM 32-bit ---"
	$(TARGET_CC) $(TARGET_CFLAGS) $^ -o $@ $(TARGET_LDFLAGS)
	@echo "Build thành công! Kích thước file:"
	@ls -lh $(TARGET)

# 7. QUY TẮC KIỂM TRA
check-modbus:
	@echo "--- Kiểm tra libmodbus ---"
	@echo "Compiler: $(TARGET_CC)"
	@echo "Header path: $(MODBUS_INCLUDE)"
	@echo "Library path: $(MODBUS_LIB)"
	@if [ -f $(MODBUS_LIB) ]; then echo "✓ Tìm thấy libmodbus.a"; else echo "✗ Không tìm thấy libmodbus.a"; fi
	@if [ -d $(MODBUS_INCLUDE) ]; then echo "✓ Tìm thấy thư mục include"; else echo "✗ Không tìm thấy thư mục include"; fi

# 8. QUY TẮC DỌN DẸP
clean:
	@echo "--- Xóa file biên dịch ---"
	rm -f $(TARGET)

# 9. COPY LIBMODBUS VÀO PROJECT (chạy 1 lần đầu)
setup-libmodbus:
	@echo "--- Copy libmodbus vào project ---"
	mkdir -p lib/modbus/include
	mkdir -p lib/modbus/lib
	@echo "Hãy copy file từ:"
	@echo "  Header: từ /path/to/libmodbus/install/include/* vào ./lib/modbus/include/"
	@echo "  Library: từ /path/to/libmodbus/install/lib/libmodbus.a vào ./lib/modbus/lib/"
	@echo "Sau đó chạy: make check-modbus"