#include "interaction.h"

#include <QtMath>
#include <algorithm>

namespace photo {
QRect interactionScreen(const QVector<QRect> &screens, const QPointF &pointer, const QRectF &selection) {
    QRect best;
    qreal largest=-1;
    for (const QRect &screen : screens) {
        if (screen.isEmpty()) continue;
        if (QRectF(screen).contains(pointer)) return screen;
        const QRectF overlap=QRectF(screen).intersected(selection);
        const qreal area=overlap.width()*overlap.height();
        if (area>largest) { largest=area; best=screen; }
    }
    return best;
}
QRect fitInsideScreen(const QRect &desired, const QRect &screen, int margin) {
    if (screen.isEmpty()) return {};
    const int inset=qMin(margin,qMax(0,(qMin(screen.width(),screen.height())-1)/2));
    const QRect safe=screen.adjusted(inset,inset,-inset,-inset);
    const QSize size(qMin(desired.width(),safe.width()),qMin(desired.height(),safe.height()));
    return QRect(QPoint(qBound(safe.left(),desired.left(),safe.right()-size.width()+1),
                        qBound(safe.top(),desired.top(),safe.bottom()-size.height()+1)),size);
}
QRect toolbarPlacement(const QSize &size, const QRectF &selection, const QRect &screen) {
    const QRectF local=selection.intersected(screen);
    const int x=qRound(local.right())-size.width();
    const QRect below=fitInsideScreen(QRect(QPoint(x,qCeil(local.bottom())+8),size),screen);
    const QRect above=fitInsideScreen(QRect(QPoint(x,qFloor(local.top())-size.height()-8),size),screen);
    const QRectF lowerOverlap=QRectF(below).intersected(selection);
    const QRectF upperOverlap=QRectF(above).intersected(selection);
    const qreal lowerArea=lowerOverlap.width()*lowerOverlap.height();
    const qreal upperArea=upperOverlap.width()*upperOverlap.height();
    return lowerArea<=upperArea ? below : above;
}
QRect popupPlacement(const QSize &size, const QRect &avoid, const QPoint &anchor,
                     const QRect &screen, bool preferAbove) {
    const QRect safe=screen.adjusted(8,8,-8,-8);
    const QRect above(safe.left(),safe.top(),safe.width(),qMax(0,avoid.top()-8-safe.top()));
    const int belowTop=avoid.bottom()+9;
    const QRect below(safe.left(),belowTop,safe.width(),qMax(0,safe.bottom()-belowTop+1));
    const QRect left(safe.left(),safe.top(),qMax(0,avoid.left()-8-safe.left()),safe.height());
    const int rightLeft=avoid.right()+9;
    const QRect right(rightLeft,safe.top(),qMax(0,safe.right()-rightLeft+1),safe.height());
    const QVector<QRect> candidates=preferAbove ? QVector<QRect>{above,below,left,right} : QVector<QRect>{below,above,right,left};
    QRect best;
    qint64 largest=-1;
    for (const QRect &area : candidates) {
        if (area.isEmpty()) continue;
        const QSize fitted(qMin(size.width(),area.width()),qMin(size.height(),area.height()));
        const qint64 score=qint64(fitted.width())*fitted.height();
        const QRect desired(QPoint(anchor.x()-fitted.width()/2,
                                    area==above ? area.bottom()-fitted.height()+1 : area.top()),fitted);
        const QRect placed=fitInsideScreen(desired,area,0);
        if (fitted==size) return placed;
        if (score>largest) { largest=score; best=placed; }
    }
    return best.isEmpty() ? fitInsideScreen(QRect(anchor,size),screen) : best;
}
}
