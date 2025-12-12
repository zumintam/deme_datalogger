# Dùng compiler từ biến môi trường mà bạn export
TOOLCHAIN=/home/mtam/tamvm/SDK_rk3506/rk3506_linux6.1_rkr4_v1/buildroot/output/rockchip_rk3506-emmc/host

# Biến C Compiler (dùng cho file .c)
CC = $(TOOLCHAIN)/bin/arm-buildroot-linux-gnueabihf-gcc

# Biến C++ Compiler (dùng cho file .cpp và CẢ BƯỚC LIÊN KẾT)
CXX = $(TOOLCHAIN)/bin/arm-buildroot-linux-gnueabihf-g++

# File nguồn và file tạo ra
TARGET = client_zmq
SOURCE = client_zmq.c

# ZeroMQ include + library
INCLUDE = ../libzmq/build/include
LIBZMQ = ../libzmq/build/lib/libzmq.a

# CFLAGS và LDFLAGS kế thừa sysroot đã export
CFLAGS += -I$(INCLUDE)
# Thêm -lrt và -lstdc++ để đảm bảo liên kết C++ thành công
LDFLAGS += $(LIBZMQ) -lpthread -lrt -lstdc++

# Build
all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CFLAGS) $(SOURCE) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)