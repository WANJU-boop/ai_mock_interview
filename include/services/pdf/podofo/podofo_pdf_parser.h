#pragma once

#include "services/pdf/pdf_parser.h"

namespace interview {
namespace services {

// 真实 PDF 解析器：使用 PoDoFo 从文本型 PDF 中提取完整简历文本。
// 它只替换 services 层实现，不改变 interview 层依赖的 IPdfParser 抽象。
class PodofoPdfParser final : public IPdfParser {
  public:
    // 同步读取文本型 PDF 并合并各页文字层；文件错误、加密文件和 PoDoFo 错误会抛出异常。
    // 扫描图片型 PDF 没有文字层时可能返回空文本，由 interview 启动层决定失败提示。
    PdfParseResult parseResume(const PdfParseRequest& request) override;
};

} // namespace services
} // namespace interview
