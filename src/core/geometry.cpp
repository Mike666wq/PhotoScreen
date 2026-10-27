#include "geometry.h"
#include <QtMath>
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
QImage cropPixels(const QImage &image, const QSizeF &logicalSize, const QRectF &selection) {
    return image.copy(pixelRect(selection, logicalSize, image.size()));
}
}
