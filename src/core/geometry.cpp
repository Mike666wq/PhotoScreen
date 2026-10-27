#include "geometry.h"
#include <QtMath>
#include <QPainter>
#include <algorithm>

namespace photo {
QRect pixelRect(const QRectF &logical, const QSizeF &logicalSize, const QSize &pixelSize) {
    if (logicalSize.width() <= 0 || logicalSize.height() <= 0 || pixelSize.isEmpty()) return {};
    const QRectF r = logical.normalized().intersected(QRectF(QPointF(0, 0), logicalSize));
    if (r.isEmpty()) return {};
    const qreal sx = pixelSize.width() / logicalSize.width();
    const qreal sy = pixelSize.height() / logicalSize.height();
    const int left = std::clamp(qFloor(r.left() * sx), 0, pixelSize.width());
    const int top = std::clamp(qFloor(r.top() * sy), 0, pixelSize.height());
    const int right = std::clamp(qCeil(r.right() * sx), left, pixelSize.width());
    const int bottom = std::clamp(qCeil(r.bottom() * sy), top, pixelSize.height());
    return QRect(left, top, right - left, bottom - top);
}
QRectF clampRect(const QRectF &rect, const QRectF &bounds) {
    return rect.normalized().intersected(bounds);
}
AffineMap logicalMapFromNative(const QRect &nativeScreen, const QRect &logicalScreen) {
    AffineMap map;
    if (nativeScreen.isEmpty() || logicalScreen.isEmpty()) return map;
    map.scaleX = qreal(logicalScreen.width()) / nativeScreen.width();
    map.scaleY = qreal(logicalScreen.height()) / nativeScreen.height();
    map.offset = QPointF(logicalScreen.x(), logicalScreen.y())
               - QPointF(nativeScreen.x()*map.scaleX, nativeScreen.y()*map.scaleY);
    return map;
}
AffineMap pixelMapFromScreen(const QRect &logicalGeometry, const QSize &pixelSize) {
    AffineMap map;
    if (logicalGeometry.isEmpty() || pixelSize.isEmpty()) return map;
    map.scaleX = qreal(pixelSize.width()) / logicalGeometry.width();
    map.scaleY = qreal(pixelSize.height()) / logicalGeometry.height();
    map.offset = QPointF(-logicalGeometry.x()*map.scaleX, -logicalGeometry.y()*map.scaleY);
    return map;
}
QRect nativeWindowToLogical(const QRect &window, const QRect &nativeScreen, const QRect &logicalScreen) {
    if (nativeScreen.isEmpty() || logicalScreen.isEmpty()) return {};
    const QRect clipped=window.intersected(nativeScreen);
    if (clipped.isEmpty()) return {};
    const qreal sx=qreal(logicalScreen.width())/nativeScreen.width();
    const qreal sy=qreal(logicalScreen.height())/nativeScreen.height();
    return QRect(logicalScreen.x()+qRound((clipped.x()-nativeScreen.x())*sx),
                 logicalScreen.y()+qRound((clipped.y()-nativeScreen.y())*sy),
                 qRound(clipped.width()*sx),qRound(clipped.height()*sy));
}
QImage cropPixels(const QImage &image, const QSizeF &logicalSize, const QRectF &selection) {
    return image.copy(pixelRect(selection, logicalSize, image.size()));
}
QImage magnifierSample(const QImage &image, const QPoint &pixel, int radius) {
    if (image.isNull() || radius < 0) return {};
    const int side=radius*2+1;
    QImage sample(side,side,QImage::Format_ARGB32_Premultiplied); sample.fill(Qt::black);
    const QRect requested(pixel.x()-radius,pixel.y()-radius,side,side);
    const QRect available=requested.intersected(image.rect());
    if (!available.isEmpty()) {
        QPainter painter(&sample);
        painter.drawImage(available.topLeft()-requested.topLeft(),image,available);
    }
    return sample;
}
}
