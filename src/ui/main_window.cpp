// clang-format off
#include "ui/main_window.h"

#include <memory>
#include <utility>

#include <QByteArray>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/interview_worker.h"
// clang-format on

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
    view_report_button_->setEnabled(false);
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
    if (provider_name == QStringLiteral("mock")) {
        // 明确区分预设回答与真实语音，避免演示界面让用户误以为麦克风或在线 AI 已启用。
        session_summary_label_->setText(session_summary_label_->text() +
                                        QStringLiteral("（自动预设回答，无需麦克风）"));
    }
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
    view_report_button_->setEnabled(success && !report_path_.isEmpty());
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

void MainWindow::ShowReport() {
    if (report_path_.isEmpty()) {
        return;
    }

    // 报告已由 worker 原子写入。这里只做有大小上限的本地读取，不联网、不重新评分，
    // 也不将候选人正文写入日志；限制大小可避免误选超大文件时长时间阻塞 UI。
    QFile report_file(report_path_);
    constexpr qint64 kMaximumReportBytes = 4 * 1024 * 1024;
    if (!report_file.open(QIODevice::ReadOnly) || report_file.size() > kMaximumReportBytes) {
        status_label_->setText(QStringLiteral("无法查看报告：文件不可读或超过 4 MiB。"));
        return;
    }
    const QByteArray bytes = report_file.read(kMaximumReportBytes + 1);
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
    const QJsonObject report = document.object();
    const QJsonArray records = report.value(QStringLiteral("question_answer_records")).toArray();
    if (bytes.size() > kMaximumReportBytes || parse_error.error != QJsonParseError::NoError ||
        !document.isObject() || records.isEmpty() ||
        report.value(QStringLiteral("question_count")).toDouble(-1) != records.size()) {
        // 损坏或不匹配的报告不能显示为“0 分成功”，保留原文件让用户检查。
        status_label_->setText(QStringLiteral("无法查看报告：JSON 或问答记录格式无效。"));
        return;
    }

    QString text =
        QStringLiteral("面试结果 / Interview results\n已完成 %1 道题\n\n").arg(records.size());
    for (int index = 0; index < records.size(); ++index) {
        const QJsonObject record = records.at(index).toObject();
        const QJsonObject score = record.value(QStringLiteral("final_score")).toObject();
        const double points = score.value(QStringLiteral("score")).toDouble(-1);
        if (!record.value(QStringLiteral("question")).isString() ||
            !record.value(QStringLiteral("candidate_answer")).isString() ||
            !score.value(QStringLiteral("feedback")).isString() || points < 0 || points > 100) {
            status_label_->setText(QStringLiteral("无法查看报告：题目或评分字段无效。"));
            return;
        }
        text += QStringLiteral("%1. %2\n回答：%3\n得分：%4 / 100\n反馈：%5\n")
                    .arg(index + 1)
                    .arg(record.value(QStringLiteral("question")).toString(),
                         record.value(QStringLiteral("candidate_answer")).toString())
                    .arg(points)
                    .arg(score.value(QStringLiteral("feedback")).toString());
        if (record.value(QStringLiteral("has_follow_up")).toBool()) {
            text += QStringLiteral("追问：%1\n补充回答：%2\n")
                        .arg(record.value(QStringLiteral("follow_up_prompt")).toString(),
                             record.value(QStringLiteral("follow_up_answer")).toString());
        }
        text += QLatin1Char('\n');
    }

    // 对话框由主窗口拥有，关闭后自动释放。纯文本控件不会执行回答里的 HTML 或打开链接。
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("面试结果 / Interview results"));
    dialog->resize(840, 600);
    auto* layout = new QVBoxLayout(dialog);
    auto* preview = new QPlainTextEdit(dialog);
    preview->setObjectName(QStringLiteral("reportPreview"));
    preview->setReadOnly(true);
    preview->setPlainText(text);
    layout->addWidget(preview);
    dialog->show();
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
    view_report_button_ = new QPushButton(QStringLiteral("查看报告"), report_group);
    view_report_button_->setObjectName(QStringLiteral("viewReportButton"));
    view_report_button_->setEnabled(false);
    open_report_button_ = new QPushButton(QStringLiteral("打开目录"), report_group);
    open_report_button_->setEnabled(false);
    report_layout->addWidget(report_path_edit_, 1);
    report_layout->addWidget(view_report_button_);
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
    connect(view_report_button_, &QPushButton::clicked, this, &MainWindow::ShowReport);
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
