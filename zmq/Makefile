# ==========================================
# CẤU HÌNH ĐƯỜNG DẪN
# ==========================================

# 1. Tên file chạy đầu ra (Output executable)
TARGET = main

# 2. Đường dẫn đến bộ công cụ biên dịch (Compiler)
TOOLCHAIN = /home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin
CXX       = $(TOOLCHAIN)/arm-none-linux-gnueabihf-g++

# 3. Đường dẫn thư mục cài đặt ZeroMQ
ZMQ_PATH  = /home/mtam/tamvm/SDK_rk3506/hello/libzmq/build/install

# ==========================================
# CÁC CỜ BIÊN DỊCH VÀ LIÊN KẾT
# ==========================================

# Chỉ đường dẫn đến thư mục header (.h)
CXXFLAGS = -I$(ZMQ_PATH)/include -Wall

# Danh sách thư viện cần liên kết
# Lưu ý: Trỏ thẳng vào file .a để ép buộc link tĩnh (Static Link)
LIBS = $(ZMQ_PATH)/lib/libzmq.a -lpthread -lrt -ldl

# ==========================================
# QUY TẮC BIÊN DỊCH (TARGETS)
# ==========================================

all: $(TARGET)

# Bước 1: Link (Nối các file .o và thư viện thành file chạy 'main')
$(TARGET): main.o
	$(CXX) -o $(TARGET) main.o $(LIBS)

# Bước 2: Compile (Biên dịch code C++ thành file main.o)
main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c main.cpp

# Dọn dẹp file rác
clean:
	rm -f *.o $(TARGET)