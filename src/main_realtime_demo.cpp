#include "app/realtime_demo_app.h"
#include "common/config.h"
#include "common/logger.h"
#include "services/audio/portaudio/portaudio_audio_device.h"
#include "services/llm/llm_client_factory.h"
#include "services/pdf/podofo/podofo_pdf_parser.h"
#include "services/realtime/realtime_client_factory.h"
#include "session/interview_setup.h"
#include "session/realtime_audio_bridge.h"

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
        const std::string report_output_directory =
            config.report.save_json ? config.report.output_directory : "";
        if (config.realtime.provider == "mock") {
            return interview::app::runConfiguredRealtimeInterview(
                std::cout, prepared_interview, *realtime_client, config.realtime.provider, nullptr,
                report_output_directory);
        }

        if (config.realtime.dialog.input_mod == "audio") {
            // PortAudio callback 只写/读内部 PCM 队列；runConfiguredRealtimeInterview
            // 所在的当前线程 是 realtime client 唯一所有者。将来 Qt 必须把整个调用迁入可 join 的
            // worker。
            interview::services::PortAudioAudioDevice audio_device;
            const interview::services::AudioPcmFormat capture_format = {
                config.realtime.audio.capture_sample_rate_hz,
                config.realtime.audio.capture_channels,
                config.realtime.audio.frames_per_buffer,
            };
            const interview::services::AudioPcmFormat playback_format = {
                config.realtime.tts.sample_rate_hz,
                config.realtime.tts.channels,
                config.realtime.audio.frames_per_buffer,
            };
            interview::session::RealtimeAudioBridge audio_bridge(audio_device, *realtime_client,
                                                                 capture_format, playback_format);
            return interview::app::runConfiguredRealtimeInterview(
                std::cout, prepared_interview, *realtime_client, config.realtime.provider,
                &audio_bridge, report_output_directory);
        }

        // text 模式继续保留最小 WSS smoke test，避免在没有麦克风的机器上误进入完整音频循环。
        return interview::app::runRealtimeConnectionSmoke(std::cout, *realtime_client,
                                                          config.realtime.provider);
    } catch (const std::exception& error) {
        std::cerr << "Realtime demo 初始化失败：" << error.what() << '\n';
        return 1;
    }
}
