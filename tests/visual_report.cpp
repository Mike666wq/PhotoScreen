#include "core/annotation.h"
#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QtMath>

int main(int argc, char **argv) {
    QApplication app(argc,argv);
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
    return image.save(outputDir+QStringLiteral("/arrow-length-thickness-directions.png")) ? 0 : 1;
}
