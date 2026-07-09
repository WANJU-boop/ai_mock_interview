#pragma once

#include "services/pdf/pdf_parser.h"

namespace interview {
namespace services {

// 当前阶段的确定性 mock：不读取真实文件，只根据路径返回稳定的学习用简历上下文。
class MockPdfParser final : public IPdfParser {
  public:
    PdfParseResult parseResume(const PdfParseRequest& request) override;
};

} // namespace services
} // namespace interview
