#include <gtest/gtest.h>

#include "session/dialog_session.h"

// 新建会话时应处于连接状态，且还没有进入结束态。
TEST(DialogSessionTest, StartsWithConnectingState) {
    interview::session::DialogSession session;

    EXPECT_EQ(session.getState(), interview::session::InterviewState::kConnecting);
    EXPECT_FALSE(session.isFinished());
}

// 切换到普通中间状态后，应能正确读回该状态。
TEST(DialogSessionTest, CanChangeState) {
    interview::session::DialogSession session;

    session.setState(interview::session::InterviewState::kIdle);

    EXPECT_EQ(session.getState(), interview::session::InterviewState::kIdle);
    EXPECT_FALSE(session.isFinished());
}

// 只有完成态和错误态会被视为“会话已结束”。
TEST(DialogSessionTest, FinishedWhenCompletedOrError) {
    interview::session::DialogSession session;

    session.setState(interview::session::InterviewState::kCompleted);
    EXPECT_TRUE(session.isFinished());

    session.setState(interview::session::InterviewState::kError);
    EXPECT_TRUE(session.isFinished());
}

// 新会话不应预先带有任何回答历史。
TEST(DialogSessionTest, StartsWithEmptyCandidateAnswerHistory) {
    interview::session::DialogSession session;

    EXPECT_EQ(session.getCandidateAnswerCount(), 0u);
    EXPECT_TRUE(session.getCandidateAnswers().empty());
}

// 单条回答应能被正常保存。
TEST(DialogSessionTest, CanStoreSingleCandidateAnswer) {
    interview::session::DialogSession session;

    session.addCandidateAnswer("I built a small C++ logger.");

    ASSERT_EQ(session.getCandidateAnswerCount(), 1u);
    ASSERT_EQ(session.getCandidateAnswers().size(), 1u);
    EXPECT_EQ(session.getCandidateAnswers().front(), "I built a small C++ logger.");
}

// 多条回答需要保持追加顺序，方便后续逐题总结。
TEST(DialogSessionTest, PreservesCandidateAnswerOrder) {
    interview::session::DialogSession session;

    session.addCandidateAnswer("First answer");
    session.addCandidateAnswer("Second answer");
    session.addCandidateAnswer("Third answer");

    ASSERT_EQ(session.getCandidateAnswerCount(), 3u);
    EXPECT_EQ(session.getCandidateAnswers()[0], "First answer");
    EXPECT_EQ(session.getCandidateAnswers()[1], "Second answer");
    EXPECT_EQ(session.getCandidateAnswers()[2], "Third answer");
}

// 新会话不应预先带有任何评分结果。
TEST(DialogSessionTest, StartsWithEmptyScoreResultHistory) {
    interview::session::DialogSession session;

    EXPECT_EQ(session.getScoreResultCount(), 0u);
    EXPECT_TRUE(session.getScoreResults().empty());
}

// 单条评分结果应能被正常保存，供总结阶段读取。
TEST(DialogSessionTest, CanStoreSingleScoreResult) {
    interview::session::DialogSession session;

    session.addScoreResult(82, "Good answer, but add one concrete example.");

    ASSERT_EQ(session.getScoreResultCount(), 1u);
    ASSERT_EQ(session.getScoreResults().size(), 1u);
    EXPECT_EQ(session.getScoreResults().front().score, 82);
    EXPECT_EQ(session.getScoreResults().front().feedback, "Good answer, but add one concrete example.");
}

// 多条评分结果需要保持追加顺序，确保和题目、回答逐题对齐。
TEST(DialogSessionTest, PreservesScoreResultOrder) {
    interview::session::DialogSession session;

    session.addScoreResult(55, "Basic answer, but it needs more detail.");
    session.addScoreResult(90, "Strong answer with concrete detail.");

    ASSERT_EQ(session.getScoreResultCount(), 2u);
    EXPECT_EQ(session.getScoreResults()[0].score, 55);
    EXPECT_EQ(session.getScoreResults()[0].feedback, "Basic answer, but it needs more detail.");
    EXPECT_EQ(session.getScoreResults()[1].score, 90);
    EXPECT_EQ(session.getScoreResults()[1].feedback, "Strong answer with concrete detail.");
}

// 新会话不应预先带有完整问答记录。
TEST(DialogSessionTest, StartsWithEmptyQuestionAnswerRecordHistory) {
    interview::session::DialogSession session;

    EXPECT_EQ(session.getQuestionAnswerRecordCount(), 0u);
    EXPECT_TRUE(session.getQuestionAnswerRecords().empty());
}

// 不需要追问时，完整记录只保存主问题、主回答和最终评分。
TEST(DialogSessionTest, CanStoreQuestionAnswerRecordWithoutFollowUp) {
    interview::session::DialogSession session;

    interview::session::QuestionAnswerRecord record;
    record.question = "What is one C++ concept you are learning?";
    record.candidate_answer = "I am learning class design with a small project.";
    record.final_score = {92, "Strong answer with concrete detail."};

    session.addQuestionAnswerRecord(record);

    ASSERT_EQ(session.getQuestionAnswerRecordCount(), 1u);
    const interview::session::QuestionAnswerRecord& saved_record = session.getQuestionAnswerRecords().front();
    EXPECT_EQ(saved_record.question, "What is one C++ concept you are learning?");
    EXPECT_EQ(saved_record.candidate_answer, "I am learning class design with a small project.");
    EXPECT_FALSE(saved_record.has_follow_up);
    EXPECT_TRUE(saved_record.follow_up_prompt.empty());
    EXPECT_TRUE(saved_record.follow_up_answer.empty());
    EXPECT_EQ(saved_record.final_score.score, 92);
    EXPECT_EQ(saved_record.final_score.feedback, "Strong answer with concrete detail.");
}

// 需要追问时，完整记录要把追问提示和追问回答分开保存。
TEST(DialogSessionTest, CanStoreQuestionAnswerRecordWithFollowUp) {
    interview::session::DialogSession session;

    interview::session::QuestionAnswerRecord record;
    record.question = "Which project detail would you improve next?";
    record.candidate_answer = "I would improve the CLI flow.";
    record.has_follow_up = true;
    record.follow_up_prompt = "Could you explain one specific design choice or tradeoff in more detail?";
    record.follow_up_answer = "I would separate input collection from summary formatting.";
    record.final_score = {86, "Strong answer with concrete detail."};

    session.addQuestionAnswerRecord(record);

    ASSERT_EQ(session.getQuestionAnswerRecordCount(), 1u);
    const interview::session::QuestionAnswerRecord& saved_record = session.getQuestionAnswerRecords().front();
    EXPECT_EQ(saved_record.question, "Which project detail would you improve next?");
    EXPECT_EQ(saved_record.candidate_answer, "I would improve the CLI flow.");
    EXPECT_TRUE(saved_record.has_follow_up);
    EXPECT_EQ(saved_record.follow_up_prompt,
              "Could you explain one specific design choice or tradeoff in more detail?");
    EXPECT_EQ(saved_record.follow_up_answer, "I would separate input collection from summary formatting.");
    EXPECT_EQ(saved_record.final_score.score, 86);
    EXPECT_EQ(saved_record.final_score.feedback, "Strong answer with concrete detail.");
}
