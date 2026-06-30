#include "common/config.h"
#include "services/llm_client.h"
#include "session/interview_setup.h"

#include <gtest/gtest.h>
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

} // namespace

// 验证 interview 层会把配置和题目生成真正收口成一个可运行的面试上下文。
TEST(InterviewSetupTest, PreparesInterviewContextWithGeneratedQuestions) {
    interview::services::MockLlmClient llm_client;

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(makeInterviewConfig(2), llm_client);

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

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config, llm_client);

    ASSERT_TRUE(prepared_interview.isReady());
    EXPECT_EQ(llm_client.last_request.candidate_name, "Test Candidate");
    EXPECT_EQ(llm_client.last_request.target_role, "C++ Intern");
    EXPECT_EQ(llm_client.last_request.question_count, 2);
}

// 验证当服务层没有生成任何题目时，失败原因会由 interview 层统一产出给入口层消费。
TEST(InterviewSetupTest, ReturnsFailureWhenQuestionGenerationProducesNoQuestions) {
    interview::services::MockLlmClient llm_client;

    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(makeInterviewConfig(0), llm_client);

    EXPECT_FALSE(prepared_interview.isReady());
    EXPECT_EQ(prepared_interview.getErrorMessage(), "面试启动失败：没有生成任何问题。");
    EXPECT_EQ(prepared_interview.getCandidateName(), "Test Candidate");
    EXPECT_EQ(prepared_interview.getTargetRole(), "C++ Intern");
}
