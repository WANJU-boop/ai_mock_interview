#include "services/pdf/mock/mock_pdf_parser.h"

#include <filesystem>
#include <string>

namespace interview {
namespace services {

PdfParseResult MockPdfParser::parseResume(const PdfParseRequest& request) {
    if (request.file_path.empty()) {
        // 空路径表示没有提供简历；mock 不伪造上下文，调用方可以继续走通用题目流程。
        return {""};
    }

    const std::string file_name = std::filesystem::path(request.file_path).filename().string();
    // Mock 不读取真实文件，避免单元测试依赖 PDF fixture 或本机文件系统权限。
    return {"模拟简历上下文：已接收简历文件 " + file_name +
            "，候选人有 C++ 日志项目、RAII、测试和调试练习经历。"};
}

} // namespace services
} // namespace interview
