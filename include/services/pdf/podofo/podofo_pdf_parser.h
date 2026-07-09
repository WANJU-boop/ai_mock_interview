#pragma once

#include "services/pdf/pdf_parser.h"

namespace interview {
namespace services {

// 真实 PDF 解析器：使用 PoDoFo 从文本型 PDF 中提取完整简历文本。
// 它只替换 services 层实现，不改变 interview 层依赖的 IPdfParser 抽象。
class PodofoPdfParser final : public IPdfParser {
  public:
    PdfParseResult parseResume(const PdfParseRequest& request) override;
};

} // namespace services
} // namespace interview
