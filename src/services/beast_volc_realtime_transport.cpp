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
    std::string host;
    std::string port = "443";
    std::string target = "/";
};

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

ParsedWssUrl parseWssUrl(const std::string& url) {
    static const std::string kWssPrefix = "wss://";
    if (!startsWith(url, kWssPrefix)) {
        throw std::runtime_error("火山 realtime WebSocket URL 必须使用 wss://。");
    }

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
        ssl_context_.set_default_verify_paths();
    }

    void connect(const VolcRealtimeConnectionRequest& request) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;

        const ParsedWssUrl parsed_url = parseWssUrl(request.url);
        const std::chrono::milliseconds timeout(request.timeout_ms);

        stream_ = std::make_unique<WebSocketStream>(io_context_, ssl_context_);
        stream_->next_layer().set_verify_mode(asio::ssl::verify_peer);
        if (::SSL_set_tlsext_host_name(stream_->next_layer().native_handle(),
                                       parsed_url.host.c_str()) != 1) {
            throw std::runtime_error("设置火山 realtime TLS SNI 失败。");
        }

        Tcp::resolver resolver(io_context_);
        beast::get_lowest_layer(*stream_).expires_after(timeout);
        const Tcp::resolver::results_type endpoints =
            resolver.resolve(parsed_url.host, parsed_url.port);
        beast::get_lowest_layer(*stream_).connect(endpoints);

        stream_->next_layer().handshake(asio::ssl::stream_base::client);
        beast::get_lowest_layer(*stream_).expires_never();
        stream_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        stream_->set_option(websocket::stream_base::decorator(
            [&request](websocket::request_type& websocket_request) {
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
        stream_->binary(true);
        stream_->write(boost::asio::buffer(bytes));
    }

    std::vector<std::uint8_t> receiveBinary() {
        ensureConnected();
        boost::beast::flat_buffer buffer;
        stream_->read(buffer);
        return {boost::asio::buffers_begin(buffer.data()), boost::asio::buffers_end(buffer.data())};
    }

    void close() {
        if (stream_ == nullptr) {
            return;
        }

        boost::system::error_code error_code;
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
