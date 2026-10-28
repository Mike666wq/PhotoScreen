#include "core/annotation.h"
#include "core/geometry.h"
#include "core/interaction.h"
#include "report_font.h"
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
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("PhotoScreenTests"));
        QCoreApplication::setApplicationName(QStringLiteral("CoreTests"));
        QVERIFY2(loadReportFont(),"固定测试字体加载失败或缺少中文字形");
    }
    void uiPlacementAvoidsScreenGaps() {
        const QVector<QRect> screens={QRect(-600,0,600,480),QRect(0,200,600,480)};
        QCOMPARE(interactionScreen(screens,QPointF(100,250),QRectF(-40,210,200,100)),screens[1]);
        QCOMPARE(interactionScreen(screens,QPointF(100,100),QRectF(-400,20,200,100)),screens[0]);
        for (const QRect &screen : screens) {
            for (const QPoint &corner : {screen.topLeft(),screen.topRight(),screen.bottomLeft(),screen.bottomRight()}) {
                const QRectF selection(corner,QSizeF(20,20));
                const QRect toolbar=toolbarPlacement(QSize(460,120),selection,screen);
                QVERIFY(screen.contains(toolbar));
                const QRect colors=popupPlacement(QSize(280,280),toolbar,toolbar.center(),screen,false);
                QVERIFY(screen.contains(colors)); QVERIFY(!colors.intersects(toolbar));
                QVERIFY(screen.contains(fitInsideScreen(QRect(corner,QSize(146,144)),screen)));
            }
        }
    }
    void displayedOutputMetricsMatchRenderer() {
        ScreenImage left; left.geometry=QRect(-100,0,100,100); left.image=QImage(150,150,QImage::Format_RGB32); left.image.fill(Qt::white);
        ScreenImage right; right.geometry=QRect(0,0,100,100); right.image=QImage(200,200,QImage::Format_RGB32); right.image.fill(Qt::white);
        const QVector<ScreenImage> screens={left,right};
        for (const QRectF &selection : {QRectF(-90.5,10.5,20,30),QRectF(-10.5,10.5,30,20),QRectF(20.5,10.5,40,30)}) {
            const SelectionMetrics metrics=selectionMetrics(screens,selection);
            QCOMPARE(metrics.pixelSize,renderSelection(screens,selection,{}).size());
        }
    }
    void constrainedGeometryAndObjectHandles() {
        const QRectF bounds(-100,-100,400,400);
        const QPointF start(20,30);
        for (const QPointF &point : {QPointF(100,50),QPointF(-80,10),QPointF(40,-70),QPointF(500,500)}) {
            const QPointF end=constrainedSquareEnd(start,point,bounds);
            QVERIFY(qAbs(qAbs(end.x()-start.x())-qAbs(end.y()-start.y()))<0.001);
            QVERIFY(bounds.contains(end));
            const QPointF arrow=constrainedArrowEnd(start,point,bounds);
            const QPointF delta=arrow-start;
            QVERIFY(qAbs(delta.x())<0.001 || qAbs(delta.y())<0.001 || qAbs(qAbs(delta.x())-qAbs(delta.y()))<0.001);
            QVERIFY(bounds.adjusted(-0.001,-0.001,0.001,0.001).contains(arrow));
        }
        Annotation rect; rect.rect=QRectF(10,20,100,60);
        QCOMPARE(annotationHandles(rect).size(),8);
        QCOMPARE(hitHandle(annotationHandles(rect),QPointF(10,50)),7);
        rect.tool=Tool::Text; QCOMPARE(annotationHandles(rect).size(),2);
        rect.tool=Tool::Arrow; QCOMPARE(annotationHandles(rect).first(),rect.rect.topLeft());
        const QRectF resized=resizeFromHandle(QRectF(10,20,100,60),7,QPointF(0,50),bounds);
        QCOMPARE(resized,QRectF(0,20,110,60));
        QVERIFY(resizeFromHandle(QRectF(10,20,100,60),0,QPointF(110,80),bounds).width()>=2);
    }
    void orderedMosaicMatchesPreviewAndPartialUpdates() {
        QImage base(120,100,QImage::Format_ARGB32_Premultiplied);
        for (int y=0;y<base.height();++y) for (int x=0;x<base.width();++x)
            base.setPixelColor(x,y,((x+y)%2)?QColor("#dddddd"):QColor("#444444"));
        Annotation before; before.tool=Tool::Rectangle; before.rect=QRectF(20,20,20,20); before.width=12; before.color=Qt::red;
        Annotation mosaic; mosaic.tool=Tool::Mosaic; mosaic.rect=QRectF(10,10,70,70);
        Annotation after; after.tool=Tool::Rectangle; after.rect=QRectF(45,30,20,30); after.width=4; after.color=Qt::green;
        QVector<Annotation> items={before,mosaic,after};
        const AffineMap map;
        QImage layer; renderAnnotationLayer(layer,base,QRect(0,0,120,100),QPoint(),1,1,items);
        const QImage preview=compositeLayers(base,layer);
        QCOMPARE(preview,renderSelection(base,QSizeF(120,100),QRectF(0,0,120,100),items));
        int red=0;
        for (int y=10;y<80;++y) for(int x=10;x<80;++x) {
            const QColor color=preview.pixelColor(x,y);
            if (color.red()>180 && color.green()<80) ++red;
        }
        QVERIFY(red>0); // 后画的马赛克取样确实包含前面的红色标注。
        QCOMPARE(preview.pixelColor(45,45),QColor(Qt::green));
        const auto original=items; items[0].color=Qt::blue;
        const QRect damage=annotationDamage(original,items,map,base.rect());
        QVERIFY(!damage.isEmpty()); QVERIFY(damage.width()<base.width());
        renderAnnotationLayer(layer,base,map,items,damage);
        const QImage updated=compositeLayers(base,layer);
        QCOMPARE(updated,renderSelection(base,QSizeF(120,100),QRectF(0,0,120,100),items));
        QVERIFY(annotationDamage(items,items,map,base.rect()).isEmpty());
    }
    void mixedDpiMosaicUsesTheSameOutputGrid() {
        ScreenImage left; left.geometry=QRect(-100,0,100,100); left.image=QImage(150,150,QImage::Format_ARGB32); left.image.fill(Qt::white);
        ScreenImage right; right.geometry=QRect(0,0,100,100); right.image=QImage(200,200,QImage::Format_ARGB32); right.image.fill(Qt::gray);
        const QVector<ScreenImage> screens={left,right};
        const QRectF selection(-20.5,10.25,70,60);
        Annotation arrow; arrow.tool=Tool::Arrow; arrow.rect=QRectF(QPointF(-15,20),QPointF(35,50)); arrow.color=Qt::red; arrow.width=8;
        Annotation mosaic; mosaic.tool=Tool::Mosaic; mosaic.rect=QRectF(-12,12,40,40);
        const auto metrics=selectionMetrics(screens,selection);
        QCOMPARE(metrics.screenCount,2); QCOMPARE(metrics.pixelSize,QSize(140,121));
        const QImage base=renderSelectionBase(screens,selection);
        QImage layer; renderAnnotationLayer(layer,base,metrics.pixelFromLogical,{arrow,mosaic});
        QCOMPARE(compositeLayers(base,layer),renderSelection(screens,selection,{arrow,mosaic}));
        const QVector<Annotation> previous={arrow,mosaic}; arrow.color=Qt::blue;
        const QVector<Annotation> current={arrow,mosaic};
        renderAnnotationLayer(layer,base,metrics.pixelFromLogical,current,annotationDamage(previous,current,metrics.pixelFromLogical,base.rect()));
        QCOMPARE(compositeLayers(base,layer),renderSelection(screens,selection,current));
    }
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
    void stableIdentityAndNoOpHistory() {
        AnnotationDocument doc;
        Annotation a; a.rect=QRectF(10,10,20,20);
        const AnnotationId first=doc.add(a);
        const AnnotationId second=doc.add(a);
        QVERIFY(first!=0); QVERIFY(second!=first);
        doc.remove(0);
        QCOMPARE(doc.indexOf(second),0);
        doc.undoStack()->undo();
        QCOMPARE(doc.indexOf(first),0); QCOMPARE(doc.indexOf(second),1);
        const int depth=doc.undoDepth();
        doc.replace(1,doc.items()[1]);
        QCOMPARE(doc.undoDepth(),depth);
        doc.beginEdit();
        Annotation changed=doc.items()[1]; changed.width=12;
        doc.setItemInPlace(1,changed);
        doc.cancelEdit();
        QCOMPARE(doc.items()[1].width,3.0);
        QCOMPARE(doc.undoDepth(),depth);
        doc.undoStack()->undo();
        QCOMPARE(doc.indexOf(second),-1);
        const AnnotationId third=doc.add(a);
        QVERIFY(third!=second); QVERIFY(third!=first);
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
    void penStrokeIsHittable() {
        Annotation pen; pen.tool=Tool::Pen; pen.width=6; pen.color=Qt::red;
        pen.points={QPointF(20,50),QPointF(120,50),QPointF(120,90)};
        QVERIFY(annotationContains(pen,QPointF(60,50),2));
        QVERIFY(annotationContains(pen,QPointF(120,70),2));
        QVERIFY(!annotationContains(pen,QPointF(60,80),2));
        QVERIFY(!annotationContains(pen,QPointF(200,200),2));
    }
    void singlePointPenIsHittable() {
        Annotation pen; pen.tool=Tool::Pen; pen.width=8;
        pen.points={QPointF(50,50)};
        QVERIFY(annotationContains(pen,QPointF(52,52),2));
        QVERIFY(!annotationContains(pen,QPointF(70,70),2));
    }
    void mosaicAreaIsHittable() {
        Annotation mosaic; mosaic.tool=Tool::Mosaic; mosaic.rect=QRectF(30,30,60,40);
        QVERIFY(annotationContains(mosaic,QPointF(60,50),2));
        QVERIFY(annotationContains(mosaic,QPointF(33,33),2));
        QVERIFY(!annotationContains(mosaic,QPointF(120,120),2));
    }
    void rectangleHasSharpCorners() {
        const QRectF rect(50,50,100,100);
        QImage sharp(200,200,QImage::Format_ARGB32); sharp.fill(Qt::white);
        Annotation annotation; annotation.tool=Tool::Rectangle; annotation.rect=rect; annotation.color=Qt::red; annotation.width=8;
        QPainter painter(&sharp);
        drawAnnotation(painter,annotation);
        painter.end();
        // 同样的矩形用圆角笔刷渲染作为对照。
        QImage round(200,200,QImage::Format_ARGB32); round.fill(Qt::white);
        QPainter roundPainter(&round);
        roundPainter.setPen(QPen(Qt::red,8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        roundPainter.setBrush(Qt::NoBrush);
        roundPainter.drawRect(rect);
        roundPainter.end();
        // 角外侧方块内，直角版本覆盖的像素必须多于圆角版本。
        int sharpPixels=0, roundPixels=0;
        for (int y=44;y<52;++y) for (int x=44;x<52;++x) {
            const QColor s=sharp.pixelColor(x,y), r=round.pixelColor(x,y);
            if (s.red()>200 && s.green()<100) ++sharpPixels;
            if (r.red()>200 && r.green()<100) ++roundPixels;
        }
        QVERIFY2(sharpPixels>roundPixels,
                 qPrintable(QStringLiteral("直角=%1 圆角=%2").arg(sharpPixels).arg(roundPixels)));
        QVERIFY(sharp.pixelColor(50,50).red()>200);
    }
    void penScalingKeepsBounds() {
        // setBounds(target) 后，bounds() 必须仍等于 target（含线宽外扩语义）。
        Annotation pen; pen.tool=Tool::Pen; pen.width=6;
        pen.points={QPointF(20,20),QPointF(80,20),QPointF(80,60)};
        const QRectF target(100,100,200,120);
        pen.setBounds(target);
        const QRectF after=pen.bounds();
        QVERIFY(qAbs(after.left()-target.left())<0.01);
        QVERIFY(qAbs(after.top()-target.top())<0.01);
        QVERIFY(qAbs(after.width()-target.width())<0.02);
        QVERIFY(qAbs(after.height()-target.height())<0.02);
    }
    void penScalingDoesNotDriftByLineWidth() {
        // 包围盒含线宽外扩；缩放后笔迹本身不应整体平移一个线宽。
        Annotation pen; pen.tool=Tool::Pen; pen.width=10;
        pen.points={QPointF(40,40),QPointF(140,40)};
        const QVector<QPointF> before=pen.points;
        const QRectF beforeBounds=pen.bounds();
        pen.setBounds(QRectF(300,200,240,120));
        const QRectF after=pen.bounds();
        // 笔迹首点相对包围盒左上角的偏移应保持不变（仍等于一个线宽）。
        const qreal offsetBefore=before.first().x()-beforeBounds.left();
        const qreal offsetAfter=pen.points.first().x()-after.left();
        QVERIFY(qAbs(offsetBefore-offsetAfter)<0.01);
        // 末点相对包围盒右边界的偏移同样保持不变。
        const qreal tailBefore=beforeBounds.right()-before.last().x();
        const qreal tailAfter=after.right()-pen.points.last().x();
        QVERIFY(qAbs(tailBefore-tailAfter)<0.02);
    }
    void virtualDesktopIsUnionOfScreens() {
        QVector<ScreenImage> screens;
        ScreenImage left; left.geometry=QRect(0,0,800,600); left.image=QImage(1600,1200,QImage::Format_ARGB32);
        ScreenImage right; right.geometry=QRect(800,0,800,600); right.image=QImage(800,600,QImage::Format_ARGB32);
        screens<<left<<right;
        QCOMPARE(virtualGeometry(screens),QRect(0,0,1600,600));
        // 缩放比例按屏计算，不能全局统一。
        QVERIFY(qAbs(left.scaleX()-2.0)<0.001);
        QVERIFY(qAbs(right.scaleX()-1.0)<0.001);
        // 命中点落在第二块屏上。
        const ScreenImage *hit=screenAt(screens,QPointF(900,300));
        QVERIFY(hit!=nullptr); QCOMPARE(hit->geometry,QRect(800,0,800,600));
        QVERIFY(screenAt(screens,QPointF(400,300))!=nullptr);
    }
    void affineMapsRoundTrip() {
        // 原生像素 → 逻辑坐标
        const QRect native(0,0,1920,1080), logical(-1536,0,1536,864);
        const AffineMap map=logicalMapFromNative(native,logical);
        QVERIFY(qAbs(map.map(QPointF(0,0)).x()+1536)<0.001);
        QVERIFY(qAbs(map.map(QPointF(0,0)).y())<0.001);
        QVERIFY(qAbs(map.map(QPointF(1920,1080)).x())<0.001);
        QVERIFY(qAbs(map.map(QPointF(1920,1080)).y()-864)<0.001);
        QVERIFY(qAbs(map.unmap(map.map(QPointF(500,500))).x()-500)<0.001);
        QVERIFY(qAbs(map.unmap(map.map(QPointF(500,500))).y()-500)<0.001);
        // 逻辑坐标 → 屏幕像素
        const AffineMap pixels=pixelMapFromScreen(QRect(800,0,800,600),QSize(1600,1200));
        QVERIFY(qAbs(pixels.map(QPointF(800,0)).x())<0.001);
        QVERIFY(qAbs(pixels.map(QPointF(800,0)).y())<0.001);
        QVERIFY(qAbs(pixels.map(QPointF(1600,600)).x()-1600)<0.001);
        QVERIFY(qAbs(pixels.map(QPointF(1600,600)).y()-1200)<0.001);
        QVERIFY(qAbs(pixels.unmap(pixels.map(QPointF(1000,300))).x()-1000)<0.001);
        QVERIFY(qAbs(pixels.unmap(pixels.map(QPointF(1000,300))).y()-300)<0.001);
    }
    void singleScreenRenderMatchesLegacy() {
        // 选区只落在一块屏上时，新接口必须与旧的单屏实现逐像素一致。
        QImage image(200,200,QImage::Format_ARGB32); image.fill(Qt::white);
        Annotation a; a.tool=Tool::Rectangle; a.rect=QRectF(20,20,20,20); a.color=Qt::red; a.width=2;
        const auto legacy=renderSelection(image,QSizeF(100,100),QRectF(10,10,60,60),{a});
        ScreenImage screen; screen.geometry=QRect(0,0,100,100); screen.image=image;
        const auto modern=renderSelection(QVector<ScreenImage>{screen},QRectF(10,10,60,60),{a});
        QCOMPARE(modern.size(),legacy.size());
        for (int y=0;y<legacy.height();++y) for (int x=0;x<legacy.width();++x)
            if (modern.pixelColor(x,y)!=legacy.pixelColor(x,y)) {
                QVERIFY2(false,qPrintable(QStringLiteral("首个不同的像素 (%1,%2)").arg(x).arg(y)));
            }
    }
    void crossScreenRenderKeepsBothScreens() {
        // 左屏 2x，右屏 1x；选区横跨两屏，输出比例取 2x，两屏都不被降采样。
        QVector<ScreenImage> screens;
        ScreenImage retina; retina.geometry=QRect(0,0,800,600);
        retina.image=QImage(1600,1200,QImage::Format_ARGB32); retina.image.fill(Qt::red);
        ScreenImage plain; plain.geometry=QRect(800,0,800,600);
        plain.image=QImage(800,600,QImage::Format_ARGB32); plain.image.fill(Qt::blue);
        screens<<retina<<plain;
        // 选区逻辑范围 x=700..900，跨过两屏边界 x=800。
        const auto out=renderSelection(screens,QRectF(700,100,200,100),{});
        QCOMPARE(out.size(),QSize(400,200));
        // 左半是 2x 屏，右半是 1x 屏被放大到 2x。
        QCOMPARE(out.pixelColor(10,100),QColor(Qt::red));
        QCOMPARE(out.pixelColor(390,100),QColor(Qt::blue));
        // 两屏交界处不能出现空隙。
        QCOMPARE(out.pixelColor(199,100),QColor(Qt::red));
        QCOMPARE(out.pixelColor(200,100),QColor(Qt::blue));
    }
    void crossScreenRenderUsesLargestScaleOfTouchedScreens() {
        // 选区只落在 1x 屏上时按 1x 输出，落在 2x 屏上时按 2x 输出。
        QVector<ScreenImage> screens;
        ScreenImage retina; retina.geometry=QRect(0,0,800,600);
        retina.image=QImage(1600,1200,QImage::Format_ARGB32); retina.image.fill(Qt::red);
        ScreenImage plain; plain.geometry=QRect(800,0,800,600);
        plain.image=QImage(800,600,QImage::Format_ARGB32); plain.image.fill(Qt::blue);
        screens<<retina<<plain;
        QCOMPARE(renderSelection(screens,QRectF(900,100,100,100),{}).size(),QSize(100,100));
        QCOMPARE(renderSelection(screens,QRectF(100,100,100,100),{}).size(),QSize(200,200));
    }
    void crossScreenRenderDrawsAnnotations() {
        QVector<ScreenImage> screens;
        ScreenImage retina; retina.geometry=QRect(0,0,800,600);
        retina.image=QImage(1600,1200,QImage::Format_ARGB32); retina.image.fill(Qt::white);
        ScreenImage plain; plain.geometry=QRect(800,0,800,600);
        plain.image=QImage(800,600,QImage::Format_ARGB32); plain.image.fill(Qt::white);
        screens<<retina<<plain;
        Annotation a; a.tool=Tool::Rectangle; a.rect=QRectF(700,100,200,100); a.color=Qt::red; a.width=2;
        const auto out=renderSelection(screens,QRectF(700,100,200,100),{a});
        QCOMPARE(out.size(),QSize(400,200));
        // 矩形边框横跨两屏，两侧都要画出来。
        QVERIFY(out.pixelColor(1,100).red()>200);
        QVERIFY(out.pixelColor(398,100).red()>200);
        QCOMPARE(out.pixelColor(200,100),QColor(Qt::white));
    }
    void crossScreenRenderMatchesSingleScreenOnOffsetScreen() {
        // 选区落在一块「偏移 800 且 2x」的屏上时，跨屏接口必须与单屏接口逐像素一致。
        // 标注坐标是虚拟桌面逻辑坐标，换算到该屏像素时原点与比例都要参与，否则整块错位。
        QImage image(1600,1200,QImage::Format_ARGB32); image.fill(Qt::white);
        Annotation a; a.tool=Tool::Rectangle; a.rect=QRectF(100,100,100,100); a.color=Qt::red; a.width=2;
        const auto legacy=renderSelection(image,QSizeF(800,600),QRectF(100,100,100,100),{a});
        Annotation b=a; b.translate(QPointF(800,0));
        ScreenImage retina; retina.geometry=QRect(800,0,800,600); retina.image=image;
        const auto modern=renderSelection(QVector<ScreenImage>{retina},QRectF(900,100,100,100),{b});
        QCOMPARE(modern.size(),legacy.size());
        for (int y=0;y<legacy.height();++y) for (int x=0;x<legacy.width();++x)
            if (modern.pixelColor(x,y)!=legacy.pixelColor(x,y)) {
                QVERIFY2(false,qPrintable(QStringLiteral("首个不同的像素 (%1,%2) 期望 %3 实际 %4")
                            .arg(x).arg(y).arg(legacy.pixelColor(x,y).name()).arg(modern.pixelColor(x,y).name())));
            }
    }
};
QTEST_MAIN(CoreTests)
#include "core_tests.moc"
