#include "common/config.h"
#include "common/logger.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QString>
#include <string>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("AI Mock Interview"));
    application.setOrganizationName(QStringLiteral("Interview Learning Project"));

    // Qt 入口只决定默认配置路径和启动窗口；所有阻塞服务都由窗口创建的 worker 线程拥有。
    interview::common::Logger::Init("interview.log", false);
    const std::string config_path =
        argc > 1 ? argv[1] : interview::common::findDefaultConfigPath(argv[0]);

    interview::ui::MainWindow main_window(QString::fromStdString(config_path));
    main_window.show();
    return application.exec();
}
