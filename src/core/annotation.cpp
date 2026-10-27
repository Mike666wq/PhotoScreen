#include "annotation.h"
#include "geometry.h"
#include <QPainter>
#include <QUndoCommand>
#include <QtMath>
#include <algorithm>

namespace photo {
QRectF Annotation::bounds() const {
    if (tool == Tool::Pen && !points.isEmpty()) {
        QRectF box(points.first(), QSizeF(0, 0));
        for (const auto &point : points) box = box.united(QRectF(point, QSizeF(0.01, 0.01)));
        return box.adjusted(-width, -width, width, width);
    }
    return rect.normalized();
}
void Annotation::translate(const QPointF &delta) {
    rect.translate(delta);
    for (auto &point : points) point += delta;
}
void Annotation::setBounds(const QRectF &target) {
    const QRectF old = bounds();
    if (old.isEmpty()) { rect = target; return; }
    if (!points.isEmpty()) {
        for (auto &p : points) {
            p = QPointF(target.left() + (p.x() - old.left()) * target.width() / old.width(),
                        target.top() + (p.y() - old.top()) * target.height() / old.height());
        }
    }
    const bool reversedX = rect.width() < 0;
    const bool reversedY = rect.height() < 0;
    rect = QRectF(reversedX ? target.right() : target.left(), reversedY ? target.bottom() : target.top(),
                  reversedX ? -target.width() : target.width(), reversedY ? -target.height() : target.height());
}
class Change final : public QUndoCommand {
public:
    Change(AnnotationDocument *doc, QVector<Annotation> before, QVector<Annotation> after)
        : m_doc(doc), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { m_doc->setItemsForCommand(m_before); }
    void redo() override { m_doc->setItemsForCommand(m_after); }
private:
    AnnotationDocument *m_doc;
    QVector<Annotation> m_before, m_after;
};
void AnnotationDocument::add(const Annotation &item) {
    auto after = m_items; after.append(item); m_undo.push(new Change(this, m_items, after));
}
void AnnotationDocument::replace(int index, const Annotation &item) {
    if (index < 0 || index >= m_items.size()) return;
    auto after = m_items; after[index] = item; m_undo.push(new Change(this, m_items, after));
}
void AnnotationDocument::remove(int index) {
    if (index < 0 || index >= m_items.size()) return;
    auto after = m_items; after.removeAt(index); m_undo.push(new Change(this, m_items, after));
}
void drawAnnotation(QPainter &p, const Annotation &a) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(a.color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (a.tool) {
    case Tool::Rectangle: p.drawRect(a.rect.normalized()); break;
    case Tool::Ellipse: p.drawEllipse(a.rect.normalized()); break;
    case Tool::Arrow: {
        const QPointF start = a.rect.topLeft(), end = a.rect.bottomRight();
        p.drawLine(start, end);
        const QLineF line(start, end);
        if (line.length() > 0.01) {
            const qreal angle = qAtan2(end.y() - start.y(), end.x() - start.x());
            const qreal length = qMax<qreal>(10, a.width * 4);
            const QPointF v1(end.x() - length * qCos(angle - 0.5), end.y() - length * qSin(angle - 0.5));
            const QPointF v2(end.x() - length * qCos(angle + 0.5), end.y() - length * qSin(angle + 0.5));
            p.drawLine(end, v1); p.drawLine(end, v2);
        }
        break;
    }
    case Tool::Pen:
        if (a.points.size() == 1) p.drawPoint(a.points.first());
        else if (!a.points.isEmpty()) { QPainterPath path(a.points.first()); for (qsizetype i=1; i<a.points.size(); ++i) path.lineTo(a.points[i]); p.drawPath(path); }
        break;
    case Tool::Text: {
        QFont font = p.font(); font.setPixelSize(a.fontSize); p.setFont(font);
        p.drawText(a.rect.normalized(), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, a.text);
        break;
    }
    default: break;
    }
    p.restore();
}
QImage renderSelection(const QImage &source, const QSizeF &logicalSize, const QRectF &selection,
                       const QVector<Annotation> &annotations) {
    const QRect crop = pixelRect(selection, logicalSize, source.size());
    if (crop.isEmpty()) return {};
    QImage out = source.copy(crop).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal sx = source.width() / logicalSize.width();
    const qreal sy = source.height() / logicalSize.height();
    for (const auto &a : annotations) {
        if (a.tool == Tool::Mosaic) {
            const QRect area = pixelRect(a.rect, logicalSize, source.size()).intersected(crop).translated(-crop.topLeft());
            if (area.isEmpty()) continue;
            const QImage piece = out.copy(area);
            const int block = qMax(6, qRound(12 * qMin(sx, sy)));
            const QImage small = piece.scaled(qMax(1, area.width()/block), qMax(1, area.height()/block), Qt::IgnoreAspectRatio, Qt::FastTransformation);
            QPainter p(&out); p.setRenderHint(QPainter::SmoothPixmapTransform, false);
            p.drawImage(area, small.scaled(area.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
        } else {
            QPainter p(&out);
            p.translate(-crop.x(), -crop.y()); p.scale(sx, sy);
            drawAnnotation(p, a);
        }
    }
    return out;
}
}
