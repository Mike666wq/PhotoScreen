#include "app.h"
#include "platform/platform.h"
#include <QApplication>

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PhotoScreen"));
    QCoreApplication::setApplicationName(QStringLiteral("PhotoScreen"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PHOTOSCREEN_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    photo::appendDiagnosticLog(QStringLiteral("PhotoScreen 启动，版本 %1").arg(QStringLiteral(PHOTOSCREEN_VERSION)));
    if (!photo::acquireSingleInstance()) {
        // 已知未修项：单实例获取失败时静默退出。这里至少落一条日志便于排查「托盘点了没反应」。
        photo::appendDiagnosticLog(QStringLiteral("单实例获取失败：已有实例在运行，本次启动直接退出"));
        return 0;
    }
    photo::App controller;
    if (!controller.initialize()) return 1;
    return app.exec();
}
