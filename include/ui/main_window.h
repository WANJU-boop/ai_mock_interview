#pragma once

#include "session/dialog_cancellation.h"

#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <memory>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QThread;

namespace interview {
namespace ui {

// MainWindow 只负责收集配置路径、展示面试进度和管理 worker 生命周期。
// 网络、音频、PDF 和 LLM 均留在 InterviewWorker，保持 UI -> session -> services 的依赖方向。
class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QString default_config_path, QWidget* parent = nullptr);

  protected:
    // 面试运行时先请求安全取消并等待 worker 自己退出，防止析构仍在运行的 QThread。
    void closeEvent(QCloseEvent* event) override;

  private slots:
    void SelectConfigFile();
    void StartInterview();
    void StopInterview();
    void HandleSessionPrepared(const QString& candidate_name, const QString& target_role,
                               int question_count, const QString& provider_name);
    void HandleStateChanged(const QString& state_text);
    void HandleInterviewerText(const QString& text);
    void HandleCandidateTranscript(const QString& text, bool is_final);
    void HandleFinished(bool success, const QString& message, const QString& report_path);
    void HandleWorkerThreadFinished();
    void OpenReportDirectory();

  private:
    void BuildUi();
    void SetRunning(bool running);
    void AppendConversation(const QString& speaker, const QString& text);

    QLineEdit* config_path_edit_ = nullptr;
    QPushButton* browse_button_ = nullptr;
    QLabel* session_summary_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QPlainTextEdit* conversation_edit_ = nullptr;
    QLabel* partial_transcript_label_ = nullptr;
    QLineEdit* report_path_edit_ = nullptr;
    QPushButton* open_report_button_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;

    QPointer<QThread> worker_thread_;
    std::shared_ptr<session::DialogCancellationToken> cancellation_token_;
    QString report_path_;
    bool close_requested_ = false;
};

} // namespace ui
} // namespace interview
