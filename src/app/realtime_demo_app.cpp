#include "app/realtime_demo_app.h"

#include "services/realtime/mock/mock_realtime_client.h"
#include "services/realtime/realtime_client.h"
#include "session/dialog_orchestrator.h"
#include "session/interview_report.h"

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
                                   const std::string& provider_name) {
    output << "=== Realtime " << provider_name << " Demo ===\n";
    if (!prepared_interview.isReady()) {
        output << prepared_interview.getErrorMessage() << '\n';
        return 1;
    }

    // 这里通过 IRealtimeClient 抽象启动编排层：
    // mock provider 会读确定性脚本；火山 provider 会在 connect() 里建立真实 WSS。
    // 未来接入音频时，app 层仍不需要知道麦克风、TTS 或供应商 frame 的细节。
    session::DialogOrchestrator orchestrator(prepared_interview, realtime_client);
    const session::DialogOrchestratorResult result = orchestrator.run();

    printInterviewerMessages(result.interviewer_messages, output);
    if (!result.success) {
        output << "Realtime demo 失败：" << result.error_message << '\n';
        return 1;
    }

    const nlohmann::json report = session::buildInterviewReportJson(result.session);
    output << "\n=== Realtime " << provider_name << " 报告 JSON ===\n";
    output << report.dump(2) << '\n';
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
