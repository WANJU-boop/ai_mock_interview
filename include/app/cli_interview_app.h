#pragma once

#include "session/interview_setup.h"

#include <istream>
#include <ostream>

namespace interview {
namespace app {

// 运行一次基于终端输入输出流的 mock 面试流程。
// 这样 main 可以只保留初始化和面试准备逻辑，测试则能用字符串流覆盖完整交互链路。
int runCliInterview(std::istream& input, std::ostream& output,
                    session::PreparedInterview& prepared_interview);

} // namespace app
} // namespace interview
