#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>

inline bool loadReportFont() {
    const QString path=qEnvironmentVariable("PHOTOSCREEN_TEST_FONT");
    if (path.isEmpty()) return true;
    const int id=QFontDatabase::addApplicationFont(path);
    const QStringList families=QFontDatabase::applicationFontFamilies(id);
    if (id<0 || families.isEmpty()) return false;
    QFont font(families.first()); font.setPixelSize(13);
    QGuiApplication::setFont(font);
    return QFontMetrics(font).inFontUcs4(0x622a) && QFontMetrics(font).inFontUcs4(0x56fe);
}
