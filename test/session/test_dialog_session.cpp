#include <gtest/gtest.h>

#include "session/dialog_session.h"

using interview::session::DialogSession;
using interview::session::InterviewState;

TEST(DialogSessionTest, StartsWithConnectingState) {
  DialogSession session;

  EXPECT_EQ(session.getState(), InterviewState::kConnecting);
  EXPECT_FALSE(session.isFinished());
}

TEST(DialogSessionTest, CanChangeState) {
  DialogSession session;

  session.setState(InterviewState::kIdle);

  EXPECT_EQ(session.getState(), InterviewState::kIdle);
  EXPECT_FALSE(session.isFinished());
}

TEST(DialogSessionTest, FinishedWhenCompletedOrError) {
  DialogSession session;

  session.setState(InterviewState::kCompleted);
  EXPECT_TRUE(session.isFinished());

  session.setState(InterviewState::kError);
  EXPECT_TRUE(session.isFinished());
}
