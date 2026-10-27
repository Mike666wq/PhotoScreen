#include "core/annotation.h"
#include "core/geometry.h"
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QtTest>
#include <QtMath>

using namespace photo;
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void scaling_data() {
        QTest::addColumn<QSizeF>("logical"); QTest::addColumn<QSize>("pixels");
        QTest::addColumn<QRectF>("selection"); QTest::addColumn<QRect>("expected");
        QTest::newRow("retina") << QSizeF(800,600) << QSize(1600,1200) << QRectF(10,20,100,80) << QRect(20,40,200,160);
        QTest::newRow("125%") << QSizeF(800,600) << QSize(1000,750) << QRectF(10,20,100,80) << QRect(12,25,126,100);
        QTest::newRow("150%") << QSizeF(800,600) << QSize(1200,900) << QRectF(10,20,100,80) << QRect(15,30,150,120);
        QTest::newRow("200%") << QSizeF(800,600) << QSize(1600,1200) << QRectF(10,20,100,80) << QRect(20,40,200,160);
        QTest::newRow("clipped") << QSizeF(800,600) << QSize(1600,1200) << QRectF(-10,-10,20,20) << QRect(0,0,20,20);
    }
    void scaling() {
        QFETCH(QSizeF,logical); QFETCH(QSize,pixels); QFETCH(QRectF,selection); QFETCH(QRect,expected);
        QCOMPARE(pixelRect(selection,logical,pixels),expected);
    }
    void mixedScaleNegativeMonitor() {
        const QRect screen(-1920,0,1920,1080), logical(-1536,0,1536,864);
        QCOMPARE(nativeWindowToLogical(QRect(-1900,50,400,250),screen,logical),QRect(-1520,40,320,200));
        QCOMPARE(nativeWindowToLogical(QRect(-50,0,200,100),screen,logical),QRect(-40,0,40,80));
    }
    void cropAndDraw() {
        QImage image(200,200,QImage::Format_ARGB32); image.fill(Qt::white);
        Annotation a; a.tool=Tool::Rectangle; a.rect=QRectF(20,20,20,20); a.color=Qt::red; a.width=2;
        const auto out=renderSelection(image,QSizeF(100,100),QRectF(10,10,60,60),{a});
        QCOMPARE(out.size(),QSize(120,120));
        QVERIFY(out.pixelColor(20,20).red()>200); QVERIFY(out.pixelColor(20,20).green()<100);
        QCOMPARE(out.pixelColor(100,100),QColor(Qt::white));
    }
    void mosaicChangesPixels() {
        QImage image(100,100,QImage::Format_ARGB32);
        for (int y=0;y<100;++y) for(int x=0;x<100;++x) image.setPixelColor(x,y,((x+y)%2)?Qt::black:Qt::white);
        Annotation a; a.tool=Tool::Mosaic; a.rect=QRectF(20,20,40,40);
        const auto out=renderSelection(image,QSizeF(100,100),QRectF(0,0,100,100),{a});
        QVERIFY(out!=image); QCOMPARE(out.pixelColor(0,0),image.pixelColor(0,0));
    }
    void textStyleIsAppliedToDocument() {
        Annotation text; text.tool=Tool::Text; text.rect=QRectF(5,5,220,70);
        text.text=QStringLiteral("Weight"); text.fontSize=48; text.fontWeight=900; text.color=QColor("#ff3b3b");
        QTextDocument document;
        configureTextDocument(document,text);
        QCOMPARE(document.defaultFont().pixelSize(),48);
        QCOMPARE(int(document.defaultFont().weight()),900);
        bool found=false;
        for (QTextBlock block=document.begin(); block.isValid(); block=block.next()) {
            for (auto it=block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment=it.fragment();
                if (!fragment.isValid()) continue;
                QCOMPARE(int(fragment.charFormat().fontWeight()),900);
                QCOMPARE(fragment.charFormat().font().pixelSize(),48);
                QCOMPARE(fragment.charFormat().foreground().color(),QColor("#ff3b3b"));
                found=true;
            }
        }
        QVERIFY(found);
    }
    void undoRedo() {
        AnnotationDocument doc; Annotation a; a.rect=QRectF(10,10,20,20); doc.add(a);
        QCOMPARE(doc.items().size(),1); a.translate(QPointF(5,5)); doc.replace(0,a);
        QCOMPARE(doc.items()[0].rect.topLeft(),QPointF(15,15));
        doc.undoStack()->undo(); QCOMPARE(doc.items()[0].rect.topLeft(),QPointF(10,10));
        doc.undoStack()->undo(); QCOMPARE(doc.items().size(),0);
        doc.undoStack()->redo(); QCOMPARE(doc.items().size(),1);
    }
    void arrowLengthAndThickness() {
        Annotation arrow; arrow.tool=Tool::Arrow; arrow.color=QColor("#ff3b3b"); arrow.width=3;
        arrow.rect=QRectF(QPointF(20,100),QPointF(120,100));
        const QRectF shortBounds=arrowPath(arrow).boundingRect();
        arrow.rect=QRectF(QPointF(20,100),QPointF(320,100));
        const QRectF longBounds=arrowPath(arrow).boundingRect();
        QVERIFY(longBounds.height()>shortBounds.height()*2.9);
        arrow.width=12;
        QVERIFY(arrowPath(arrow).boundingRect().height()>longBounds.height()*3.5);
        QVERIFY(arrowPath(arrow).contains(QPointF(200,100)));
        QVERIFY(!arrowPath(arrow).contains(QPointF(200,180)));
    }
    void arrowDirectionAndHitTesting() {
        const QPointF center(200,200);
        for (int i=0;i<8;++i) {
            const qreal angle=i*3.141592653589793/4.0;
            const QPointF direction(qCos(angle),qSin(angle));
            Annotation arrow; arrow.tool=Tool::Arrow; arrow.width=3;
            arrow.rect=QRectF(center,center+direction*120);
            const auto path=arrowPath(arrow);
            QVERIFY(!path.isEmpty());
            QVERIFY(annotationContains(arrow,center+direction*70,2));
            QVERIFY(!annotationContains(arrow,center+direction*70+QPointF(-direction.y(),direction.x())*30,2));
        }
        Annotation diagonal; diagonal.tool=Tool::Arrow; diagonal.rect=QRectF(QPointF(40,40),QPointF(240,240));
        QVERIFY(!annotationContains(diagonal,QPointF(220,60)));
    }
    void magnifierAtEdges() {
        QImage source(20,20,QImage::Format_RGB32); source.fill(Qt::white);
        source.setPixelColor(0,0,Qt::red); source.setPixelColor(19,19,Qt::blue);
        auto top=magnifierSample(source,QPoint(0,0)); QCOMPARE(top.size(),QSize(17,17));
        QCOMPARE(top.pixelColor(8,8),QColor(Qt::red)); QCOMPARE(top.pixelColor(0,0),QColor(Qt::black));
        auto bottom=magnifierSample(source,QPoint(19,19));
        QCOMPARE(bottom.pixelColor(8,8),QColor(Qt::blue)); QCOMPARE(bottom.pixelColor(16,16),QColor(Qt::black));
    }
};
QTEST_MAIN(CoreTests)
#include "core_tests.moc"
