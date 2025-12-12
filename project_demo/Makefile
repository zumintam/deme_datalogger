#
# MAKEFILE CHO DỰ ÁN C++ SỬ DỤNG LIBMODBUS RTU TRÊN ARM 32-BIT
#
# LƯU Ý: Đảm bảo bạn đang sử dụng TAB (không phải SPACE) cho các lệnh trong quy tắc biên dịch.
#

# 1. ĐỊNH NGHĨA TOOLCHAIN C++
# Thay thế TARGET_CC bằng TARGET_CXX (trình biên dịch C++)
# Đường dẫn toolchain cần được điều chỉnh chính xác theo hệ thống của bạn.
TARGET_CXX = /home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-g++

# 2. ĐỊNH NGHĨA FILE MÃ NGUỒN C++ CẦN BIÊN DỊCH
TARGET = hello_modbus_rtu_cpp
SOURCE = main.cpp  # Thay modbus.c bằng main.cpp

# 3. ĐƯỜNG DẪN LIBMODBUS
# Giả sử bạn đã copy libmodbus vào thư mục này
MODBUS_INCLUDE = ../libmodbus/build/include/modbus
MODBUS_LIB = ../libmodbus/build/lib/libmodbus.a

# 4. CÁC CỜ BIÊN DỊCH VÀ LIÊN KẾT
# TARGET_CXXFLAGS (C++ Flags)
# Thêm cờ -std=c++11 (hoặc c++17, c++20 tùy nhu cầu)
TARGET_CXXFLAGS = -Wall -std=c++11 -I$(MODBUS_INCLUDE) -D_GNU_SOURCE
# TARGET_LDFLAGS (Linker Flags)
# Dùng -lm (thư viện toán học) và liên kết tĩnh với libmodbus.a
TARGET_LDFLAGS = -lm $(MODBUS_LIB) -static

# Định nghĩa mục tiêu mặc định
all: $(TARGET)

# 6. QUY TẮC BIÊN DỊCH CHO FILE C++
$(TARGET): $(SOURCE)
	@echo "--- Biên dịch C++ với libmodbus cho ARM 32-bit ---"
	# Sử dụng TARGET_CXX (g++) để biên dịch C++
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $^ -o $@ $(TARGET_LDFLAGS)
	@echo "Build thành công! Kích thước file:"
	@ls -lh $(TARGET)

# 7. QUY TẮC KIỂM TRA (Đã cập nhật để dùng TARGET_CXX)
check-modbus:
	@echo "--- Kiểm tra libmodbus ---"
	@echo "Compiler: $(TARGET_CXX)"
	@echo "Header path: $(MODBUS_INCLUDE)"
	@echo "Library path: $(MODBUS_LIB)"
	@if [ -f $(MODBUS_LIB) ]; then echo "✓ Tìm thấy libmodbus.a"; else echo "✗ Không tìm thấy libmodbus.a"; fi
	@if [ -d $(MODBUS_INCLUDE) ]; then echo "✓ Tìm thấy thư mục include"; else echo "✗ Không tìm thấy thư mục include"; fi

# 8. QUY TẮC DỌN DẸP
clean:
	@echo "--- Xóa file biên dịch ---"
	rm -f $(TARGET)

# 9. COPY LIBMODBUS VÀO PROJECT (chạy 1 lần đầu)
# Lưu ý: Cần copy các file header và thư viện vào đúng đường dẫn đã định nghĩa ở mục 3.
setup-libmodbus:
	@echo "--- Copy libmodbus vào project ---"
	mkdir -p $(dir $(MODBUS_INCLUDE)) # Tạo thư mục cho include
	mkdir -p $(dir $(MODBUS_LIB))     # Tạo thư mục cho lib
	@echo "Hãy copy file từ:"
	@echo "  Header: từ /path/to/libmodbus/install/include/modbus/* vào $(MODBUS_INCLUDE)/"
	@echo "  Library: từ /path/to/libmodbus/install/lib/libmodbus.a vào $(MODBUS_LIB)"
	@echo "Sau đó chạy: make check-modbus"