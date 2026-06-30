// clang-format off
#include <cstddef>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "session/dialog_session.h"
#include "session/interview_report.h"
// clang-format on

namespace {

interview::session::QuestionAnswerRecord makeRecord(const std::string& question,
                                                    const std::string& answer, int score) {
    interview::session::QuestionAnswerRecord record;
    record.question = question;
    record.candidate_answer = answer;
    record.final_score = {score, "生成的反馈。"};
    return record;
}

} // namespace

// 空会话也应该生成稳定结构，方便调用方不用额外判断“没有记录”的特殊情况。
TEST(InterviewReportTest, BuildsEmptyReportForNewSession) {
    const interview::session::DialogSession session;

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);

    EXPECT_EQ(report.at("question_count"), 0u);
    ASSERT_TRUE(report.at("question_answer_records").is_array());
    EXPECT_TRUE(report.at("question_answer_records").empty());
}

// 无追问记录是最常见路径，需要保留主问题、主回答和最终评分。
TEST(InterviewReportTest, SerializesQuestionAnswerRecordWithoutFollowUp) {
    interview::session::DialogSession session;

    interview::session::QuestionAnswerRecord record;
    record.question = "你最近在学习哪个 C++ 概念？";
    record.candidate_answer = "我在一个小项目里练习类设计。";
    record.final_score = {92, "回答扎实，包含具体细节。"};
    session.addQuestionAnswerRecord(record);

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);
    const nlohmann::json& saved_record = report.at("question_answer_records").at(0);

    EXPECT_EQ(report.at("question_count"), 1u);
    EXPECT_EQ(saved_record.at("question"), "你最近在学习哪个 C++ 概念？");
    EXPECT_EQ(saved_record.at("candidate_answer"), "我在一个小项目里练习类设计。");
    EXPECT_FALSE(saved_record.at("has_follow_up"));
    EXPECT_EQ(saved_record.at("follow_up_prompt"), "");
    EXPECT_EQ(saved_record.at("follow_up_answer"), "");
    EXPECT_EQ(saved_record.at("final_score").at("score"), 92);
    EXPECT_EQ(saved_record.at("final_score").at("feedback"), "回答扎实，包含具体细节。");
}

// 有追问时，追问提示和追问回答必须分开保存，后续报告才能清楚展示对话层次。
TEST(InterviewReportTest, SerializesQuestionAnswerRecordWithFollowUp) {
    interview::session::DialogSession session;

    interview::session::QuestionAnswerRecord record;
    record.question = "你接下来想改进哪个项目细节？";
    record.candidate_answer = "我想改进 CLI 流程。";
    record.has_follow_up = true;
    record.follow_up_prompt = "能不能再展开一个具体设计选择或取舍？";
    record.follow_up_answer = "我会把输入收集和总结格式化分开。";
    record.final_score = {86, "回答扎实，包含具体细节。"};
    session.addQuestionAnswerRecord(record);

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);
    const nlohmann::json& saved_record = report.at("question_answer_records").at(0);

    EXPECT_TRUE(saved_record.at("has_follow_up"));
    EXPECT_EQ(saved_record.at("follow_up_prompt"), "能不能再展开一个具体设计选择或取舍？");
    EXPECT_EQ(saved_record.at("follow_up_answer"), "我会把输入收集和总结格式化分开。");
    EXPECT_EQ(saved_record.at("final_score").at("score"), 86);
}

// 报告数组顺序要和会话记录顺序一致，避免总结阶段出现题目和回答错位。
TEST(InterviewReportTest, PreservesQuestionAnswerRecordOrder) {
    interview::session::DialogSession session;
    session.addQuestionAnswerRecord(makeRecord("问题一", "回答一", 70));
    session.addQuestionAnswerRecord(makeRecord("问题二", "回答二", 80));
    session.addQuestionAnswerRecord(makeRecord("问题三", "回答三", 90));

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);
    const nlohmann::json& records = report.at("question_answer_records");

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records.at(0).at("question"), "问题一");
    EXPECT_EQ(records.at(1).at("question"), "问题二");
    EXPECT_EQ(records.at(2).at("question"), "问题三");
}

// 序列化应该是只读操作，生成报告不能顺手修改 DialogSession 内部状态。
TEST(InterviewReportTest, DoesNotModifyDialogSession) {
    interview::session::DialogSession session;
    session.addQuestionAnswerRecord(makeRecord("问题一", "回答一", 70));

    const std::size_t record_count_before = session.getQuestionAnswerRecordCount();
    const std::string first_question_before = session.getQuestionAnswerRecords().front().question;

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);

    EXPECT_EQ(report.at("question_count"), 1u);
    EXPECT_EQ(session.getQuestionAnswerRecordCount(), record_count_before);
    ASSERT_FALSE(session.getQuestionAnswerRecords().empty());
    EXPECT_EQ(session.getQuestionAnswerRecords().front().question, first_question_before);
}
