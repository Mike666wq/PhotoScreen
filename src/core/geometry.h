#pragma once
#include <QImage>
#include <QRectF>

namespace photo {
QRect pixelRect(const QRectF &logical, const QSizeF &logicalSize, const QSize &pixelSize);
QRectF clampRect(const QRectF &rect, const QRectF &bounds);
QImage cropPixels(const QImage &image, const QSizeF &logicalSize, const QRectF &selection);
}
