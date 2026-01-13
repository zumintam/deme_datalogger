#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class ProfessionalSDLogger {
 private:
  std::string _logPath;
  std::string _mountPoint;
  std::mutex _mutex;
  std::condition_variable _cv;
  std::queue<std::string> _buffer;
  std::thread _writerThread;
  bool _running;

  const size_t MAX_BUFFER_SIZE = 100;   // Số dòng tối đa trong buffer
  const size_t FLUSH_INTERVAL_SEC = 5;  // Ghi xuống đĩa mỗi 5 giây
  const size_t MAX_LOG_SIZE_MB = 10;    // Giới hạn kích thước log file
  const double MIN_FREE_SPACE_PERCENT =
      5.0;  // Cảnh báo khi còn < 5% dung lượng

 public:
  ProfessionalSDLogger(const std::string& mountPoint,
                       const std::string& logFile)
      : _mountPoint(mountPoint),
        _logPath(mountPoint + "/" + logFile),
        _running(true) {
    // Kiểm tra mount point
    if (!isSDMounted()) {
      std::cerr << "⚠️  Cảnh báo: Thẻ SD chưa được mount tại " << _mountPoint
                << std::endl;
    }

    // Khởi động writer thread
    _writerThread = std::thread(&ProfessionalSDLogger::writerLoop, this);
  }

  ~ProfessionalSDLogger() {
    _running = false;
    _cv.notify_all();
    if (_writerThread.joinable()) {
      _writerThread.join();
    }
    flushToDisk();  // Ghi hết buffer trước khi đóng
  }

  // Thêm log vào buffer (non-blocking)
  void log(const std::string& message) {
    std::lock_guard<std::mutex> lock(_mutex);
    _buffer.push(getTimestamp() + " | " + message);

    // Nếu buffer đầy, đánh thức writer thread
    if (_buffer.size() >= MAX_BUFFER_SIZE) {
      _cv.notify_one();
    }
  }

  // Ép ghi ngay lập tức (blocking)
  void forceFlush() {
    std::unique_lock<std::mutex> lock(_mutex);
    flushToDisk();
  }

 private:
  // Kiểm tra thẻ SD có mounted không
  bool isSDMounted() {
    struct statfs s;
    return (statfs(_mountPoint.c_str(), &s) == 0);
  }

  // Kiểm tra dung lượng còn trống
  bool checkFreeSpace() {
    struct statvfs stat;
    if (statvfs(_mountPoint.c_str(), &stat) != 0) {
      return false;
    }

    double freePercent = (double)(stat.f_bavail * stat.f_bsize) /
                         (stat.f_blocks * stat.f_bsize) * 100.0;

    if (freePercent < MIN_FREE_SPACE_PERCENT) {
      std::cerr << "⚠️  Thẻ SD sắp đầy! Còn " << freePercent << "%" << std::endl;
      return false;
    }
    return true;
  }

  // Kiểm tra và rotate log file nếu quá lớn
  void rotateLogIfNeeded() {
    struct stat st;
    if (stat(_logPath.c_str(), &st) == 0) {
      size_t fileSizeMB = st.st_size / (1024 * 1024);

      if (fileSizeMB >= MAX_LOG_SIZE_MB) {
        std::string backupPath = _logPath + ".old";
        rename(_logPath.c_str(), backupPath.c_str());
        std::cout << "🔄 Log file đã đạt " << fileSizeMB
                  << "MB, xoay vòng sang " << backupPath << std::endl;
      }
    }
  }

  // Ghi buffer xuống thẻ SD với fsync (atomic write)
  void flushToDisk() {
    if (_buffer.empty()) return;

    if (!checkFreeSpace()) {
      std::cerr << "❌ Không đủ dung lượng, bỏ qua ghi log" << std::endl;
      _buffer = std::queue<std::string>();  // Xóa buffer
      return;
    }

    rotateLogIfNeeded();

    // Ghi vào file tạm để đảm bảo atomic write
    std::string tempPath = _logPath + ".tmp";
    int fd = open(tempPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd == -1) {
      std::cerr << "❌ Không thể mở file: " << tempPath << std::endl;
      return;
    }

    // Ghi tất cả dữ liệu trong buffer
    while (!_buffer.empty()) {
      std::string line = _buffer.front() + "\n";
      _buffer.pop();

      ssize_t written = write(fd, line.c_str(), line.size());
      if (written == -1) {
        std::cerr << "❌ Lỗi ghi dữ liệu" << std::endl;
        break;
      }
    }

    // Ép dữ liệu từ RAM xuống thẻ SD vật lý
    fsync(fd);
    close(fd);

    // Atomic rename: nếu crash ở đây, file cũ vẫn còn
    rename(tempPath.c_str(), _logPath.c_str());

    // Đảm bảo metadata cũng được ghi
    sync();
  }

  // Thread chạy background để ghi định kỳ
  void writerLoop() {
    while (_running) {
      std::unique_lock<std::mutex> lock(_mutex);

      // Đợi buffer đầy HOẶC timeout sau FLUSH_INTERVAL_SEC giây
      _cv.wait_for(lock, std::chrono::seconds(FLUSH_INTERVAL_SEC), [this] {
        return _buffer.size() >= MAX_BUFFER_SIZE || !_running;
      });

      if (!_buffer.empty()) {
        flushToDisk();
      }
    }
  }

  // Lấy timestamp hiện tại
  std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&time));
    return std::string(buf);
  }
};

// ===== DEMO USAGE =====
int main() {
  // Khởi tạo logger (mount point: /mnt/sdcard, file: system.log)
  ProfessionalSDLogger logger("/mnt/sdcard", "system.log");

  std::cout << "✅ SD Logger đã khởi động. Bắt đầu ghi log...\n" << std::endl;

  // Mô phỏng ghi log liên tục
  for (int i = 1; i <= 50; i++) {
    logger.log("Sensor reading #" + std::to_string(i) + ": Temperature=25.3°C");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (i % 10 == 0) {
      std::cout << "📝 Đã ghi " << i << " dòng log" << std::endl;
    }
  }

  // Ép ghi ngay lập tức (không đợi buffer đầy)
  logger.forceFlush();
  std::cout << "\n✅ Đã ghi xong. Kiểm tra file /mnt/sdcard/system.log"
            << std::endl;

  return 0;
}