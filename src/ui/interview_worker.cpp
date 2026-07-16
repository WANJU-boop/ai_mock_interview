#include "ui/interview_worker.h"

#include "app/realtime_demo_app.h"
#include "common/config.h"
#include "services/audio/portaudio/portaudio_audio_device.h"
#include "services/llm/llm_client_factory.h"
#include "services/pdf/podofo/podofo_pdf_parser.h"
#include "services/realtime/realtime_client_factory.h"
#include "session/dialog_orchestrator.h"
#include "session/interview_report.h"
#include "session/interview_setup.h"
#include "session/realtime_audio_bridge.h"

#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace interview {
namespace ui {

namespace {

QString StateText(session::InterviewState state) {
    switch (state) {
    case session::InterviewState::kConnecting:
        return QStringLiteral("正在连接");
    case session::InterviewState::kInterviewerSpeaking:
        return QStringLiteral("面试官正在说话");
    case session::InterviewState::kIdle:
        return QStringLiteral("正在准备下一步");
    case session::InterviewState::kCandidateSpeaking:
        return QStringLiteral("请开始回答");
    case session::InterviewState::kInterviewerThinking:
        return QStringLiteral("正在评分和生成反馈");
    case session::InterviewState::kSessionEnding:
        return QStringLiteral("正在生成报告");
    case session::InterviewState::kCompleted:
        return QStringLiteral("面试已完成");
    case session::InterviewState::kError:
        return QStringLiteral("面试已停止");
    }

    return QStringLiteral("未知状态");
}

std::string SaveReportIfConfigured(const common::ReportConfig& config,
                                   const session::DialogSession& interview_session) {
    if (!config.save_json) {
        return "";
    }

    const std::filesystem::path report_path =
        session::createInterviewReportPath(config.output_directory);
    session::saveInterviewReportJson(interview_session, report_path);
    return report_path.string();
}

} // namespace

InterviewWorker::InterviewWorker(
    QString config_path, std::shared_ptr<session::DialogCancellationToken> cancellation_token,
    QObject* parent)
    : QObject(parent), config_path_(std::move(config_path)),
      cancellation_token_(std::move(cancellation_token)) {}

void InterviewWorker::Run() {
    try {
        if (cancellation_token_ == nullptr) {
            throw std::runtime_error("Qt worker 缺少取消令牌。");
        }
        if (cancellation_token_->IsStopRequested()) {
            // 窗口可能在 QThread 真正调度 Run 前就已关闭；此时不读取简历或创建任何外部服务。
            emit Finished(false, QStringLiteral("面试已取消。"), {});
            return;
        }

        const common::AppConfig config = common::loadConfigFromFile(config_path_.toStdString());
        emit SessionPrepared(QString::fromStdString(config.interview.candidate_name),
                             QString::fromStdString(config.interview.target_role),
                             config.interview.question_count,
                             QString::fromStdString(config.realtime.provider));
        emit StateChanged(QStringLiteral("正在准备题目"));

        // 所有外部服务都在 worker 线程创建。即使准备题目或解析 PDF 失败，局部 RAII 对象也会
        // 在同一线程析构，不会把第三方资源生命周期泄漏给 MainWindow。
        std::unique_ptr<services::ILlmClient> llm_client = services::createLlmClient(config.llm);
        services::PodofoPdfParser pdf_parser;
        session::PreparedInterview prepared_interview =
            session::prepareInterview(config.interview, *llm_client, pdf_parser);
        if (!prepared_interview.isReady()) {
            emit Finished(false, QString::fromStdString(prepared_interview.getErrorMessage()), {});
            return;
        }

        std::vector<common::RealtimeEvent> scripted_events;
        if (config.realtime.provider == "mock") {
            // Qt 默认演示复用确定性脚本，确保首次运行不需要网络、密钥、麦克风或扬声器。
            scripted_events = app::buildDefaultRealtimeDemoScript(
                prepared_interview.getManager().getQuestionCount());
        } else if (config.realtime.dialog.input_mod != "keep_alive") {
            throw std::runtime_error("Qt 完整语音面试要求 realtime.dialog.input_mod=keep_alive。");
        }

        std::unique_ptr<services::IRealtimeClient> realtime_client =
            services::createRealtimeClient(config.realtime, scripted_events);
        std::unique_ptr<services::PortAudioAudioDevice> audio_device;
        std::unique_ptr<session::RealtimeAudioBridge> audio_bridge;

        if (config.realtime.provider != "mock") {
            // 真实语音模式沿用 CLI 已验证的 PCM 参数；worker 独占 bridge 和 realtime client，
            // PortAudio callback 仍然只通过内部队列交换音频，不触碰 Qt 控件。
            audio_device = std::make_unique<services::PortAudioAudioDevice>();
            const services::AudioPcmFormat capture_format = {
                config.realtime.audio.capture_sample_rate_hz,
                config.realtime.audio.capture_channels,
                config.realtime.audio.frames_per_buffer,
            };
            const services::AudioPcmFormat playback_format = {
                config.realtime.tts.sample_rate_hz,
                config.realtime.tts.channels,
                config.realtime.audio.frames_per_buffer,
            };
            audio_bridge = std::make_unique<session::RealtimeAudioBridge>(
                *audio_device, *realtime_client, capture_format, playback_format);
        }

        session::DialogOrchestrator orchestrator(prepared_interview, *realtime_client,
                                                 audio_bridge.get(), this,
                                                 cancellation_token_.get());
        const session::DialogOrchestratorResult result = orchestrator.run();
        if (!result.success) {
            emit Finished(false, QString::fromStdString(result.error_message), {});
            return;
        }

        const std::string report_path = SaveReportIfConfigured(config.report, result.session);
        emit Finished(true, QStringLiteral("面试完成。"), QString::fromStdString(report_path));
    } catch (const std::exception& error) {
        // 对 UI 只暴露可展示摘要；异常消息来自既有配置/服务边界，不包含鉴权 header。
        emit Finished(false,
                      QStringLiteral("面试启动或运行失败：") + QString::fromUtf8(error.what()), {});
    }
}

void InterviewWorker::OnStateChanged(session::InterviewState state) {
    emit StateChanged(StateText(state));
}

void InterviewWorker::OnInterviewerText(const std::string& text) {
    emit InterviewerTextReceived(QString::fromStdString(text));
}

void InterviewWorker::OnCandidateTranscript(const std::string& text, bool is_final) {
    emit CandidateTranscriptReceived(QString::fromStdString(text), is_final);
}

} // namespace ui
} // namespace interview
