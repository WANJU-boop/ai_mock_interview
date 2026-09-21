// clang-format off
#include <string>

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

#include "common/config.h"
#include "common/logger.h"
#include "ui/main_window.h"
// clang-format on

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("AI Mock Interview"));
    application.setOrganizationName(QStringLiteral("Interview Learning Project"));

    // 双击 .app 时工作目录可能是只读目录；使用系统推荐的应用数据目录保存日志。
    // 若目录创建失败，Logger 仍降级到控制台输出，不影响启动；网络服务继续由 worker 拥有。
    const QString log_directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(log_directory);
    interview::common::Logger::Init(
        QDir(log_directory).filePath(QStringLiteral("interview.log")).toStdString(), false);
    const std::string config_path =
        argc > 1 ? argv[1] : interview::common::findDefaultConfigPath(argv[0]);

    interview::ui::MainWindow main_window(QString::fromStdString(config_path));
    main_window.show();
    return application.exec();
}
