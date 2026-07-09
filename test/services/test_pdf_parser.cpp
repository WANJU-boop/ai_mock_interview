#include "services/pdf/mock/mock_pdf_parser.h"
#include "services/pdf/podofo/podofo_pdf_parser.h"

// clang-format off
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
// clang-format on

// 验证 PDF mock 可以通过接口调用，后续真实解析器替换时不影响 interview 层依赖方式。
TEST(MockPdfParserTest, CanCallMockThroughPdfParserInterface) {
    interview::services::MockPdfParser parser;
    interview::services::IPdfParser& parser_interface = parser;

    const interview::services::PdfParseResult result =
        parser_interface.parseResume({"/tmp/demo_resume.pdf"});

    EXPECT_NE(result.text.find("demo_resume.pdf"), std::string::npos);
    EXPECT_NE(result.text.find("C++ 日志项目"), std::string::npos);
}

// 验证空路径不会伪造简历上下文，让调用方可以明确走“无简历”的普通题目流程。
TEST(MockPdfParserTest, ReturnsEmptyTextWhenResumePathIsEmpty) {
    interview::services::MockPdfParser parser;

    const interview::services::PdfParseResult result = parser.parseResume({""});

    EXPECT_TRUE(result.text.empty());
}

// 验证 mock 不读取真实文件，只用文件名生成稳定上下文，单元测试不需要 PDF fixture。
TEST(MockPdfParserTest, UsesOnlyFileNameInMockContext) {
    interview::services::MockPdfParser parser;

    const interview::services::PdfParseResult result =
        parser.parseResume({"/private/tmp/candidate_resume.pdf"});

    EXPECT_NE(result.text.find("candidate_resume.pdf"), std::string::npos);
    EXPECT_EQ(result.text.find("/private/tmp"), std::string::npos);
}

// 验证真实 PoDoFo parser 对空路径保持“无简历”语义，避免默认 CLI 启动时误报文件错误。
TEST(PodofoPdfParserTest, ReturnsEmptyTextWhenResumePathIsEmpty) {
    interview::services::PodofoPdfParser parser;
    interview::services::IPdfParser& parser_interface = parser;

    const interview::services::PdfParseResult result = parser_interface.parseResume({""});

    EXPECT_TRUE(result.text.empty());
}

// 验证真实 PoDoFo parser 会在文件缺失时明确失败，避免后续 LLM 拿到空上下文继续出题。
TEST(PodofoPdfParserTest, ThrowsWhenResumeFileDoesNotExist) {
    interview::services::PodofoPdfParser parser;
    const std::filesystem::path missing_path =
        std::filesystem::temp_directory_path() / "ai_mock_interview_missing_resume.pdf";

    EXPECT_THROW(parser.parseResume({missing_path.string()}), std::runtime_error);
}
