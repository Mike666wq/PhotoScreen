#pragma once

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
}
