#include "app/cli_interview_app.h"

#include "session/dialog_session.h"
#include "session/interview_report.h"
#include "session/interview_state.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace interview {
namespace app {

namespace {

// 把内部状态枚举转换成 CLI 更容易读懂的中文标签，运行时不暴露内部枚举名。
std::string interviewStateToString(session::InterviewState state) {
    switch (state) {
    case session::InterviewState::kConnecting:
        return "连接中";
    case session::InterviewState::kInterviewerSpeaking:
        return "面试官提问中";
    case session::InterviewState::kIdle:
        return "等待中";
    case session::InterviewState::kCandidateSpeaking:
        return "候选人回答中";
    case session::InterviewState::kInterviewerThinking:
        return "面试官思考中";
    case session::InterviewState::kSessionEnding:
        return "会话收尾中";
    case session::InterviewState::kCompleted:
        return "已完成";
    case session::InterviewState::kError:
        return "错误";
    }

    return "未知状态";
}

// 统一做状态切换并打印变化，便于测试和手动运行都观察同一条状态流。
void transitionState(session::DialogSession& interview_session, session::InterviewState next_state,
                     std::ostream& output) {
    const session::InterviewState current_state = interview_session.getState();
    interview_session.setState(next_state);
    output << "[状态] " << interviewStateToString(current_state) << " -> "
           << interviewStateToString(next_state) << '\n';
}

// 总结阶段统一打印结构化问答记录，帮助用户回看主回答和追问表现。
void printSummary(std::size_t question_count, const session::DialogSession& interview_session,
                  std::ostream& output) {
    const std::vector<session::QuestionAnswerRecord>& records =
        interview_session.getQuestionAnswerRecords();

    output << "\n=== 面试总结 ===\n";
    output << "已回答 " << records.size() << " / " << question_count << " 道题。\n";

    const std::size_t item_count = std::min(question_count, records.size());
    for (std::size_t index = 0; index < item_count; ++index) {
        const session::QuestionAnswerRecord& record = records[index];
        output << index + 1 << ". 题目：" << record.question << '\n';
        output << "   回答：" << record.candidate_answer << '\n';
        if (record.has_follow_up) {
            output << "   追问：" << record.follow_up_prompt << '\n';
            output << "   追问回答：" << record.follow_up_answer << '\n';
        }
        output << "   得分：" << record.final_score.score << "/100 - "
               << record.final_score.feedback << '\n';
    }
}

// 报告既打印为 JSON 便于学习，也可由调用方显式配置为持久化文件。
void printReportJson(const session::DialogSession& interview_session, std::ostream& output) {
    const nlohmann::json report = session::buildInterviewReportJson(interview_session);
    output << "\n=== 面试报告 JSON ===\n";
    output << report.dump(2) << '\n';
}

// 把导出失败收口在 app 层：报告正文不会进入日志，终端只显示用户指定目录中的最终路径或错误摘要。
bool exportReportIfConfigured(const session::DialogSession& interview_session,
                              const std::string& output_directory, std::ostream& output) {
    if (output_directory.empty()) {
        return true;
    }

    try {
        const std::filesystem::path report_path =
            session::createInterviewReportPath(output_directory);
        session::saveInterviewReportJson(interview_session, report_path);
        output << "报告已保存到：" << report_path.string() << '\n';
        return true;
    } catch (const std::exception& error) {
        output << "报告导出失败：" << error.what() << '\n';
        return false;
    }
}

} // namespace

int runCliInterview(std::istream& input, std::ostream& output,
                    session::PreparedInterview& prepared_interview,
                    const std::string& report_output_directory) {
    if (!prepared_interview.isReady()) {
        output << prepared_interview.getErrorMessage() << '\n';
        return 1;
    }

    session::DialogSession interview_session;
    session::InterviewManager& manager = prepared_interview.getManager();

    // 开场阶段：先打招呼，再把会话切回等待状态。
    output << "=== AI 模拟面试 CLI 演示 ===\n";
    transitionState(interview_session, session::InterviewState::kInterviewerSpeaking, output);
    output << "面试官：欢迎你，" << prepared_interview.getCandidateName() << "。";
    output << "本次模拟面试岗位是 " << prepared_interview.getTargetRole() << "。\n";
    transitionState(interview_session, session::InterviewState::kIdle, output);

    std::size_t question_number = 1;
    while (manager.hasCurrentQuestion()) {
        // 每轮先取当前题目；如果为空，说明流程状态已经不一致。
        const std::string* current_question = manager.getCurrentQuestion();
        if (current_question == nullptr) {
            transitionState(interview_session, session::InterviewState::kError, output);
            output << "面试已停止：没有找到当前题目。\n";
            return 1;
        }

        // 面试官提问阶段。
        transitionState(interview_session, session::InterviewState::kInterviewerSpeaking, output);
        output << "\n问题 " << question_number << "/" << manager.getQuestionCount() << "："
               << *current_question << '\n';

        // 候选人输入阶段。
        transitionState(interview_session, session::InterviewState::kCandidateSpeaking, output);
        output << "你的回答：";

        std::string answer;
        if (!std::getline(input, answer)) {
            transitionState(interview_session, session::InterviewState::kError, output);
            output << "\n输入提前结束。\n";
            return 1;
        }

        // 思考阶段：先评分，再决定是否进入下一题。
        transitionState(interview_session, session::InterviewState::kInterviewerThinking, output);
        if (!manager.recordCandidateAnswer(interview_session, answer)) {
            transitionState(interview_session, session::InterviewState::kError, output);
            output << "面试已停止：回答无法被记录。\n";
            return 1;
        }

        const services::LlmScoreResult score_result = manager.scoreCandidateAnswer(answer);
        output << "得分：" << score_result.score << "/100 - " << score_result.feedback << '\n';

        std::string follow_up_answer;
        services::LlmScoreResult final_score_result = score_result;
        const session::FollowUpDecision follow_up_decision = manager.decideFollowUp(score_result);

        if (follow_up_decision.needs_follow_up) {
            transitionState(interview_session, session::InterviewState::kInterviewerSpeaking,
                            output);
            output << "追问：" << follow_up_decision.prompt << '\n';

            transitionState(interview_session, session::InterviewState::kCandidateSpeaking, output);
            output << "你的追问回答：";

            if (!std::getline(input, follow_up_answer)) {
                transitionState(interview_session, session::InterviewState::kError, output);
                output << "\n输入提前结束。\n";
                return 1;
            }

            transitionState(interview_session, session::InterviewState::kInterviewerThinking,
                            output);
            final_score_result = manager.scoreCandidateAnswer(answer + " " + follow_up_answer);
            output << "更新后得分：" << final_score_result.score << "/100 - "
                   << final_score_result.feedback << '\n';
        }

        session::QuestionAnswerRecord record;
        record.question = *current_question;
        record.candidate_answer = answer;
        record.has_follow_up = follow_up_decision.needs_follow_up;
        record.follow_up_prompt = follow_up_decision.prompt;
        record.follow_up_answer = follow_up_answer;
        record.final_score = {final_score_result.score, final_score_result.feedback};
        // 评分历史和结构化记录都写入会话，后续报告导出和 UI 读取可复用同一份数据。
        interview_session.addScoreResult(final_score_result.score, final_score_result.feedback);
        interview_session.addQuestionAnswerRecord(record);

        manager.moveToNextQuestion();
        ++question_number;

        if (manager.hasCurrentQuestion()) {
            // 还有下一题时，回到空闲态等待下一轮开始。
            transitionState(interview_session, session::InterviewState::kIdle, output);
        }
    }

    // 所有题目结束后，进入总结和收尾阶段。
    transitionState(interview_session, session::InterviewState::kSessionEnding, output);
    printSummary(manager.getQuestionCount(), interview_session, output);
    printReportJson(interview_session, output);
    if (!exportReportIfConfigured(interview_session, report_output_directory, output)) {
        transitionState(interview_session, session::InterviewState::kError, output);
        return 1;
    }
    transitionState(interview_session, session::InterviewState::kCompleted, output);
    output << "面试完成。\n";
    return 0;
}

} // namespace app
} // namespace interview
