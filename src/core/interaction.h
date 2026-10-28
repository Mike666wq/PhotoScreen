#pragma once

#include "annotation.h"

#include <QPointF>
#include <QRect>
#include <QSize>
#include <QVector>

namespace photo {
// 布局输入、输出均使用同一个 Qt 逻辑坐标空间。
QRect interactionScreen(const QVector<QRect> &screens, const QPointF &pointer, const QRectF &selection);
QRect fitInsideScreen(const QRect &desired, const QRect &screen, int margin = 8);
QRect toolbarPlacement(const QSize &size, const QRectF &selection, const QRect &screen);
QRect popupPlacement(const QSize &size, const QRect &avoid, const QPoint &anchor,
                     const QRect &screen, bool preferAbove);
QVector<QPointF> rectangleHandles(const QRectF &rect);
QVector<QPointF> annotationHandles(const Annotation &annotation);
int hitHandle(const QVector<QPointF> &handles, const QPointF &point, qreal radius = 6);
QRectF resizeFromHandle(const QRectF &start, int handle, const QPointF &point,
                        const QRectF &bounds, qreal minimum = 2);
QPointF constrainedSquareEnd(const QPointF &start, const QPointF &point, const QRectF &bounds);
QPointF constrainedArrowEnd(const QPointF &start, const QPointF &point, const QRectF &bounds);
}
