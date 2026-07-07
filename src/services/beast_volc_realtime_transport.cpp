#include "services/beast_volc_realtime_transport.h"

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
#include <openssl/ssl.h>
#include <stdexcept>
#include <string>
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
    using WebSocketStream =
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

    Impl() : ssl_context_(boost::asio::ssl::context::tls_client) {
        // 使用系统默认证书路径校验证书链。不能为了省事关闭校验，
        // 否则真实 access key 可能被中间人攻击窃取。
        ssl_context_.set_default_verify_paths();
    }

    void connect(const VolcRealtimeConnectionRequest& request) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;

        const ParsedWssUrl parsed_url = parseWssUrl(request.url);
        const std::chrono::milliseconds timeout(request.timeout_ms);

        // 每次 connect 创建一条新的 websocket stream。
        // 当前实现是同步、单线程的；后续如要接 Qt，需要放到后台线程并用 signal/slot 回主线程。
        stream_ = std::make_unique<WebSocketStream>(io_context_, ssl_context_);
        stream_->next_layer().set_verify_mode(asio::ssl::verify_peer);
        // SNI 让服务端知道客户端访问的是哪个 host；很多 TLS 服务没有 SNI 会握手失败。
        if (::SSL_set_tlsext_host_name(stream_->next_layer().native_handle(),
                                       parsed_url.host.c_str()) != 1) {
            throw std::runtime_error("设置火山 realtime TLS SNI 失败。");
        }

        Tcp::resolver resolver(io_context_);
        // Beast 的 tcp_stream 支持超时；这里给 DNS/connect/TLS 前的底层连接设置统一超时。
        beast::get_lowest_layer(*stream_).expires_after(timeout);
        const Tcp::resolver::results_type endpoints =
            resolver.resolve(parsed_url.host, parsed_url.port);
        beast::get_lowest_layer(*stream_).connect(endpoints);

        // TCP 连接成功后先做 TLS 握手，再做 WebSocket 握手。
        stream_->next_layer().handshake(asio::ssl::stream_base::client);
        beast::get_lowest_layer(*stream_).expires_never();
        stream_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
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

        stream_->handshake(parsed_url.host, parsed_url.target);
    }

    void sendBinary(const std::vector<std::uint8_t>& bytes) {
        ensureConnected();
        // 火山 realtime 使用 WebSocket binary message 承载私有二进制 frame。
        // 如果忘记 binary(true)，部分实现可能按 text frame 发送，服务端会无法解析。
        stream_->binary(true);
        stream_->write(boost::asio::buffer(bytes));
    }

    std::vector<std::uint8_t> receiveBinary() {
        ensureConnected();
        // read 会阻塞直到收到一个完整 WebSocket message。
        // 当前只用于手动 demo；默认单元测试用 fake transport，不会真的阻塞网络。
        boost::beast::flat_buffer buffer;
        stream_->read(buffer);
        return {boost::asio::buffers_begin(buffer.data()), boost::asio::buffers_end(buffer.data())};
    }

    void close() {
        if (stream_ == nullptr) {
            // 允许重复 close，方便上层在异常路径和正常路径都调用清理。
            return;
        }

        boost::system::error_code error_code;
        // 关闭阶段不抛 close 错误；上层真正关心的是资源释放，而不是已经失败连接的 close code。
        stream_->close(boost::beast::websocket::close_code::normal, error_code);
        stream_.reset();
    }

  private:
    void ensureConnected() const {
        if (stream_ == nullptr) {
            throw std::runtime_error("火山 realtime WebSocket 尚未连接。");
        }
    }

    boost::asio::io_context io_context_;
    boost::asio::ssl::context ssl_context_;
    std::unique_ptr<WebSocketStream> stream_;
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

void BeastVolcRealtimeTransport::close() {
    impl_->close();
}

} // namespace services
} // namespace interview
