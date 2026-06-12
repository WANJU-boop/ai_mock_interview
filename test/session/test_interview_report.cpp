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
    record.final_score = {score, "Generated feedback."};
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
    record.question = "What is one C++ concept you are learning?";
    record.candidate_answer = "I am learning class design with a small project.";
    record.final_score = {92, "Strong answer with concrete detail."};
    session.addQuestionAnswerRecord(record);

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);
    const nlohmann::json& saved_record = report.at("question_answer_records").at(0);

    EXPECT_EQ(report.at("question_count"), 1u);
    EXPECT_EQ(saved_record.at("question"), "What is one C++ concept you are learning?");
    EXPECT_EQ(saved_record.at("candidate_answer"),
              "I am learning class design with a small project.");
    EXPECT_FALSE(saved_record.at("has_follow_up"));
    EXPECT_EQ(saved_record.at("follow_up_prompt"), "");
    EXPECT_EQ(saved_record.at("follow_up_answer"), "");
    EXPECT_EQ(saved_record.at("final_score").at("score"), 92);
    EXPECT_EQ(saved_record.at("final_score").at("feedback"), "Strong answer with concrete detail.");
}

// 有追问时，追问提示和追问回答必须分开保存，后续报告才能清楚展示对话层次。
TEST(InterviewReportTest, SerializesQuestionAnswerRecordWithFollowUp) {
    interview::session::DialogSession session;

    interview::session::QuestionAnswerRecord record;
    record.question = "Which project detail would you improve next?";
    record.candidate_answer = "I would improve the CLI flow.";
    record.has_follow_up = true;
    record.follow_up_prompt =
        "Could you explain one specific design choice or tradeoff in more detail?";
    record.follow_up_answer = "I would separate input collection from summary formatting.";
    record.final_score = {86, "Strong answer with concrete detail."};
    session.addQuestionAnswerRecord(record);

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);
    const nlohmann::json& saved_record = report.at("question_answer_records").at(0);

    EXPECT_TRUE(saved_record.at("has_follow_up"));
    EXPECT_EQ(saved_record.at("follow_up_prompt"),
              "Could you explain one specific design choice or tradeoff in more detail?");
    EXPECT_EQ(saved_record.at("follow_up_answer"),
              "I would separate input collection from summary formatting.");
    EXPECT_EQ(saved_record.at("final_score").at("score"), 86);
}

// 报告数组顺序要和会话记录顺序一致，避免总结阶段出现题目和回答错位。
TEST(InterviewReportTest, PreservesQuestionAnswerRecordOrder) {
    interview::session::DialogSession session;
    session.addQuestionAnswerRecord(makeRecord("Question one", "Answer one", 70));
    session.addQuestionAnswerRecord(makeRecord("Question two", "Answer two", 80));
    session.addQuestionAnswerRecord(makeRecord("Question three", "Answer three", 90));

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);
    const nlohmann::json& records = report.at("question_answer_records");

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records.at(0).at("question"), "Question one");
    EXPECT_EQ(records.at(1).at("question"), "Question two");
    EXPECT_EQ(records.at(2).at("question"), "Question three");
}

// 序列化应该是只读操作，生成报告不能顺手修改 DialogSession 内部状态。
TEST(InterviewReportTest, DoesNotModifyDialogSession) {
    interview::session::DialogSession session;
    session.addQuestionAnswerRecord(makeRecord("Question one", "Answer one", 70));

    const std::size_t record_count_before = session.getQuestionAnswerRecordCount();
    const std::string first_question_before = session.getQuestionAnswerRecords().front().question;

    const nlohmann::json report = interview::session::buildInterviewReportJson(session);

    EXPECT_EQ(report.at("question_count"), 1u);
    EXPECT_EQ(session.getQuestionAnswerRecordCount(), record_count_before);
    ASSERT_FALSE(session.getQuestionAnswerRecords().empty());
    EXPECT_EQ(session.getQuestionAnswerRecords().front().question, first_question_before);
}
