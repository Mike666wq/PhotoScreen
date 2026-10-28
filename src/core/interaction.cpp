#include "interaction.h"

#include <QLineF>
#include <QtMath>
#include <algorithm>

namespace photo {
QVector<QPointF> rectangleHandles(const QRectF &r) {
    return {r.topLeft(),QPointF(r.center().x(),r.top()),r.topRight(),QPointF(r.right(),r.center().y()),
            r.bottomRight(),QPointF(r.center().x(),r.bottom()),r.bottomLeft(),QPointF(r.left(),r.center().y())};
}
QVector<QPointF> annotationHandles(const Annotation &annotation) {
    if (annotation.tool==Tool::Arrow) return {annotation.rect.topLeft(),annotation.rect.bottomRight()};
    const QRectF rect=annotation.bounds();
    if (annotation.tool==Tool::Text) return {QPointF(rect.left(),rect.center().y()),QPointF(rect.right(),rect.center().y())};
    return rectangleHandles(rect);
}
int hitHandle(const QVector<QPointF> &handles, const QPointF &point, qreal radius) {
    for (int i=0;i<handles.size();++i)
        if (QRectF(handles[i]-QPointF(radius,radius),QSizeF(2*radius,2*radius)).contains(point)) return i;
    return -1;
}
QRectF resizeFromHandle(const QRectF &start, int handle, const QPointF &point, const QRectF &bounds, qreal minimum) {
    const QPointF p(qBound(bounds.left(),point.x(),bounds.right()),qBound(bounds.top(),point.y(),bounds.bottom()));
    QRectF rect=start;
    if (handle==0 || handle==6 || handle==7) rect.setLeft(p.x());
    if (handle==2 || handle==3 || handle==4) rect.setRight(p.x());
    if (handle>=0 && handle<=2) rect.setTop(p.y());
    if (handle>=4 && handle<=6) rect.setBottom(p.y());
    rect=rect.normalized().intersected(bounds);
    const qreal minWidth=qMin(minimum,bounds.width()),minHeight=qMin(minimum,bounds.height());
    if (rect.width()<minWidth) rect.setWidth(minWidth);
    if (rect.height()<minHeight) rect.setHeight(minHeight);
    rect.moveLeft(qBound(bounds.left(),rect.left(),bounds.right()-rect.width()));
    rect.moveTop(qBound(bounds.top(),rect.top(),bounds.bottom()-rect.height()));
    return rect;
}
QPointF constrainedSquareEnd(const QPointF &start, const QPointF &point, const QRectF &bounds) {
    const QPointF delta=point-start;
    const int xSign=delta.x()<0?-1:1,ySign=delta.y()<0?-1:1;
    qreal side=qMax(qAbs(delta.x()),qAbs(delta.y()));
    side=qMin(side,xSign>0?bounds.right()-start.x():start.x()-bounds.left());
    side=qMax(0.0,qMin(side,ySign>0?bounds.bottom()-start.y():start.y()-bounds.top()));
    return start+QPointF(xSign*side,ySign*side);
}
QPointF constrainedArrowEnd(const QPointF &start, const QPointF &point, const QRectF &bounds) {
    const QPointF delta=point-start;
    constexpr qreal step=3.14159265358979323846/4;
    const qreal angle=qRound(qAtan2(delta.y(),delta.x())/step)*step;
    const QPointF direction(qCos(angle),qSin(angle));
    qreal length=QLineF(start,point).length();
    if (qAbs(direction.x())>0.00001)
        length=qMin(length,(direction.x()>0?bounds.right()-start.x():bounds.left()-start.x())/direction.x());
    if (qAbs(direction.y())>0.00001)
        length=qMin(length,(direction.y()>0?bounds.bottom()-start.y():bounds.top()-start.y())/direction.y());
    return start+direction*qMax(0.0,length);
}
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
