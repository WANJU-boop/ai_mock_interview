#pragma once

#include "session/dialog_cancellation.h"
#include "session/dialog_observer.h"

#include <QObject>
#include <QString>
#include <memory>
#include <string>

namespace interview {
namespace ui {

// InterviewWorker 是 Qt UI 与现有同步 C++ 主流程之间的线程边界。
// 它在所属 QThread 中创建并销毁 LLM、PDF、WebSocket 和音频资源；窗口只接收信号，
// 因而不会从主线程直接读写 socket、PortAudio 或候选人报告。
class InterviewWorker final : public QObject, public session::IDialogObserver {
    Q_OBJECT

  public:
    // config_path 和 cancellation_token 按值保存，保证 worker 启动后不引用窗口控件。
    InterviewWorker(QString config_path,
                    std::shared_ptr<session::DialogCancellationToken> cancellation_token,
                    QObject* parent = nullptr);

  public slots:
    // 在 worker 线程同步运行完整面试；mock 与真实 keep_alive 语音模式共用同一条业务链路。
    void Run();

  signals:
    // 配置加载成功后发出不含密钥的会话摘要，供窗口展示当前候选人、岗位和 provider。
    void SessionPrepared(const QString& candidate_name, const QString& target_role,
                         int question_count, const QString& provider_name);
    // 状态和对话信号由 Qt 自动排队回主线程，窗口槽函数不得在 worker 线程直接调用。
    void StateChanged(const QString& state_text);
    void InterviewerTextReceived(const QString& text);
    void CandidateTranscriptReceived(const QString& text, bool is_final);
    // report_path 只包含本地路径；报告正文不会经过 signal 或日志。
    void Finished(bool success, const QString& message, const QString& report_path);

  private:
    void OnStateChanged(session::InterviewState state) override;
    void OnInterviewerText(const std::string& text) override;
    void OnCandidateTranscript(const std::string& text, bool is_final) override;

    QString config_path_;
    std::shared_ptr<session::DialogCancellationToken> cancellation_token_;
};

} // namespace ui
} // namespace interview
