#include "app.h"
#include "platform/platform.h"
#include <QApplication>

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PhotoScreen"));
    QCoreApplication::setApplicationName(QStringLiteral("PhotoScreen"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PHOTOSCREEN_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    if (!photo::acquireSingleInstance()) return 0;
    photo::App controller;
    if (!controller.initialize()) return 1;
    return app.exec();
}
