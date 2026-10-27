#pragma once
#include <QImage>
#include <QPointF>
#include <QRectF>

namespace photo {
// 仿射映射：target = source * scale + offset。
// 用来在不同坐标系之间换算（原生像素 ↔ Qt 逻辑 ↔ 屏幕像素）。
struct AffineMap {
    qreal scaleX = 1.0;
    qreal scaleY = 1.0;
    QPointF offset;
    QPointF map(const QPointF &source) const { return {source.x()*scaleX+offset.x(), source.y()*scaleY+offset.y()}; }
    QPointF unmap(const QPointF &target) const { return {(target.x()-offset.x())/scaleX, (target.y()-offset.y())/scaleY}; }
};
QRect pixelRect(const QRectF &logical, const QSizeF &logicalSize, const QSize &pixelSize);
QRectF clampRect(const QRectF &rect, const QRectF &bounds);
// 原生屏幕坐标 → Qt 逻辑坐标 的映射。
AffineMap logicalMapFromNative(const QRect &nativeScreen, const QRect &logicalScreen);
// Qt 逻辑坐标 → 屏幕像素坐标 的映射。
AffineMap pixelMapFromScreen(const QRect &logicalGeometry, const QSize &pixelSize);
QRect nativeWindowToLogical(const QRect &window, const QRect &nativeScreen, const QRect &logicalScreen);
QImage cropPixels(const QImage &image, const QSizeF &logicalSize, const QRectF &selection);
QImage magnifierSample(const QImage &image, const QPoint &pixel, int radius = 8);
}
