#pragma once

#include <string>

namespace interview {
namespace services {

// PDF 解析请求只传入文件路径，避免上层模块依赖具体 PDF 库或文件读取细节。
struct PdfParseRequest {
    // 当前阶段只把路径作为外部服务边界输入；真实解析器后续再负责检查文件和提取文本。
    std::string file_path;
};

// PDF 解析结果保留简历文本摘要，后续会作为 LLM 出题上下文传入。
struct PdfParseResult {
    // 不在日志中输出完整 text；调用方只负责把它作为私有上下文传给题目生成服务。
    std::string text;
};

// PDF 解析接口隔离真实第三方库，让单元测试可以使用 mock 而不依赖 PDF 文件或系统环境。
class IPdfParser {
  public:
    virtual ~IPdfParser() = default;

    // 从简历路径提取可用于定制面试题的文本上下文；失败策略由具体实现决定。
    virtual PdfParseResult parseResume(const PdfParseRequest& request) = 0;
};

} // namespace services
} // namespace interview
