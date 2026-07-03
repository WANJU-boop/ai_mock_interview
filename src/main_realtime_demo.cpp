#include "app/realtime_demo_app.h"
#include "common/config.h"
#include "common/logger.h"
#include "services/llm_client_factory.h"
#include "services/pdf_parser.h"
#include "session/interview_setup.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    // realtime demo 仍复用当前配置和取题链路，只把候选人输入替换成 mock realtime 事件脚本。
    interview::common::Logger::Init("interview.log", false);

    const std::string config_path =
        argc > 1 ? argv[1] : interview::common::findDefaultConfigPath(argv[0]);

    try {
        const interview::common::AppConfig config =
            interview::common::loadConfigFromFile(config_path);
        std::unique_ptr<interview::services::ILlmClient> llm_client =
            interview::services::createLlmClient(config.llm);
        interview::services::PodofoPdfParser pdf_parser;
        interview::session::PreparedInterview prepared_interview =
            interview::session::prepareInterview(config.interview, *llm_client, pdf_parser);

        std::vector<interview::common::RealtimeEvent> scripted_events;
        if (prepared_interview.isReady()) {
            // 题目数量来自已经准备好的 InterviewManager，确保 demo 脚本和真实题目流一致。
            scripted_events = interview::app::buildDefaultRealtimeDemoScript(
                prepared_interview.getManager().getQuestionCount());
        }

        return interview::app::runRealtimeDemoInterview(std::cout, prepared_interview,
                                                        scripted_events);
    } catch (const std::exception& error) {
        std::cerr << "Realtime demo 初始化失败：" << error.what() << '\n';
        return 1;
    }
}
