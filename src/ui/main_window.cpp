#include "ui/main_window.h"

#include "ui/interview_worker.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>
#include <utility>

namespace interview {
namespace ui {

MainWindow::MainWindow(QString default_config_path, QWidget* parent) : QMainWindow(parent) {
    BuildUi();
    config_path_edit_->setText(std::move(default_config_path));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (worker_thread_ != nullptr && worker_thread_->isRunning()) {
        // 不能在主线程直接 terminate worker：它可能正拥有 WebSocket、PortAudio 或 HTTP future。
        // 请求取消后保持窗口事件循环可响应，等 worker 发出 finished 再自动关闭。
        close_requested_ = true;
        StopInterview();
        status_label_->setText(QStringLiteral("正在安全停止，完成后窗口会自动关闭…"));
        event->ignore();
        return;
    }

    event->accept();
}

void MainWindow::SelectConfigFile() {
    const QString selected_path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择面试配置"), config_path_edit_->text(),
        QStringLiteral("JSON 配置 (*.json);;所有文件 (*)"));
    if (!selected_path.isEmpty()) {
        config_path_edit_->setText(selected_path);
    }
}

void MainWindow::StartInterview() {
    if (worker_thread_ != nullptr && worker_thread_->isRunning()) {
        return;
    }

    const QString config_path = config_path_edit_->text().trimmed();
    if (config_path.isEmpty()) {
        status_label_->setText(QStringLiteral("请先选择配置文件。"));
        return;
    }

    conversation_edit_->clear();
    partial_transcript_label_->setText(QStringLiteral("等待候选人回答…"));
    session_summary_label_->setText(QStringLiteral("正在读取配置…"));
    report_path_.clear();
    report_path_edit_->clear();
    open_report_button_->setEnabled(false);
    SetRunning(true);
    status_label_->setText(QStringLiteral("正在启动 worker…"));

    cancellation_token_ = std::make_shared<session::DialogCancellationToken>();
    QThread* thread = new QThread(this);
    InterviewWorker* worker = new InterviewWorker(config_path, cancellation_token_);
    worker->moveToThread(thread);
    worker_thread_ = thread;

    // started -> Run 让完整同步面试在 worker 线程执行；worker -> MainWindow 的 AutoConnection
    // 因线程归属不同会成为 queued connection，所有控件更新仍发生在 Qt 主线程。
    connect(thread, &QThread::started, worker, &InterviewWorker::Run);
    connect(worker, &InterviewWorker::SessionPrepared, this, &MainWindow::HandleSessionPrepared);
    connect(worker, &InterviewWorker::StateChanged, this, &MainWindow::HandleStateChanged);
    connect(worker, &InterviewWorker::InterviewerTextReceived, this,
            &MainWindow::HandleInterviewerText);
    connect(worker, &InterviewWorker::CandidateTranscriptReceived, this,
            &MainWindow::HandleCandidateTranscript);
    connect(worker, &InterviewWorker::Finished, this, &MainWindow::HandleFinished);
    connect(worker, &InterviewWorker::Finished, thread, &QThread::quit);
    connect(worker, &InterviewWorker::Finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, &MainWindow::HandleWorkerThreadFinished);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

void MainWindow::StopInterview() {
    if (cancellation_token_ == nullptr) {
        return;
    }

    // RequestStop 只修改原子值，不调用 worker QObject 槽，也不跨线程销毁资源。
    cancellation_token_->RequestStop();
    stop_button_->setEnabled(false);
    status_label_->setText(QStringLiteral("正在停止面试…"));
}

void MainWindow::HandleSessionPrepared(const QString& candidate_name, const QString& target_role,
                                       int question_count, const QString& provider_name) {
    session_summary_label_->setText(QStringLiteral("候选人：%1　岗位：%2　题目：%3　Realtime：%4")
                                        .arg(candidate_name, target_role)
                                        .arg(question_count)
                                        .arg(provider_name));
}

void MainWindow::HandleStateChanged(const QString& state_text) {
    status_label_->setText(state_text);
}

void MainWindow::HandleInterviewerText(const QString& text) {
    AppendConversation(QStringLiteral("面试官"), text);
}

void MainWindow::HandleCandidateTranscript(const QString& text, bool is_final) {
    if (is_final) {
        AppendConversation(QStringLiteral("候选人"), text);
        partial_transcript_label_->setText(QStringLiteral("等待下一次回答…"));
        return;
    }

    // partial transcript 会被后续识别结果覆盖，不追加到正式对话，避免界面出现大量重复文本。
    partial_transcript_label_->setText(QStringLiteral("实时识别：%1").arg(text));
}

void MainWindow::HandleFinished(bool success, const QString& message, const QString& report_path) {
    if (success) {
        status_label_->setText(QStringLiteral("面试已完成"));
    } else if (message == QStringLiteral("面试已取消。")) {
        status_label_->setText(QStringLiteral("面试已取消"));
    } else {
        status_label_->setText(QStringLiteral("面试运行失败"));
    }
    AppendConversation(QStringLiteral("系统"), message);

    report_path_ = report_path;
    report_path_edit_->setText(report_path_);
    open_report_button_->setEnabled(!report_path_.isEmpty());
    stop_button_->setEnabled(false);
}

void MainWindow::HandleWorkerThreadFinished() {
    worker_thread_.clear();
    cancellation_token_.reset();
    SetRunning(false);

    if (close_requested_) {
        // 等当前 queued signal 全部处理完再重新触发 closeEvent，此时不再存在运行中的线程。
        QTimer::singleShot(0, this, &QWidget::close);
    }
}

void MainWindow::OpenReportDirectory() {
    if (report_path_.isEmpty()) {
        return;
    }

    const QString directory = QFileInfo(report_path_).absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        status_label_->setText(QStringLiteral("无法打开报告目录：%1").arg(directory));
    }
}

void MainWindow::BuildUi() {
    setWindowTitle(QStringLiteral("C++ AI 模拟面试"));
    resize(920, 680);

    QWidget* central_widget = new QWidget(this);
    QVBoxLayout* root_layout = new QVBoxLayout(central_widget);

    QLabel* title_label = new QLabel(QStringLiteral("C++ AI 模拟面试"), central_widget);
    QFont title_font = title_label->font();
    title_font.setPointSize(title_font.pointSize() + 6);
    title_font.setBold(true);
    title_label->setFont(title_font);
    root_layout->addWidget(title_label);

    QGroupBox* config_group = new QGroupBox(QStringLiteral("运行配置"), central_widget);
    QHBoxLayout* config_layout = new QHBoxLayout(config_group);
    config_path_edit_ = new QLineEdit(config_group);
    config_path_edit_->setObjectName(QStringLiteral("configPathEdit"));
    config_path_edit_->setPlaceholderText(
        QStringLiteral("config.example.json 或 config.local.json"));
    browse_button_ = new QPushButton(QStringLiteral("选择…"), config_group);
    browse_button_->setObjectName(QStringLiteral("browseButton"));
    config_layout->addWidget(config_path_edit_, 1);
    config_layout->addWidget(browse_button_);
    root_layout->addWidget(config_group);

    session_summary_label_ = new QLabel(QStringLiteral("尚未加载面试配置。"), central_widget);
    session_summary_label_->setWordWrap(true);
    root_layout->addWidget(session_summary_label_);

    status_label_ = new QLabel(QStringLiteral("准备就绪"), central_widget);
    status_label_->setObjectName(QStringLiteral("statusLabel"));
    QFont status_font = status_label_->font();
    status_font.setBold(true);
    status_label_->setFont(status_font);
    root_layout->addWidget(status_label_);

    QGroupBox* conversation_group = new QGroupBox(QStringLiteral("面试对话"), central_widget);
    QVBoxLayout* conversation_layout = new QVBoxLayout(conversation_group);
    conversation_edit_ = new QPlainTextEdit(conversation_group);
    conversation_edit_->setObjectName(QStringLiteral("conversationEdit"));
    conversation_edit_->setReadOnly(true);
    conversation_edit_->setPlaceholderText(
        QStringLiteral("点击“开始面试”后，这里会显示面试官问题和候选人最终回答。"));
    partial_transcript_label_ = new QLabel(QStringLiteral("等待候选人回答…"), conversation_group);
    partial_transcript_label_->setObjectName(QStringLiteral("partialTranscriptLabel"));
    partial_transcript_label_->setWordWrap(true);
    conversation_layout->addWidget(conversation_edit_, 1);
    conversation_layout->addWidget(partial_transcript_label_);
    root_layout->addWidget(conversation_group, 1);

    QGroupBox* report_group = new QGroupBox(QStringLiteral("面试报告"), central_widget);
    QHBoxLayout* report_layout = new QHBoxLayout(report_group);
    report_path_edit_ = new QLineEdit(report_group);
    report_path_edit_->setReadOnly(true);
    report_path_edit_->setPlaceholderText(QStringLiteral("完成后显示本地 JSON 报告路径"));
    open_report_button_ = new QPushButton(QStringLiteral("打开目录"), report_group);
    open_report_button_->setEnabled(false);
    report_layout->addWidget(report_path_edit_, 1);
    report_layout->addWidget(open_report_button_);
    root_layout->addWidget(report_group);

    QHBoxLayout* action_layout = new QHBoxLayout();
    action_layout->addStretch();
    stop_button_ = new QPushButton(QStringLiteral("停止面试"), central_widget);
    stop_button_->setObjectName(QStringLiteral("stopButton"));
    stop_button_->setEnabled(false);
    start_button_ = new QPushButton(QStringLiteral("开始面试"), central_widget);
    start_button_->setObjectName(QStringLiteral("startButton"));
    start_button_->setDefault(true);
    action_layout->addWidget(stop_button_);
    action_layout->addWidget(start_button_);
    root_layout->addLayout(action_layout);

    setCentralWidget(central_widget);

    connect(browse_button_, &QPushButton::clicked, this, &MainWindow::SelectConfigFile);
    connect(start_button_, &QPushButton::clicked, this, &MainWindow::StartInterview);
    connect(stop_button_, &QPushButton::clicked, this, &MainWindow::StopInterview);
    connect(open_report_button_, &QPushButton::clicked, this, &MainWindow::OpenReportDirectory);
}

void MainWindow::SetRunning(bool running) {
    config_path_edit_->setEnabled(!running);
    browse_button_->setEnabled(!running);
    start_button_->setEnabled(!running);
    stop_button_->setEnabled(running);
}

void MainWindow::AppendConversation(const QString& speaker, const QString& text) {
    conversation_edit_->appendPlainText(QStringLiteral("%1：%2\n").arg(speaker, text));
}

} // namespace ui
} // namespace interview
