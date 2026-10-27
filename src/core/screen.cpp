#include "screen.h"

namespace photo {
QRect virtualGeometry(const QVector<ScreenImage> &screens) {
    QRect result;
    for (const auto &screen : screens) {
        if (screen.geometry.isEmpty()) continue;
        result = result.isNull() ? screen.geometry : result.united(screen.geometry);
    }
    return result;
}
const ScreenImage *screenAt(const QVector<ScreenImage> &screens, const QPointF &logical) {
    const ScreenImage *hit = nullptr;
    for (const auto &screen : screens) {
        if (screen.isEmpty() || !screen.geometry.contains(logical.toPoint())) continue;
        // 屏幕边界重叠时取比例最大的那块，保证取到的像素最精细。
        if (!hit || screen.scaleX() > hit->scaleX()) hit = &screen;
    }
    return hit;
}
}
