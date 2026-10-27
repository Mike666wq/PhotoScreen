#include "platform/macimage.h"
#include <QBuffer>
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
