#pragma once
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QVector>

namespace photo {
// 虚拟桌面中的一块屏幕：逻辑位置 + 物理像素图。
// 不同屏幕的缩放比例可能不同（Retina 与 1x 屏混用），因此比例按屏计算，
// 不能用全局统一比例，否则跨屏选区会有一侧被降采样。
struct ScreenImage {
    QRect geometry;                 // 该屏在虚拟桌面逻辑坐标中的矩形
    QImage image;                   // 该屏的物理像素图
    bool isEmpty() const { return geometry.isEmpty() || image.isNull(); }
    qreal scaleX() const { return geometry.isEmpty() || image.isNull() ? 1.0 : qreal(image.width()) / geometry.width(); }
    qreal scaleY() const { return geometry.isEmpty() || image.isNull() ? 1.0 : qreal(image.height()) / geometry.height(); }
};
// 所有屏幕的并集，即虚拟桌面在逻辑坐标中的范围。
QRect virtualGeometry(const QVector<ScreenImage> &screens);
// 命中给定点所在屏幕；落在多个屏上时返回比例最大的那块。
const ScreenImage *screenAt(const QVector<ScreenImage> &screens, const QPointF &logical);
}
