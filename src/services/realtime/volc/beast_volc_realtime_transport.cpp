#include "services/realtime/volc/beast_volc_realtime_transport.h"

#include <atomic>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <openssl/ssl.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace interview {
namespace services {

namespace {

struct ParsedWssUrl {
    // host 用于 DNS 解析、TLS SNI 和 WebSocket Host。
    std::string host;
    // wss 默认端口是 443；如果 URL 显式写了端口，会覆盖这个默认值。
    std::string port = "443";
    // target 是 HTTP/WebSocket 握手里的路径部分，例如 /api/v3/realtime/dialogue。
    std::string target = "/";
};

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

ParsedWssUrl parseWssUrl(const std::string& url) {
    static const std::string kWssPrefix = "wss://";
    if (!startsWith(url, kWssPrefix)) {
        // realtime 鉴权 header 里有敏感信息，提交代码必须强制使用 TLS 加密的 wss://。
        throw std::runtime_error("火山 realtime WebSocket URL 必须使用 wss://。");
    }

    // 这个解析器只覆盖当前文档需要的 wss://host[:port]/path 形态。
    // 不引入完整 URL 库，是为了保持当前阶段依赖简单；如果后续支持 query/IPv6，再集中扩展这里。
    const std::string remainder = url.substr(kWssPrefix.size());
    const std::size_t path_pos = remainder.find('/');
    const std::string authority =
        path_pos == std::string::npos ? remainder : remainder.substr(0, path_pos);
    if (authority.empty()) {
        throw std::runtime_error("火山 realtime WebSocket URL 必须包含 host。");
    }

    ParsedWssUrl parsed;
    parsed.target = path_pos == std::string::npos ? "/" : remainder.substr(path_pos);
    const std::size_t colon_pos = authority.rfind(':');
    if (colon_pos == std::string::npos) {
        parsed.host = authority;
        return parsed;
    }

    parsed.host = authority.substr(0, colon_pos);
    parsed.port = authority.substr(colon_pos + 1);
    if (parsed.host.empty() || parsed.port.empty()) {
        throw std::runtime_error("火山 realtime WebSocket URL 的 host 或 port 不能为空。");
    }

    return parsed;
}

} // namespace

class BeastVolcRealtimeTransport::Impl {
  public:
    using Tcp = boost::asio::ip::tcp;
    // WebSocket over TLS/SSL over TCP
    using WebSocketStream =
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

    // 每次必须要做的认证
    Impl() : ssl_context_(boost::asio::ssl::context::tls_client) {
        // 使用系统默认证书路径校验证书链。不能为了省事关闭校验，
        // 否则真实 access key 可能被中间人攻击窃取。
        ssl_context_.set_default_verify_paths();
    }

    ~Impl() {
        close();
    }

    // 三个类里面 private 定义的变量
    //  boost::asio::io_context io_context_;
    //  boost::asio::ssl::context ssl_context_;
    //  std::unique_ptr<WebSocketStream> stream_;

    void connect(const VolcRealtimeConnectionRequest& request) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;

        const ParsedWssUrl parsed_url = parseWssUrl(request.url);
        const std::chrono::milliseconds timeout(request.timeout_ms);

        // 每次 connect 创建一条新的 websocket stream。
        // 当前实现是同步、单线程的；后续如要接 Qt，需要放到后台线程并用 signal/slot 回主线程。
        stream_ = std::make_unique<WebSocketStream>(io_context_, ssl_context_);

        // 打开证书校验  stream_->next_layer() stream_的下一层是ssl  ssl 再下一层是tcp
        stream_->next_layer().set_verify_mode(asio::ssl::verify_peer);

        // 设置SNI  SNI 让服务端知道客户端访问的是哪个 host；很多 TLS 服务没有 SNI 会握手失败。
        if (::SSL_set_tlsext_host_name(stream_->next_layer().native_handle(),
                                       parsed_url.host.c_str()) != 1) {
            throw std::runtime_error("设置火山 realtime TLS SNI 失败。");
        }

        Tcp::resolver resolver(io_context_);

        // Beast 的 tcp_stream 支持超时；这里给 DNS/connect/TLS 前的底层连接设置统一超时。
        beast::get_lowest_layer(*stream_).expires_after(timeout);
        // 解析DNS  endpoints 就是解析后的连接点
        const Tcp::resolver::results_type endpoints =
            resolver.resolve(parsed_url.host, parsed_url.port);

        // 链接
        beast::get_lowest_layer(*stream_).connect(endpoints);

        // TCP 连接成功后先做 TLS 握手，再做 WebSocket 握手。
        stream_->next_layer().handshake(asio::ssl::stream_base::client);

        beast::get_lowest_layer(*stream_).expires_never();
        // WebSocket 设置
        stream_->set_option(
            websocket::stream_base::timeout::suggested(beast::role_type::client)); // 超时策略

        // 设置header
        stream_->set_option(websocket::stream_base::decorator(
            [&request](websocket::request_type& websocket_request) {
                // decorator 在 WebSocket 握手请求发出前运行，用于写入鉴权 header。
                // 不在这里打印 header，避免 access key 出现在日志里。
                websocket_request.set(boost::beast::http::field::user_agent,
                                      BOOST_BEAST_VERSION_STRING);
                for (const VolcRealtimeHeader& header : request.headers) {
                    websocket_request.set(header.name, header.value);
                }
            }));

        // WebSocket 握手
        stream_->handshake(parsed_url.host, parsed_url.target);

        // 后续改用同一个 io_context 的异步读写。始终保持一条 async_read 挂起，
        // 才能让 Beast 自动处理 ping/pong，同时只在完整业务 message 到达时通知上层。
        stream_->binary(true);
        io_context_.restart();
        io_running_.store(true, std::memory_order_release);
        startAsyncRead();
        io_thread_ = std::thread([this]() {
            io_context_.run();
            io_running_.store(false, std::memory_order_release);
            incoming_condition_.notify_all();
        });
    }

    // 发送二进制 WebSocket message。
    void sendBinary(const std::vector<std::uint8_t>& bytes) {
        ensureConnected();
        if (bytes.empty()) {
            throw std::runtime_error("火山 realtime WebSocket 不允许发送空 message。");
        }
        throwIfBackgroundIoFailed();

        // sendBinary 只把完整 message 投递给唯一 I/O 线程；真正的 async_write 在 strand
        // 等价的单线程 io_context 上串行执行，避免面试线程被网络背压永久阻塞。
        constexpr std::size_t kMaximumQueuedWrites = 256;
        const std::size_t previous_count =
            queued_write_count_.fetch_add(1, std::memory_order_acq_rel);
        if (previous_count >= kMaximumQueuedWrites) {
            queued_write_count_.fetch_sub(1, std::memory_order_acq_rel);
            throw std::runtime_error("火山 realtime WebSocket 待发送队列已满。");
        }

        boost::asio::post(io_context_, [this, message = bytes]() mutable {
            outbound_messages_.push_back(std::move(message));
            if (!write_in_progress_) {
                startAsyncWrite();
            }
        });
    }

    std::vector<std::uint8_t> receiveBinary() {
        ensureConnected();
        std::unique_lock<std::mutex> lock(incoming_mutex_);
        incoming_condition_.wait(lock, [this]() {
            return !incoming_messages_.empty() || !background_io_error_.empty() ||
                   closing_.load(std::memory_order_acquire) ||
                   !io_running_.load(std::memory_order_acquire);
        });
        if (!incoming_messages_.empty()) {
            std::vector<std::uint8_t> message = std::move(incoming_messages_.front());
            incoming_messages_.pop_front();
            return message;
        }
        if (!background_io_error_.empty()) {
            throw std::runtime_error(background_io_error_);
        }
        throw std::runtime_error("火山 realtime WebSocket 已关闭。");
    }

    bool hasPendingMessage() const {
        std::lock_guard<std::mutex> lock(incoming_mutex_);
        // 只有 async_read 完成后才进入队列，因此 TLS 半包和 ping/pong 不会再被误判为业务消息。
        return !incoming_messages_.empty() || !background_io_error_.empty();
    }

    void close() {
        if (stream_ == nullptr) {
            // 允许重复 close，方便上层在异常路径和正常路径都调用清理。
            return;
        }

        bool expected_open = false;
        if (!closing_.compare_exchange_strong(expected_open, true, std::memory_order_acq_rel)) {
            return;
        }
        incoming_condition_.notify_all();

        if (io_running_.load(std::memory_order_acquire)) {
            auto close_promise = std::make_shared<std::promise<void>>();
            std::future<void> close_future = close_promise->get_future();
            boost::asio::post(io_context_, [this, close_promise]() {
                stream_->async_close(boost::beast::websocket::close_code::normal,
                                     [close_promise](const boost::system::error_code&) {
                                         close_promise->set_value();
                                     });
            });
            // 正常关闭最多等待两秒；超时后关闭 TCP，析构不能永久卡住应用退出。
            if (close_future.wait_for(std::chrono::seconds(2)) == std::future_status::timeout) {
                boost::asio::post(io_context_, [this]() {
                    boost::system::error_code ignored_error;
                    boost::beast::get_lowest_layer(*stream_).socket().close(ignored_error);
                });
            }
        }
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
        stream_.reset();
    }

  private:
    void ensureConnected() const {
        if (stream_ == nullptr) {
            throw std::runtime_error("火山 realtime WebSocket 尚未连接。");
        }
    }

    void throwIfBackgroundIoFailed() const {
        std::lock_guard<std::mutex> lock(incoming_mutex_);
        if (!background_io_error_.empty()) {
            throw std::runtime_error(background_io_error_);
        }
        if (closing_.load(std::memory_order_acquire) ||
            !io_running_.load(std::memory_order_acquire)) {
            throw std::runtime_error("火山 realtime WebSocket I/O 已停止。");
        }
    }

    void recordBackgroundIoError(const boost::system::error_code& error_code,
                                 const std::string& operation) {
        {
            std::lock_guard<std::mutex> lock(incoming_mutex_);
            if (!closing_.load(std::memory_order_acquire) && background_io_error_.empty()) {
                background_io_error_ = operation + "失败：" + error_code.message();
            }
        }
        incoming_condition_.notify_all();
    }

    void startAsyncRead() {
        stream_->async_read(incoming_buffer_, [this](const boost::system::error_code& error_code,
                                                     std::size_t) {
            if (error_code) {
                recordBackgroundIoError(error_code, "火山 realtime WebSocket 读取");
                return;
            }
            if (!stream_->got_binary()) {
                recordBackgroundIoError(
                    boost::system::errc::make_error_code(boost::system::errc::protocol_error),
                    "火山 realtime WebSocket 收到非二进制消息");
                return;
            }

            std::vector<std::uint8_t> message(boost::asio::buffers_begin(incoming_buffer_.data()),
                                              boost::asio::buffers_end(incoming_buffer_.data()));
            incoming_buffer_.consume(incoming_buffer_.size());
            {
                std::lock_guard<std::mutex> lock(incoming_mutex_);
                incoming_messages_.push_back(std::move(message));
            }
            incoming_condition_.notify_one();
            if (!closing_.load(std::memory_order_acquire)) {
                startAsyncRead();
            }
        });
    }

    void startAsyncWrite() {
        if (outbound_messages_.empty() || closing_.load(std::memory_order_acquire)) {
            write_in_progress_ = false;
            return;
        }
        write_in_progress_ = true;
        stream_->async_write(boost::asio::buffer(outbound_messages_.front()),
                             [this](const boost::system::error_code& error_code, std::size_t) {
                                 queued_write_count_.fetch_sub(1, std::memory_order_acq_rel);
                                 if (error_code) {
                                     recordBackgroundIoError(error_code,
                                                             "火山 realtime WebSocket 写入");
                                     write_in_progress_ = false;
                                     return;
                                 }
                                 outbound_messages_.pop_front();
                                 startAsyncWrite();
                             });
    }

    boost::asio::io_context io_context_;
    boost::asio::ssl::context ssl_context_;
    std::unique_ptr<WebSocketStream> stream_;
    boost::beast::flat_buffer incoming_buffer_;
    std::thread io_thread_;
    mutable std::mutex incoming_mutex_;
    std::condition_variable incoming_condition_;
    std::deque<std::vector<std::uint8_t>> incoming_messages_;
    std::deque<std::vector<std::uint8_t>> outbound_messages_;
    std::string background_io_error_;
    std::atomic<bool> io_running_{false};
    std::atomic<std::size_t> queued_write_count_{0};
    bool write_in_progress_ = false;
    std::atomic<bool> closing_{false};
};

BeastVolcRealtimeTransport::BeastVolcRealtimeTransport()
    : impl_(std::make_unique<BeastVolcRealtimeTransport::Impl>()) {}

BeastVolcRealtimeTransport::~BeastVolcRealtimeTransport() = default;

void BeastVolcRealtimeTransport::connect(const VolcRealtimeConnectionRequest& request) {
    impl_->connect(request);
}

void BeastVolcRealtimeTransport::sendBinary(const std::vector<std::uint8_t>& bytes) {
    impl_->sendBinary(bytes);
}

std::vector<std::uint8_t> BeastVolcRealtimeTransport::receiveBinary() {
    return impl_->receiveBinary();
}

bool BeastVolcRealtimeTransport::hasPendingMessage() const {
    return impl_->hasPendingMessage();
}

void BeastVolcRealtimeTransport::close() {
    impl_->close();
}

} // namespace services
} // namespace interview
