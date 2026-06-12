#include "session/interview_report.h"

namespace interview {
namespace session {

namespace {

// 评分本身是问答记录的一部分，单独转换能让主记录序列化更清楚。
nlohmann::json buildScoreJson(const ScoreResultRecord& score_result) {
    return nlohmann::json{{"score", score_result.score}, {"feedback", score_result.feedback}};
}

// 每条问答记录保留主问题、主回答、追问信息和最终评分，避免展示层重新理解业务结构。
nlohmann::json buildQuestionAnswerRecordJson(const QuestionAnswerRecord& record) {
    return nlohmann::json{{"question", record.question},
                          {"candidate_answer", record.candidate_answer},
                          {"has_follow_up", record.has_follow_up},
                          {"follow_up_prompt", record.follow_up_prompt},
                          {"follow_up_answer", record.follow_up_answer},
                          {"final_score", buildScoreJson(record.final_score)}};
}

} // namespace

nlohmann::json buildInterviewReportJson(const DialogSession& session) {
    nlohmann::json records = nlohmann::json::array();
    for (const QuestionAnswerRecord& record : session.getQuestionAnswerRecords()) {
        records.push_back(buildQuestionAnswerRecordJson(record));
    }

    // 返回新的 JSON 对象，不暴露 DialogSession 内部容器，也不修改会话状态。
    return nlohmann::json{{"question_count", session.getQuestionAnswerRecordCount()},
                          {"question_answer_records", records}};
}

} // namespace session
} // namespace interview
