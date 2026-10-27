#include "annotation.h"
#include "geometry.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QApplication>
#include <QTextCursor>
#include <QTextDocument>
#include <QUndoCommand>
#include <QtMath>
#include <algorithm>

namespace photo {
QRectF Annotation::bounds() const {
    if (tool == Tool::Arrow) return arrowPath(*this).boundingRect();
    if (tool == Tool::Pen && !points.isEmpty()) {
        QRectF box(points.first(), QSizeF(0, 0));
        for (const auto &point : points) box = box.united(QRectF(point, QSizeF(0.01, 0.01)));
        return box.adjusted(-width, -width, width, width);
    }
    return rect.normalized();
}
QPainterPath arrowPath(const Annotation &a) {
    QPainterPath path;
    const QPointF start=a.rect.topLeft(), end=a.rect.bottomRight();
    const QLineF axis(start,end);
    const qreal length=axis.length();
    if (length<0.01) return path;
    const QPointF direction=(end-start)/length;
    const QPointF normal(-direction.y(),direction.x());
    const qreal scale=qBound<qreal>(1.0/3.0,a.width/3.0,20.0/3.0);
    const qreal headLength=qMin(length*0.42,length*0.10*scale);
    const qreal headHalfWidth=qMin(length*0.28,length*0.055*scale);
    const qreal neckHalfWidth=qMin(headHalfWidth*0.8,length*0.020*scale);
    const QPointF base=end-direction*headLength;
    path.moveTo(start);
    path.lineTo(base+normal*neckHalfWidth);
    path.lineTo(base+normal*headHalfWidth);
    path.lineTo(end);
    path.lineTo(base-normal*headHalfWidth);
    path.lineTo(base-normal*neckHalfWidth);
    path.closeSubpath();
    return path;
}
bool annotationContains(const Annotation &a, const QPointF &point, qreal tolerance) {
    if (a.tool==Tool::Text) return a.bounds().adjusted(-tolerance,-tolerance,tolerance,tolerance).contains(point);
    QPainterPath path;
    if (a.tool==Tool::Arrow) path=arrowPath(a);
    else if (a.tool==Tool::Rectangle) path.addRect(a.rect.normalized());
    else if (a.tool==Tool::Ellipse) path.addEllipse(a.rect.normalized());
    else return false;
    QPainterPathStroker stroker; stroker.setWidth(qMax<qreal>(1,a.width)+2*tolerance);
    return (a.tool==Tool::Arrow && path.contains(point)) || stroker.createStroke(path).contains(point);
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
        p.setPen(Qt::NoPen); p.setBrush(a.color); p.drawPath(arrowPath(a));
        break;
    }
    case Tool::Pen:
        if (a.points.size() == 1) p.drawPoint(a.points.first());
        else if (!a.points.isEmpty()) { QPainterPath path(a.points.first()); for (qsizetype i=1; i<a.points.size(); ++i) path.lineTo(a.points[i]); p.drawPath(path); }
        break;
    case Tool::Text: {
        const QRectF rect=a.rect.normalized();
        QFont font=QApplication::font();
        if (!a.fontFamily.isEmpty()) font.setFamily(a.fontFamily);
        font.setPixelSize(a.fontSize);
        QTextDocument document; document.setDocumentMargin(0); document.setDefaultFont(font);
        document.setPlainText(a.text); document.setTextWidth(rect.width());
        QTextCursor cursor(&document); cursor.select(QTextCursor::Document);
        QTextCharFormat format; format.setForeground(a.color); cursor.mergeCharFormat(format);
        p.translate(rect.topLeft()); p.setClipRect(QRectF(QPointF(0,0),rect.size()));
        document.drawContents(&p);
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
