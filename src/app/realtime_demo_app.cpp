#include "app/realtime_demo_app.h"

#include "services/realtime/mock/mock_realtime_client.h"
#include "services/realtime/realtime_client.h"
#include "session/dialog_orchestrator.h"
#include "session/interview_report.h"

#include <exception>
#include <filesystem>
#include <string>

namespace interview {
namespace app {

namespace {

common::RealtimeEvent makeRealtimeEvent(common::RealtimeEventType type, const std::string& text) {
    common::RealtimeEvent event;
    event.type = type;
    event.text = text;
    return event;
}

std::string defaultStrongAnswer() {
    // 默认脚本刻意使用稳定的高质量回答，避免 demo 因追问分支而需要额外事件。
    return "我最近做了一个 C++ 日志项目，练习 RAII、所有权、测试、调试和设计取舍。"
           "我会说明为什么用接口隔离日志输出，如何用单元测试验证边界，"
           "并复盘资源管理中的具体代码改动。";
}

void printInterviewerMessages(const std::vector<std::string>& messages, std::ostream& output) {
    for (const std::string& message : messages) {
        output << "面试官：" << message << '\n';
    }
}

// realtime 和 CLI 共用同一个 session 报告格式；这里只负责把成功结果保存到本地，
// 不把候选人正文写入日志或返回给外部服务。
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

std::vector<common::RealtimeEvent> buildDefaultRealtimeDemoScript(std::size_t question_count) {
    std::vector<common::RealtimeEvent> events;
    events.reserve(question_count + 1);
    events.push_back({common::RealtimeEventType::kConnected, "", "", {}});
    for (std::size_t index = 0; index < question_count; ++index) {
        events.push_back(
            makeRealtimeEvent(common::RealtimeEventType::kTranscriptFinal, defaultStrongAnswer()));
    }

    return events;
}

int runRealtimeDemoInterview(std::ostream& output, session::PreparedInterview& prepared_interview,
                             const std::vector<common::RealtimeEvent>& scripted_events) {
    services::MockRealtimeClient realtime_client(scripted_events);
    return runConfiguredRealtimeInterview(output, prepared_interview, realtime_client, "mock");
}

int runConfiguredRealtimeInterview(std::ostream& output,
                                   session::PreparedInterview& prepared_interview,
                                   services::IRealtimeClient& realtime_client,
                                   const std::string& provider_name,
                                   session::RealtimeAudioBridge* audio_bridge,
                                   const std::string& report_output_directory) {
    output << "=== Realtime " << provider_name << " Demo ===\n";
    if (!prepared_interview.isReady()) {
        output << prepared_interview.getErrorMessage() << '\n';
        return 1;
    }

    // 这里通过 IRealtimeClient 抽象启动编排层：
    // mock provider 会读确定性脚本；火山 provider 会在 connect() 里建立真实 WSS。
    // 未来接入音频时，app 层仍不需要知道麦克风、TTS 或供应商 frame 的细节。
    // audio_bridge 为空时保持 mock/text 的旧行为；audio 模式由入口显式构造并交给同一个
    // synchronous worker，确保 app 层不直接调用 PortAudio 或 websocket 细节。
    session::DialogOrchestrator orchestrator(prepared_interview, realtime_client, audio_bridge);
    const session::DialogOrchestratorResult result = orchestrator.run();

    printInterviewerMessages(result.interviewer_messages, output);
    if (!result.success) {
        output << "Realtime demo 失败：" << result.error_message << '\n';
        return 1;
    }

    const nlohmann::json report = session::buildInterviewReportJson(result.session);
    output << "\n=== Realtime " << provider_name << " 报告 JSON ===\n";
    output << report.dump(2) << '\n';
    if (!exportReportIfConfigured(result.session, report_output_directory, output)) {
        return 1;
    }
    output << "Realtime " << provider_name << " 面试完成。\n";
    return 0;
}

int runRealtimeConnectionSmoke(std::ostream& output, services::IRealtimeClient& realtime_client,
                               const std::string& provider_name) {
    output << "=== Realtime " << provider_name << " Connection Smoke ===\n";

    if (!realtime_client.connect()) {
        output << "Realtime " << provider_name << " 连接失败。\n";
        realtime_client.close();
        return 1;
    }

    // 当前 smoke test 只验证 provider 可以建立会话并发送一条面试官文本。
    // 候选人语音输入要等 IAudioDevice/PortAudio 接入后，才能进入完整 DialogOrchestrator 循环。
    if (!realtime_client.sendInterviewerText("这是一条 realtime provider 连接检查文本。")) {
        output << "Realtime " << provider_name << " 发送文本失败。\n";
        realtime_client.close();
        return 1;
    }

    realtime_client.close();
    output << "Realtime " << provider_name << " 连接 smoke test 完成。\n";
    return 0;
}

} // namespace app
} // namespace interview
