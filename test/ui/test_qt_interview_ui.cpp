// clang-format off
#include <memory>

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include "session/dialog_cancellation.h"
#include "ui/interview_worker.h"
#include "ui/main_window.h"
// clang-format on

namespace {

QString WriteMockConfig(const QTemporaryDir& temporary_directory, bool save_report = false) {
    const QString config_path = temporary_directory.filePath(QStringLiteral("config.json"));
    QFile config_file(config_path);
    if (!config_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }

    // 测试配置显式关闭报告写入，验证 Qt worker 时不会在仓库留下候选人数据或生成文件。
    QByteArray config_json = R"({
  "interview": {
    "candidate_name": "Qt 测试候选人",
    "target_role": "C++ 实习生",
    "resume_path": "",
    "question_count": 1
  },
  "llm": {
    "provider": "mock",
    "model": "mock-interviewer"
  },
  "report": {
    "save_json": false,
    "output_directory": "reports"
  },
  "realtime": {
    "provider": "mock"
  }
})";
    if (save_report) {
        // 只有报告闭环测试写文件，而且写在临时目录；不污染仓库或依赖真实服务。
        QJsonObject config = QJsonDocument::fromJson(config_json).object();
        config[QStringLiteral("report")] =
            QJsonObject{{QStringLiteral("save_json"), true},
                        {QStringLiteral("output_directory"), temporary_directory.path()}};
        config_json = QJsonDocument(config).toJson();
    }
    if (config_file.write(config_json) != config_json.size()) {
        return {};
    }
    config_file.close();
    return config_path;
}

class QtInterviewUiTest final : public QObject {
    Q_OBJECT

  private slots:
    void ConstructsMainWindow();
    void RunsMockInterviewInWorkerThread();
    void HonorsCancellationBeforeWorkerStarts();
    void CompletesMockAndDisplaysSavedReport();
    void RejectsInvalidReport();
};

// 验证最小窗口包含启动、停止和状态控件；这能尽早发现 AUTOMOC 或 Widgets 链接缺失。
void QtInterviewUiTest::ConstructsMainWindow() {
    interview::ui::MainWindow window(QStringLiteral("config.example.json"));

    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("startButton")) != nullptr);
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("stopButton")) != nullptr);
    QLabel* status_label = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    QVERIFY(status_label != nullptr);
    QCOMPARE(status_label->text(), QStringLiteral("准备就绪"));
}

// 验证 mock 配置从独立 QThread 走完整编排闭环，并通过 queued signal 返回成功结果。
// 该测试不联网、不访问音频设备，也不依赖真实 PDF 环境。
void QtInterviewUiTest::RunsMockInterviewInWorkerThread() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString config_path = WriteMockConfig(temporary_directory);
    QVERIFY(!config_path.isEmpty());

    auto cancellation_token = std::make_shared<interview::session::DialogCancellationToken>();
    QThread worker_thread;
    auto* worker = new interview::ui::InterviewWorker(config_path, cancellation_token);
    worker->moveToThread(&worker_thread);
    QSignalSpy finished_spy(worker, &interview::ui::InterviewWorker::Finished);
    QSignalSpy interviewer_spy(worker, &interview::ui::InterviewWorker::InterviewerTextReceived);

    connect(&worker_thread, &QThread::started, worker, &interview::ui::InterviewWorker::Run);
    connect(worker, &interview::ui::InterviewWorker::Finished, &worker_thread, &QThread::quit);
    connect(worker, &interview::ui::InterviewWorker::Finished, worker, &QObject::deleteLater);
    worker_thread.start();

    QTRY_COMPARE_WITH_TIMEOUT(finished_spy.count(), 1, 5000);
    worker_thread.quit();
    QVERIFY(worker_thread.wait(5000));
    QCOMPARE(finished_spy.count(), 1);
    const QList<QVariant> arguments = finished_spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("面试完成。"));
    QVERIFY(arguments.at(2).toString().isEmpty());
    QVERIFY(interviewer_spy.count() >= 2);
}

// 验证 UI 在 worker 启动前发出的取消请求会安全进入统一失败结果，不会访问网络或音频。
void QtInterviewUiTest::HonorsCancellationBeforeWorkerStarts() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString config_path = WriteMockConfig(temporary_directory);
    QVERIFY(!config_path.isEmpty());

    auto cancellation_token = std::make_shared<interview::session::DialogCancellationToken>();
    cancellation_token->RequestStop();
    QThread worker_thread;
    auto* worker = new interview::ui::InterviewWorker(config_path, cancellation_token);
    worker->moveToThread(&worker_thread);
    QSignalSpy finished_spy(worker, &interview::ui::InterviewWorker::Finished);

    connect(&worker_thread, &QThread::started, worker, &interview::ui::InterviewWorker::Run);
    connect(worker, &interview::ui::InterviewWorker::Finished, &worker_thread, &QThread::quit);
    connect(worker, &interview::ui::InterviewWorker::Finished, worker, &QObject::deleteLater);
    worker_thread.start();

    QTRY_COMPARE_WITH_TIMEOUT(finished_spy.count(), 1, 5000);
    worker_thread.quit();
    QVERIFY(worker_thread.wait(5000));
    const QList<QVariant> arguments = finished_spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("面试已取消。"));
}

// 验证从“开始”按钮经过 worker、评分、文件保存，最终可直接查看报告；防止只测试孤立控件。
void QtInterviewUiTest::CompletesMockAndDisplaysSavedReport() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString config_path = WriteMockConfig(directory, true);
    QVERIFY(!config_path.isEmpty());
    interview::ui::MainWindow window(config_path);
    auto* start = window.findChild<QPushButton*>(QStringLiteral("startButton"));
    auto* view = window.findChild<QPushButton*>(QStringLiteral("viewReportButton"));
    QVERIFY(start != nullptr);
    QVERIFY(view != nullptr);
    QVERIFY(!view->isEnabled());
    start->click();
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 5000);
    QVERIFY(view->isEnabled());
    view->click();
    auto* preview = window.findChild<QPlainTextEdit*>(QStringLiteral("reportPreview"));
    QVERIFY(preview != nullptr);
    QVERIFY(preview->isReadOnly());
    QVERIFY(preview->toPlainText().contains(QStringLiteral("已完成 1 道题")));
    QVERIFY(preview->toPlainText().contains(QStringLiteral(" / 100")));
    QVERIFY(preview->toPlainText().contains(QStringLiteral("反馈：")));
}

// 验证磁盘文件损坏时只显示可理解的错误；不把无效 JSON 误显示为正常面试结果。
void QtInterviewUiTest::RejectsInvalidReport() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{broken");
    file.close();
    interview::ui::MainWindow window(QStringLiteral("unused.json"));
    // 直接模拟“已保存”事件来检查文件读取边界，不创建网络连接或另一个后台线程。
    QVERIFY(QMetaObject::invokeMethod(&window, "HandleFinished", Q_ARG(bool, true),
                                      Q_ARG(QString, QStringLiteral("面试完成。")),
                                      Q_ARG(QString, path)));
    auto* view = window.findChild<QPushButton*>(QStringLiteral("viewReportButton"));
    QVERIFY(view != nullptr);
    view->click();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    QVERIFY(status != nullptr);
    QVERIFY(status->text().contains(QStringLiteral("格式无效")));
    QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("reportPreview")) == nullptr);
}

} // namespace

QTEST_MAIN(QtInterviewUiTest)

#include "test_qt_interview_ui.moc"
