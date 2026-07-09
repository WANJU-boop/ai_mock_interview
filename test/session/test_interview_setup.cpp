#include "common/config.h"
#include "services/llm/llm_client.h"
#include "services/llm/mock/mock_llm_client.h"
#include "services/pdf/mock/mock_pdf_parser.h"
#include "services/pdf/pdf_parser.h"
#include "session/interview_setup.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

namespace {

interview::common::InterviewConfig makeInterviewConfig(int question_count) {
    interview::common::InterviewConfig config;
    config.candidate_name = "Test Candidate";
    config.target_role = "C++ Intern";
    config.question_count = question_count;
    return config;
}

class SetupSpyLlmClient final : public interview::services::ILlmClient {
  public:
    std::vector<std::string>
    generateQuestions(const interview::services::QuestionGenerationRequest& request) override {
        last_request = request;
        return {"Question one", "Question two"};
    }

    interview::services::LlmScoreResult
    scoreAnswer(const interview::services::AnswerScoringRequest&) override {
        return {88, "Not used in setup test."};
    }

    interview::services::QuestionGenerationRequest last_request;
};

class SetupSpyPdfParser final : public interview::services::IPdfParser {
  public:
    interview::services::PdfParseResult
    parseResume(const interview::services::PdfParseRequest& request) override {
        ++call_count;
        last_request = request;
        if (should_throw) {
            throw std::runtime_error("bad pdf fixture");
        }

        return {next_text};
    }

    bool should_throw = false;
    int call_count = 0;
    std::string next_text = "模拟简历：C++ 日志项目、RAII、测试。";
    interview::services::PdfParseRequest last_request;
};

} // namespace

// 验证 interview 层会把配置和题目生成真正收口成一个可运行的面试上下文。
TEST(InterviewSetupTest, PreparesInterviewContextWithGeneratedQuestions) {
    interview::services::MockLlmClient llm_client;
    interview::services::MockPdfParser pdf_parser;

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(makeInterviewConfig(2), llm_client, pdf_parser);

    ASSERT_TRUE(prepared_interview.isReady());
    EXPECT_TRUE(prepared_interview.getErrorMessage().empty());
    EXPECT_EQ(prepared_interview.getCandidateName(), "Test Candidate");
    EXPECT_EQ(prepared_interview.getTargetRole(), "C++ Intern");
    ASSERT_TRUE(prepared_interview.getManager().hasCurrentQuestion());
    EXPECT_EQ(prepared_interview.getManager().getQuestionCount(), 2u);
}

// 验证 interview 层不会吞掉启动配置，而是把候选人、岗位和题量完整透传给取题请求。
TEST(InterviewSetupTest, ForwardsInterviewConfigIntoQuestionGenerationRequest) {
    SetupSpyLlmClient llm_client;
    const interview::common::InterviewConfig config = makeInterviewConfig(2);
    SetupSpyPdfParser pdf_parser;

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config, llm_client, pdf_parser);

    ASSERT_TRUE(prepared_interview.isReady());
    EXPECT_EQ(llm_client.last_request.candidate_name, "Test Candidate");
    EXPECT_EQ(llm_client.last_request.target_role, "C++ Intern");
    EXPECT_EQ(llm_client.last_request.question_count, 2);
    EXPECT_TRUE(llm_client.last_request.resume_context.empty());
    EXPECT_EQ(pdf_parser.call_count, 0);
}

// 验证配置中提供简历路径时，interview 层会先解析简历，再把摘要上下文透传给取题请求。
TEST(InterviewSetupTest, ForwardsParsedResumeContextIntoQuestionGenerationRequest) {
    SetupSpyLlmClient llm_client;
    interview::common::InterviewConfig config = makeInterviewConfig(2);
    config.resume_path = "/tmp/demo_resume.pdf";
    SetupSpyPdfParser pdf_parser;
    pdf_parser.next_text = "候选人做过 C++ 日志系统，练习 RAII 和单元测试。";

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config, llm_client, pdf_parser);

    ASSERT_TRUE(prepared_interview.isReady());
    EXPECT_EQ(pdf_parser.call_count, 1);
    EXPECT_EQ(pdf_parser.last_request.file_path, "/tmp/demo_resume.pdf");
    EXPECT_EQ(llm_client.last_request.resume_context,
              "候选人做过 C++ 日志系统，练习 RAII 和单元测试。");
}

// 验证当服务层没有生成任何题目时，失败原因会由 interview 层统一产出给入口层消费。
TEST(InterviewSetupTest, ReturnsFailureWhenQuestionGenerationProducesNoQuestions) {
    interview::services::MockLlmClient llm_client;
    interview::services::MockPdfParser pdf_parser;

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(makeInterviewConfig(0), llm_client, pdf_parser);

    EXPECT_FALSE(prepared_interview.isReady());
    EXPECT_EQ(prepared_interview.getErrorMessage(), "面试启动失败：没有生成任何问题。");
    EXPECT_EQ(prepared_interview.getCandidateName(), "Test Candidate");
    EXPECT_EQ(prepared_interview.getTargetRole(), "C++ Intern");
}

// 验证简历解析返回空文本时启动失败，避免后续 LLM 拿到“有路径但没有上下文”的半成品输入。
TEST(InterviewSetupTest, ReturnsFailureWhenResumeParserProducesNoText) {
    SetupSpyLlmClient llm_client;
    interview::common::InterviewConfig config = makeInterviewConfig(2);
    config.resume_path = "/tmp/empty_resume.pdf";
    SetupSpyPdfParser pdf_parser;
    pdf_parser.next_text = "";

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config, llm_client, pdf_parser);

    EXPECT_FALSE(prepared_interview.isReady());
    EXPECT_EQ(prepared_interview.getErrorMessage(), "面试启动失败：简历没有解析出可用文本。");
    EXPECT_EQ(pdf_parser.call_count, 1);
}

// 验证简历解析异常会被 interview 层收口成启动失败信息，CLI 不需要知道 PDF 库细节。
TEST(InterviewSetupTest, ReturnsFailureWhenResumeParserThrows) {
    SetupSpyLlmClient llm_client;
    interview::common::InterviewConfig config = makeInterviewConfig(2);
    config.resume_path = "/tmp/broken_resume.pdf";
    SetupSpyPdfParser pdf_parser;
    pdf_parser.should_throw = true;

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config, llm_client, pdf_parser);

    EXPECT_FALSE(prepared_interview.isReady());
    EXPECT_EQ(prepared_interview.getErrorMessage(), "面试启动失败：简历解析失败：bad pdf fixture");
    EXPECT_EQ(pdf_parser.call_count, 1);
}
