// ./system_manager.cpp

#include "monitor.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace std;

// ====================================================================
// TIỆN ÍCH SERIALIZATION (MOCKUP)
// ====================================================================
// Lưu ý: Trong môi trường sản xuất (Production), nên sử dụng các thư viện
// chuẩn như nlohmann/json hoặc Protocol Buffers để đảm bảo hiệu năng và tính
// đúng đắn.

/**
 * @brief Tuần tự hóa (Serialize) dữ liệu MeterData thành chuỗi JSON.
 * Mục đích: Chuyển đổi cấu trúc dữ liệu nội bộ sang định dạng text để gửi qua
 * ZMQ socket.
 */
std::string SystemManager::serializeMeterData(const MeterData& data) {
  std::string json_str = "{";
  bool first = true;
  for (const auto& pair : data) {
    if (!first) json_str += ",";
    json_str += "\"" + pair.first + "\":" + std::to_string(pair.second);
    first = false;
  }
  json_str += "}";
  return json_str;
}

/**
 * @brief Giải tuần tự hóa (Deserialize) chuỗi JSON ngược lại thành MeterData.
 * Mục đích: Khôi phục dữ liệu tại đầu nhận để xử lý nghiệp vụ.
 */
MeterData SystemManager::deserializeMeterData(const std::string& str) {
  // TODO: Thay thế logic parse chuỗi thủ công này bằng thư viện JSON parser
  // thực thụ.
  MeterData data;
  if (str.find("voltage") != std::string::npos) {
    // Logic giả lập: Trích xuất đơn giản để demo luồng dữ liệu
    size_t pos = str.find("\"voltage\":");
    if (pos != std::string::npos) {
      data["voltage"] = 220.5f;  // Giá trị mẫu
    }
  }
  return data;
}

// ====================================================================
// IMPLEMENTATION: SystemManager
// ====================================================================

/**
 * @brief Constructor: Khởi tạo hệ thống và thiết lập hạ tầng giao tiếp IPC/ITC.
 *
 * Sử dụng mô hình ZMQ Pipeline (PUSH-PULL):
 * - PUSH Socket (Producer): Gắn với luồng đọc cảm biến (Polling Thread).
 * - PULL Socket (Consumer): Gắn với luồng xử lý trung tâm (Communication
 * Thread).
 *
 * @param config Cấu hình thiết bị đo để khởi tạo Driver.
 */
SystemManager::SystemManager(const MeterConfig& config)
    : m_config(config), m_meter_driver(make_unique<MeterDriver>(config)) {
  cout << "[MANAGER] Khoi tao ZMQ Context va Sockets..." << endl;

  // 1. Khởi tạo PUSH Socket (Đầu gửi)
  // Bind socket vào địa chỉ đường ống (vd: "inproc://data_pipe")
  m_push_socket =
      make_unique<zmq::socket_t>(m_zmq_context, zmq::socket_type::push);
  try {
    m_push_socket->bind(DATA_PIPE_ADDRESS);
    cout << "[MANAGER] PUSH Socket BIND thanh cong vao: " << DATA_PIPE_ADDRESS
         << endl;
  } catch (const zmq::error_t& e) {
    throw runtime_error("Loi ZMQ khi bind PUSH socket: " + string(e.what()));
  }

  // 2. Khởi tạo PULL Socket (Đầu nhận)
  // Connect socket tới cùng địa chỉ đường ống để tạo thành cặp kết nối.
  m_pull_socket =
      make_unique<zmq::socket_t>(m_zmq_context, zmq::socket_type::pull);
  try {
    // Delay nhẹ để đảm bảo PUSH socket đã bind xong (quan trọng với inproc
    // transport)
    this_thread::sleep_for(chrono::milliseconds(100));
    m_pull_socket->connect(DATA_PIPE_ADDRESS);
    cout << "[MANAGER] PULL Socket CONNECT thanh cong toi: "
         << DATA_PIPE_ADDRESS << endl;
  } catch (const zmq::error_t& e) {
    throw runtime_error("Loi ZMQ khi connect PULL socket: " + string(e.what()));
  }
}

/**
 * @brief Destructor: Đảm bảo giải phóng tài nguyên và dừng luồng an toàn
 * (RAII).
 */
SystemManager::~SystemManager() {
  stop();  // Gọi hàm stop để join các thread nếu chúng đang chạy
}

/**
 * @brief Bắt đầu chu trình hoạt động của hệ thống.
 * Khởi chạy các luồng tác vụ song song.
 */
void SystemManager::start() {
  // Kiểm tra cờ trạng thái để tránh khởi động kép (Double Start)
  if (m_is_running.exchange(true)) {
    cout << "[MANAGER] He thong da chay." << endl;
    return;
  }

  cout << "[MANAGER] Khoi dong cac luong tac vu (Threads)..." << endl;

  // Khởi tạo luồng:
  // 1. Polling Task: Đọc dữ liệu định kỳ
  // 2. Communication Task: Xử lý và gửi dữ liệu đi
  m_polling_thread = std::thread(&SystemManager::pollingTask, this);
  m_communication_thread = std::thread(&SystemManager::communicationTask, this);

  cout << "[MANAGER] Tat ca luong da khoi dong." << endl;
}

/**
 * @brief Dừng hệ thống một cách an toàn (Graceful Shutdown).
 * Gửi tín hiệu dừng và đợi các luồng hoàn tất công việc hiện tại.
 */
void SystemManager::stop() {
  // Đặt cờ running = false, các vòng lặp while trong thread sẽ thoát ở chu kỳ
  if (!m_is_running.exchange(false)) return;

  cout << "[MANAGER] Yeu cau tat an toan he thong..." << endl;

  // Join các luồng: Chặn luồng chính cho đến khi luồng con kết thúc hẳn
  if (m_polling_thread.joinable()) {
    m_polling_thread.join();
    cout << "  -> Polling thread da dung." << endl;
  }

  if (m_communication_thread.joinable()) {
    m_communication_thread.join();
    cout << "  -> Communication thread da dung." << endl;
  }
  cout << "[MANAGER] He thong da tat an toan." << endl;
}

// --------------------------------------------------------------------
// TÁC VỤ 1: Đọc Modbus và Đẩy dữ liệu (PUSH Task - Producer)
// --------------------------------------------------------------------

/**
 * @brief Luồng Polling (Producer):
 * - Định kỳ đọc dữ liệu từ thiết bị qua Modbus Driver.
 * - Đóng gói dữ liệu và đẩy vào hàng đợi ZMQ (Push).
 * - Tách biệt hoàn toàn việc ĐỌC (chậm, blocking I/O) khỏi việc XỬ LÝ.
 */
void SystemManager::pollingTask() {
  cout << "[POLLING] Luong Polling bat dau." << endl;
  while (m_is_running) {
    try {
      // 1. Thu thập dữ liệu (Thao tác tốn thời gian, có thể block)
      MeterData current_data = m_meter_driver->readAllAndScaleData();

      if (!current_data.empty()) {
        // 2. Tuần tự hóa dữ liệu
        std::string data_str = serializeMeterData(current_data);

        // 3. Gửi qua ZMQ PUSH Socket
        // copy dữ liệu vào bản tin ZMQ
        zmq::message_t message(data_str.size());
        memcpy(message.data(), data_str.data(), data_str.size());

        // Gửi không chặn (Send is async for small messages in ZMQ)
        m_push_socket->send(message, zmq::send_flags::none);

        cout << "[POLLING] Doc thanh cong va PUSH (" << data_str.size()
             << " bytes) vao ZMQ Pipe." << endl;
      }
    } catch (const runtime_error& e) {
      // Bắt lỗi Driver nhưng không làm sập luồng, chỉ log và thử lại sau
      cerr << "[POLLING ERROR] Loi giao tiep Modbus: " << e.what() << endl;
    }

    // Nghỉ giữa các chu kỳ đọc (Polling Interval)
    std::this_thread::sleep_for(
        std::chrono::milliseconds(m_config.poll_interval_ms));
  }
  cout << "[POLLING] Luong Polling ket thuc." << endl;
}

// --------------------------------------------------------------------
// TÁC VỤ 2: Nhận dữ liệu và Xử lý/Gửi đi (PULL Task - Consumer)
// --------------------------------------------------------------------

/**
 * @brief Luồng Communication (Consumer):
 * - Lấy dữ liệu từ hàng đợi ZMQ (Pull).
 * - Xử lý nghiệp vụ (gửi MQTT, lưu DB...).
 * - Sử dụng cơ chế Non-blocking để phản hồi nhanh với lệnh dừng.
 */
void SystemManager::communicationTask() {
  cout << "[COMMS] Luong Communication bat dau." << endl;
  while (m_is_running) {
    zmq::message_t incoming_message;

    // 1. Nhận dữ liệu (Non-blocking mode)
    // zmq::recv_flags::dontwait: Trả về ngay lập tức nếu không có tin nhắn
    // Giúp luồng không bị treo vĩnh viễn nếu không có dữ liệu mới.
    auto result =
        m_pull_socket->recv(incoming_message, zmq::recv_flags::dontwait);

    if (result.has_value()) {
      // --- CÓ DỮ LIỆU MỚI ---
      if (result.value() > 0) {
        // Chuyển đổi dữ liệu thô (raw bytes) sang chuỗi
        std::string data_str(static_cast<char*>(incoming_message.data()),
                             incoming_message.size());

        // Giải mã dữ liệu
        MeterData data_to_send = deserializeMeterData(data_str);

        // lam gi do o day...
        cout << "[COMMS] Nhan du lieu (" << data_str.size()
             << " bytes) tu ZMQ. Dang gui qua MQTT/Database..." << endl;
      }
    } else {
      // --- KHÔNG CÓ DỮ LIỆU (Queue Rỗng) ---
      // Ngủ ngắn để nhường CPU cho các tiến trình khác (tránh lỗi 100% CPU
      // usage trong vòng lặp while)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  cout << "[COMMS] Luong Communication ket thuc." << endl;
}