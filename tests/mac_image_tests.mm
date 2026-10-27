#include "platform/macimage.h"
#include <QBuffer>
#include <QDir>
#include <QPainter>
#include <QtTest>

class MacImageTests : public QObject {
    Q_OBJECT
private slots:
    void orientationAndColor() {
        QImage original(11,7,QImage::Format_ARGB32_Premultiplied);
        original.fill(Qt::white);
        original.setPixelColor(0,0,Qt::red);
        original.setPixelColor(10,0,Qt::green);
        original.setPixelColor(0,6,Qt::blue);
        original.setPixelColor(10,6,Qt::yellow);
        original.setPixelColor(3,1,QColor(20,40,200));
        CGImageRef source=original.toCGImage();
        QVERIFY(source);
        const QImage converted=photo::imageFromCGImage(source);
        CGImageRelease(source);
        QCOMPARE(converted.size(),original.size());
        const QString outputDir=qEnvironmentVariable("PHOTOSCREEN_REPORT_DIR");
        if (!outputDir.isEmpty()) {
            QDir().mkpath(outputDir);
            QImage comparison(450,150,QImage::Format_RGB32); comparison.fill(Qt::black);
            QPainter painter(&comparison);
            painter.setRenderHint(QPainter::SmoothPixmapTransform,false);
            painter.drawImage(QRect(0,5,220,140),original);
            painter.drawImage(QRect(230,5,220,140),converted);
            painter.end();
            QVERIFY(comparison.save(outputDir+QStringLiteral("/mac-orientation-expected-actual.png")));
        }
        for (QPoint p : {QPoint(0,0),QPoint(10,0),QPoint(0,6),QPoint(10,6),QPoint(3,1)})
            QCOMPARE(converted.pixelColor(p),original.pixelColor(p));
        QByteArray png; QBuffer output(&png); QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(converted.save(&output,"PNG"));
        const QImage decoded=QImage::fromData(png,"PNG");
        QCOMPARE(decoded.pixelColor(0,0),QColor(Qt::red));
        QCOMPARE(decoded.pixelColor(0,6),QColor(Qt::blue));
    }
};
QTEST_APPLESS_MAIN(MacImageTests)
#include "mac_image_tests.moc"
