#include "core/annotation.h"
#include "report_font.h"
#include <QGuiApplication>
#include <QDir>
#include <QPainter>
#include <QtMath>

int main(int argc, char **argv) {
    QGuiApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PhotoScreenTests"));
    QCoreApplication::setApplicationName(QStringLiteral("VisualReport"));
    if (!loadReportFont()) return 1;
    const QString outputDir=qEnvironmentVariable("PHOTOSCREEN_REPORT_DIR");
    if (outputDir.isEmpty() || !QDir().mkpath(outputDir)) return 1;
    QImage image(1200,800,QImage::Format_RGB32); image.fill(Qt::white);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    QFont font=p.font(); font.setPixelSize(20); p.setFont(font); p.setPen(QColor("#333333"));
    p.drawText(QPointF(30,35),QStringLiteral("Arrow length and thickness (synthetic fixture)"));
    const QColor red("#ff3b3b");
    const int lengths[]={90,180,320}; const int widths[]={3,12};
    for (int row=0;row<2;++row) for (int col=0;col<3;++col) {
        const QPointF start(50+col*390,125+row*170);
        photo::Annotation a; a.tool=photo::Tool::Arrow; a.color=red; a.width=widths[row];
        a.rect=QRectF(start,start+QPointF(lengths[col],-35)); photo::drawAnnotation(p,a);
        p.setPen(QColor("#333333")); p.drawText(QPointF(start.x(),start.y()+50),QStringLiteral("length %1 / width %2").arg(lengths[col]).arg(widths[row]));
    }
    p.setPen(QColor("#333333")); p.drawText(QPointF(30,405),QStringLiteral("Directions and endpoint geometry"));
    for(int i=0;i<8;++i) {
        const qreal angle=i*3.141592653589793/4;
        const QPointF center(100+i*145,600), direction(qCos(angle),qSin(angle));
        photo::Annotation a; a.tool=photo::Tool::Arrow; a.color=red; a.width=6;
        a.rect=QRectF(center-direction*60,center+direction*60); photo::drawAnnotation(p,a);
    }
    p.end();
    if (!image.save(outputDir+QStringLiteral("/arrow-length-thickness-directions.png"))) return 1;

    QImage textImage(900,420,QImage::Format_RGB32); textImage.fill(Qt::white);
    QPainter textPainter(&textImage); textPainter.setRenderHint(QPainter::Antialiasing);
    QFont heading=textPainter.font(); heading.setPixelSize(22); textPainter.setFont(heading);
    textPainter.setPen(QColor("#333333"));
    textPainter.drawText(QPointF(30,38),QStringLiteral("Text size and weight (synthetic fixture)"));
    const int weights[]={100,300,400,600,800,900};
    const QString names[]={QStringLiteral("Thin 100"),QStringLiteral("Light 300"),QStringLiteral("Regular 400"),
                           QStringLiteral("DemiBold 600"),QStringLiteral("ExtraBold 800"),QStringLiteral("Black 900")};
    for (int i=0;i<6;++i) {
        photo::Annotation a; a.tool=photo::Tool::Text; a.color=QColor("#ff3b3b");
        a.rect=QRectF(30+(i%2)*440,65+(i/2)*110,390,80);
        a.text=QStringLiteral("PhotoScreen 123");
        a.fontSize=34; a.fontWeight=weights[i];
        photo::drawAnnotation(textPainter,a);
        QFont caption=textPainter.font(); caption.setPixelSize(13); caption.setWeight(QFont::Normal);
        textPainter.setFont(caption); textPainter.setPen(QColor("#555555"));
        textPainter.drawText(QPointF(a.rect.left(),a.rect.bottom()+18),names[i]);
    }
    textPainter.end();
    if (!textImage.save(outputDir+QStringLiteral("/text-size-weight.png"))) return 1;
    QImage source(800,360,QImage::Format_ARGB32_Premultiplied); source.fill(QColor("#eef3fa"));
    QPainter fixture(&source); fixture.setPen(QColor("#26374e"));
    QFont fixtureFont=fixture.font(); fixtureFont.setPixelSize(22); fixture.setFont(fixtureFont);
    fixture.drawText(QPointF(30,40),QStringLiteral("合成图：先画箭头，再打马赛克，最后写清晰文字"));
    fixture.end();
    photo::Annotation arrow; arrow.tool=photo::Tool::Arrow; arrow.color=red; arrow.width=8;
    arrow.rect=QRectF(QPointF(50,290),QPointF(680,110));
    photo::Annotation mosaic; mosaic.tool=photo::Tool::Mosaic; mosaic.rect=QRectF(280,100,250,190);
    photo::Annotation caption; caption.tool=photo::Tool::Text; caption.color=QColor("#0060cc");
    caption.rect=QRectF(350,180,350,70); caption.fontSize=28; caption.fontWeight=600; caption.text=QStringLiteral("后画文字保持清晰");
    const QImage result=photo::renderSelection(source,QSizeF(800,360),QRectF(0,0,800,360),{arrow,mosaic,caption});
    return result.save(outputDir+QStringLiteral("/mosaic-drawing-order.png")) ? 0 : 1;
}
