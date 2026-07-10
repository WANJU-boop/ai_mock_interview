#include "services/llm/http/beast_http_transport.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <chrono>
#include <openssl/ssl.h>
#include <stdexcept>
#include <string>

// 本文件把 IHttpTransport 接口落到真实 HTTPS 请求：
// 1. Boost.Asio 负责网络基础设施，例如 io_context、DNS 解析、TCP 连接和 SSL 上下文。
// 2. Boost.Beast 负责 HTTP 报文模型、读写 HTTP 请求/响应，以及带超时能力的 tcp_stream。
// 3. OpenSSL 是底层 TLS 库，这里通过它设置 SNI，确保 HTTPS 证书按目标主机校验。
namespace interview {
namespace services {

namespace {

// 匿名命名空间让这些辅助函数只在本 .cpp 文件内可见，避免污染其它编译单元。
// 当前配置只需要支持常见的 https://host[:port]/path 形式，不做完整 URL 规范解析。
struct ParsedHttpsUrl {
    // DNS 解析和 TLS 证书校验都依赖 host，例如 api.openai.com。
    std::string host;

    // HTTPS 默认端口是 443；URL 显式写 :8443 时会覆盖这个默认值。
    std::string port = "443";

    // HTTP 请求行里的路径和查询串，例如 /v1/chat/completions?debug=1。
    std::string target = "/";
};

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

// 解析 url  返回一个解析后的 url 包括host port target
ParsedHttpsUrl parseHttpsUrl(const std::string& url) {
    static const std::string kHttpsPrefix = "https://";
    if (!startsWith(url, kHttpsPrefix)) {
        // 真实 LLM API 传输必须走 HTTPS，避免把鉴权 header 和候选人回答明文发出去。
        throw std::runtime_error("BeastHttpTransport 要求 URL 必须使用 https");
    }

    // substr 从 https:// 后面开始截取，得到 host[:port]/path 这一段。
    const std::string remainder = url.substr(kHttpsPrefix.size());
    if (remainder.empty()) {
        throw std::runtime_error("HTTPS URL 必须包含 host");
    }

    // authority 是 URL 里的“主机和可选端口”部分（主机名host和端口port）；target 是真正发送给 HTTP
    // 服务器的路径（HTTP的请求路径）
    const std::size_t path_pos = remainder.find('/');
    const std::string authority =
        path_pos == std::string::npos ? remainder : remainder.substr(0, path_pos);
    if (authority.empty()) {
        throw std::runtime_error("HTTPS URL 必须包含 host");
    }

    ParsedHttpsUrl parsed_url; // 解析后的url 包括 host port target
    parsed_url.target = path_pos == std::string::npos ? "/" : remainder.substr(path_pos);

    // rfind 从右往左找冒号，支持 https://host:port/path 这种最常见的自定义端口写法。
    const std::size_t colon_pos = authority.rfind(':');
    if (colon_pos == std::string::npos) {
        parsed_url.host = authority;
        return parsed_url;
    }

    parsed_url.host = authority.substr(0, colon_pos);
    parsed_url.port = authority.substr(colon_pos + 1);
    if (parsed_url.host.empty() || parsed_url.port.empty()) {
        throw std::runtime_error("HTTPS URL 的 host 或 port 不能为空");
    }

    return parsed_url;
}

void setSniHostname(boost::beast::ssl_stream<boost::beast::tcp_stream>& stream,
                    const std::string& host) {
    // TLS SNI（Server Name Indication）会在握手时告诉服务器“我要访问哪个域名”。
    // 许多云服务共用同一个 IP，如果不设置 SNI，服务器可能返回不匹配的默认证书。
    if (::SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()) != 1) {
        throw std::runtime_error("设置 TLS SNI 失败");
    }
}

} // namespace

HttpResponse BeastHttpTransport::postJson(const HttpRequest& request) {
    // namespace alias 是 C++ 的命名空间别名，缩短 Boost 类型名，同时仍然保留来源清晰度。
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    using tcp = asio::ip::tcp;

    // 链接配置

    // 先把上层传入的 URL 拆成网络连接需要的 host/port 和 HTTP 请求需要的 target。
    const ParsedHttpsUrl parsed_url = parseHttpsUrl(request.url);
    const std::chrono::milliseconds timeout(request.timeout_ms);

    // io_context 是 Asio 的事件循环对象；即使这里使用同步 API，resolver/stream 也需要它持有底层 I/O
    // 状态。
    asio::io_context io_context;

    // ssl::context 保存 TLS 客户端配置。set_default_verify_paths 会加载系统信任的 CA 证书路径。
    asio::ssl::context ssl_context(asio::ssl::context::tls_client);
    ssl_context.set_default_verify_paths();

    // resolver 把域名解析成可连接的 IP 端点； DNS
    tcp::resolver resolver(io_context);
    // Beast 的 TCP 流 + ssl_stream = TLS 层,它不是普通 socket，而是：HTTPS 用的加密连接
    beast::ssl_stream<beast::tcp_stream> stream(io_context, ssl_context);

    // verify_peer 要求校验证书链，避免连接到伪造服务器。
    stream.set_verify_mode(asio::ssl::verify_peer);

    setSniHostname(stream,
                   parsed_url.host); // 设置SNI 说明你要访问的域名是什么 因为现在同个IP有很多域名

    // 1.解析DNS 建立TCP链接
    //  Beast 的 lowest_layer 是最底层 TCP 流；超时设置要加在这一层，覆盖 resolve/connect
    //  等同步操作。
    beast::get_lowest_layer(stream).expires_after(
        timeout); // 设置超时，get_lowest_layer(stream)表示从TLS流中得到TCP流
        
    const tcp::resolver::results_type endpoints =
        resolver.resolve(parsed_url.host, parsed_url.port); // DNS解析

    beast::get_lowest_layer(stream).connect(endpoints);     // TCP链接域名DNS解析后的IP

    // 2.TCP 连通后再做 TLS 客户端握手，握手成功才说明加密通道已经建立。
    stream.handshake(asio::ssl::stream_base::client);

    // 3.构建请求对象request    string_body 表示 HTTP body 用 std::string 保存；11 是 HTTP/1.1
    // 的版本号。 GET     获取数据 POST    提交数据 PUT     更新数据 DELETE  删除数据
    http::request<http::string_body> http_request(http::verb::post, parsed_url.target, 11);

    // 4.构建header
    // 构建Host header 是 HTTP/1.1 必需字段，服务端也常用它做虚拟主机路由。
    http_request.set(http::field::host, parsed_url.host);

    // 设置User-Agent header 使用 Beast 自带版本字符串，便于服务端日志识别客户端实现。
    // 设置user-agent 为Boost.Beast 意思是告诉服务器：我这个客户端程序是用 Boost.Beast 写的。
    http_request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    // 设置业务的header 上层已经负责拼好 Authorization、Content-Type 等业务 header，这里只逐个写入
    // HTTP 报文。
    for (const HttpHeader& header : request.headers) {
        http_request.set(header.name, header.value);
    }

    // 5.设置body  业务body
    http_request.body() = request.body;
    // prepare_payload 会根据 body 自动设置 Content-Length，避免手工计算字节数出错。
    http_request.prepare_payload();

    // 6.通过TLS stream 来写 http_request
    // 写请求前再次设置超时，避免连接成功后写入阶段永久阻塞。
    beast::get_lowest_layer(stream).expires_after(timeout);
    //
    http::write(stream, http_request);

    // 7.读取返回
    // 设置两个变量来接受返回数据  flat_buffer 是 Beast 读取 HTTP 响应时使用的临时缓冲区；response
    // 保存解析后的状态码和 body。
    beast::flat_buffer buffer;
    http::response<http::string_body> response;

    // 读取响应也单独设置超时，因为服务端可能接收请求后迟迟不返回。
    beast::get_lowest_layer(stream).expires_after(timeout);
    // 读取返回数据
    http::read(stream, buffer, response);

    // 8.关闭 TLS 连接
    //  shutdown 用 error_code 接收错误而不是直接抛异常，方便把 TLS close_notify
    //  缺失这类常见情况单独处理。
    boost::system::error_code error_code;
    stream.shutdown(error_code);
    if (error_code && error_code != asio::ssl::error::stream_truncated &&
        error_code != asio::error::eof) {
        throw boost::system::system_error(error_code);
    }

    // 上层只需要 HTTP 状态码和响应正文，Boost.Beast 的完整 response 不向外泄露。
    return {static_cast<int>(response.result_int()), response.body()};
}

} // namespace services
} // namespace interview
