#include "app/realtime_demo_app.h"
#include "common/config.h"
#include "common/logger.h"
#include "services/llm_client_factory.h"
#include "services/pdf_parser.h"
#include "services/realtime_client_factory.h"
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
        if (prepared_interview.isReady() && config.realtime.provider == "mock") {
            // 题目数量来自已经准备好的 InterviewManager，确保 demo 脚本和真实题目流一致。
            scripted_events = interview::app::buildDefaultRealtimeDemoScript(
                prepared_interview.getManager().getQuestionCount());
        }

        std::unique_ptr<interview::services::IRealtimeClient> realtime_client =
            interview::services::createRealtimeClient(config.realtime, scripted_events);
        if (config.realtime.provider == "mock") {
            return interview::app::runConfiguredRealtimeInterview(
                std::cout, prepared_interview, *realtime_client, config.realtime.provider);
        }

        // 当前真实火山 provider 只完成 text/WSS 连接和发送文本的闭环。
        // 完整面试循环需要候选人 transcript，后续接 IAudioDevice/PortAudio 后再打开。
        return interview::app::runRealtimeConnectionSmoke(std::cout, *realtime_client,
                                                          config.realtime.provider);
    } catch (const std::exception& error) {
        std::cerr << "Realtime demo 初始化失败：" << error.what() << '\n';
        return 1;
    }
}
