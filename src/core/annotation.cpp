#include "annotation.h"
#include "geometry.h"
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPainterPathStroker>
#include <QApplication>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUndoCommand>
#include <QtMath>
#include <algorithm>

namespace photo {
QString toolSettingsKey(Tool tool) {
    switch (tool) {
    case Tool::Select: return QStringLiteral("select");
    case Tool::Rectangle: return QStringLiteral("rectangle");
    case Tool::Ellipse: return QStringLiteral("ellipse");
    case Tool::Arrow: return QStringLiteral("arrow");
    case Tool::Pen: return QStringLiteral("pen");
    case Tool::Text: return QStringLiteral("text");
    case Tool::Mosaic: return QStringLiteral("mosaic");
    }
    return {};
}
namespace {
// 马赛克：把区域缩小成色块再放大回去，块大小随输出比例变化。
// 取样来自 source（而不是 out），这样标注层可以和底图分层缓存。
void drawMosaic(QImage &out, const QImage &source, const QRect &area, qreal blockScale) {
    if (area.isEmpty()) return;
    const QImage piece = source.copy(area);
    const int block = qMax(6, qRound(12 * blockScale));
    const QImage small = piece.scaled(qMax(1, area.width()/block), qMax(1, area.height()/block), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QPainter p(&out); p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(area, small.scaled(area.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
}
}
// 笔迹点的精确极值范围，不含 epsilon，避免包围盒与缩放归一化之间产生微小偏移。
static QRectF exactPointsBox(const QVector<QPointF> &points) {
    if (points.isEmpty()) return {};
    qreal minX=points.first().x(), maxX=minX, minY=points.first().y(), maxY=minY;
    for (const auto &point : points) {
        minX=qMin(minX,point.x()); maxX=qMax(maxX,point.x());
        minY=qMin(minY,point.y()); maxY=qMax(maxY,point.y());
    }
    return QRectF(QPointF(minX,minY), QPointF(maxX,maxY));
}
static bool sameAnnotation(const Annotation &a, const Annotation &b) {
    return a.id==b.id && a.tool==b.tool && a.rect==b.rect && a.points==b.points && a.color==b.color
        && qFuzzyCompare(a.width+1,b.width+1) && a.fontSize==b.fontSize && a.fontWeight==b.fontWeight
        && a.fontFamily==b.fontFamily && a.text==b.text;
}
static bool sameItems(const QVector<Annotation> &a, const QVector<Annotation> &b) {
    if (a.size()!=b.size()) return false;
    for (int i=0;i<a.size();++i) if (!sameAnnotation(a[i],b[i])) return false;
    return true;
}
QRectF Annotation::bounds() const {
    if (tool == Tool::Arrow) return arrowPath(*this).boundingRect();
    if (tool == Tool::Pen && !points.isEmpty()) {
        return exactPointsBox(points).adjusted(-width, -width, width, width);
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
    if (a.tool==Tool::Mosaic) {
        // 马赛克覆盖整个矩形区域，区域内任意位置都应命中。
        return a.rect.normalized().adjusted(-tolerance,-tolerance,tolerance,tolerance).contains(point);
    }
    if (a.tool==Tool::Pen) {
        if (a.points.isEmpty()) return false;
        if (a.points.size()==1) {
            // 单点笔迹是一个圆点，命中半径为线宽一半加容差。
            const qreal radius=qMax<qreal>(1,a.width)/2+tolerance;
            const QPointF center=a.points.first();
            return QRectF(center.x()-radius,center.y()-radius,radius*2,radius*2).contains(point);
        }
        QPainterPath path;
        path.moveTo(a.points.first());
        for (qsizetype i=1;i<a.points.size();++i) path.lineTo(a.points[i]);
        QPainterPathStroker stroker; stroker.setWidth(qMax<qreal>(1,a.width)+2*tolerance);
        stroker.setCapStyle(Qt::RoundCap); stroker.setJoinStyle(Qt::RoundJoin);
        return stroker.createStroke(path).contains(point);
    }
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
    if (tool==Tool::Pen && !points.isEmpty()) {
        // bounds() 含线宽外扩，而 points 不含；按原始包围盒归一化，避免缩放后整体偏移一个线宽。
        const QRectF raw = exactPointsBox(points);
        const QRectF inner = target.normalized().adjusted(width, width, -width, -width);
        if (points.size()==1) {
            points[0] = inner.center();
            rect = target;
            return;
        }
        // 水平或垂直笔迹的包围盒有一边为 0，属正常情况，只在对应轴上不缩放。
        const qreal sx = raw.width()  > 0 ? inner.width() /raw.width()  : 1;
        const qreal sy = raw.height() > 0 ? inner.height()/raw.height() : 1;
        for (auto &p : points)
            p = QPointF(inner.left() + (p.x()-raw.left())*sx, inner.top() + (p.y()-raw.top())*sy);
        rect = target;
        return;
    }
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
    void undo() override { m_doc->endEdit(); m_doc->setItemsForCommand(m_before); }
    void redo() override { m_doc->endEdit(); m_doc->setItemsForCommand(m_after); }
private:
    AnnotationDocument *m_doc;
    QVector<Annotation> m_before, m_after;
};
int AnnotationDocument::indexOf(AnnotationId id) const {
    if (!id) return -1;
    for (qsizetype i=0; i<m_items.size(); ++i) if (m_items[i].id==id) return int(i);
    return -1;
}
AnnotationId AnnotationDocument::add(const Annotation &item) {
    endEdit();
    Annotation added=item;
    added.id=m_nextId++;
    auto after = m_items; after.append(added); m_undo.push(new Change(this, m_items, after));
    return added.id;
}
void AnnotationDocument::replace(int index, const Annotation &item) {
    if (index < 0 || index >= m_items.size()) return;
    endEdit();
    Annotation replacement=item;
    replacement.id=m_items[index].id;
    if (sameAnnotation(m_items[index],replacement)) return;
    auto after = m_items; after[index] = replacement; m_undo.push(new Change(this, m_items, after));
}
void AnnotationDocument::remove(int index) {
    if (index < 0 || index >= m_items.size()) return;
    endEdit();
    auto after = m_items; after.removeAt(index); m_undo.push(new Change(this, m_items, after));
}
void AnnotationDocument::beginEdit() {
    if (m_editing) return;
    m_editing = true;
    m_editBase = m_items;
}
void AnnotationDocument::endEdit() {
    if (!m_editing) return;
    m_editing = false;
    const QVector<Annotation> base = m_editBase;
    m_editBase.clear();
    if (sameItems(base, m_items)) return;
    m_undo.push(new Change(this, base, m_items));
}
void AnnotationDocument::setItemInPlace(int index, const Annotation &item) {
    if (index < 0 || index >= m_items.size()) return;
    const AnnotationId id=m_items[index].id;
    m_items[index] = item;
    m_items[index].id=id;
}
void AnnotationDocument::cancelEdit() {
    if (!m_editing) return;
    m_items=std::move(m_editBase);
    m_editBase.clear();
    m_editing=false;
}
void configureTextDocument(QTextDocument &document, const Annotation &a) {
    QFont font=QApplication::font();
    if (!a.fontFamily.isEmpty()) font.setFamily(a.fontFamily);
    font.setPixelSize(a.fontSize);
    font.setWeight(static_cast<QFont::Weight>(a.fontWeight));
    document.setDocumentMargin(0);
    document.setDefaultFont(font);
    document.setPlainText(a.text);
    document.setTextWidth(a.rect.normalized().width());
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setFont(font);
    format.setForeground(a.color);
    cursor.mergeCharFormat(format);
}
QSizeF textLayoutSize(const Annotation &annotation) {
    QTextDocument document;
    configureTextDocument(document,annotation);
    return QSizeF(annotation.rect.normalized().width(),qMax(24,qCeil(document.documentLayout()->documentSize().height())+2));
}
void drawAnnotation(QPainter &p, const Annotation &a) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // 按工具区分笔刷：矩形必须是直角，画笔保留圆头圆角，箭头为填充路径不受笔刷影响。
    Qt::PenCapStyle cap = Qt::RoundCap;
    Qt::PenJoinStyle join = Qt::RoundJoin;
    if (a.tool==Tool::Rectangle) { cap = Qt::SquareCap; join = Qt::MiterJoin; }
    else if (a.tool==Tool::Ellipse) { cap = Qt::FlatCap; join = Qt::MiterJoin; }
    p.setPen(QPen(a.color, a.width, Qt::SolidLine, cap, join));
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
        QTextDocument document;
        configureTextDocument(document,a);
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
            drawMosaic(out, out, area, qMin(sx, sy));
        } else {
            QPainter p(&out);
            p.translate(-crop.x(), -crop.y()); p.scale(sx, sy);
            drawAnnotation(p, a);
        }
    }
    return out;
}
SelectionMetrics selectionMetrics(const QVector<ScreenImage> &screens, const QRectF &selection) {
    SelectionMetrics metrics;
    const ScreenImage *single=nullptr;
    qreal sx=0,sy=0;
    const QRectF sel=selection.normalized();
    for (const auto &screen : screens) {
        if (screen.isEmpty() || !QRectF(screen.geometry).intersects(sel)) continue;
        ++metrics.screenCount; single=&screen;
        sx=qMax(sx,screen.scaleX()); sy=qMax(sy,screen.scaleY());
    }
    if (!single) return metrics;
    metrics.scaleX=sx; metrics.scaleY=sy;
    if (metrics.screenCount==1) {
        metrics.pixelSize=pixelRect(sel.translated(-single->geometry.topLeft()),single->geometry.size(),single->image.size()).size();
    } else {
        metrics.pixelSize=QSize(qCeil(sel.right()*sx)-qFloor(sel.left()*sx),
                               qCeil(sel.bottom()*sy)-qFloor(sel.top()*sy));
    }
    return metrics;
}
QImage renderSelection(const QVector<ScreenImage> &screens, const QRectF &selection,
                       const QVector<Annotation> &annotations) {
    const QRectF sel = selection.normalized();
    if (sel.isEmpty()) return {};
    QVector<const ScreenImage*> hits;
    for (const auto &screen : screens) if (!screen.isEmpty() && QRectF(screen.geometry).intersects(sel)) hits.append(&screen);
    if (hits.isEmpty()) return {};

    // 输出比例取相交屏幕中的最大值，保证任何一屏都不被降采样。
    const SelectionMetrics metrics=selectionMetrics(screens,sel);
    const qreal sx=metrics.scaleX, sy=metrics.scaleY;
    if (sx <= 0 || sy <= 0) return {};

    // 选区在输出比例下的像素范围，取整规则与单屏实现一致。
    const int left = qFloor(sel.left()*sx), top = qFloor(sel.top()*sy);
    const QSize outSize=metrics.pixelSize;
    if (outSize.isEmpty()) return {};

    if (hits.size() == 1) {
        // 只落在一块屏上：直接复制像素，与单屏实现逐像素一致。
        const ScreenImage *s = hits.first();
        // 选区是虚拟桌面绝对坐标，先平移到该屏的局部坐标。
        const QRectF local = sel.translated(-s->geometry.topLeft());
        const QRect crop = pixelRect(local, s->geometry.size(), s->image.size());
        if (crop.isEmpty()) return {};
        QImage out = s->image.copy(crop).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const qreal csx = s->image.width() / s->geometry.width();
        const qreal csy = s->image.height() / s->geometry.height();
        for (const auto &a : annotations) {
            if (a.tool == Tool::Mosaic) {
                const QRect area = pixelRect(a.rect.translated(-s->geometry.topLeft()), s->geometry.size(), s->image.size())
                                       .intersected(crop).translated(-crop.topLeft());
                drawMosaic(out, out, area, qMin(csx, csy));
            } else {
                QPainter p(&out);
                // 标注是虚拟桌面逻辑坐标，先换算成该屏的像素坐标：
                // (逻辑坐标 - 屏幕原点) * 比例 - 裁剪原点。
                p.translate(-s->geometry.x()*csx-crop.x(), -s->geometry.y()*csy-crop.y());
                p.scale(csx, csy);
                drawAnnotation(p, a);
            }
        }
        return out;
    }

    // 跨屏：先把每块屏合成到输出图，再叠加标注。
    QImage out(outSize, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::black);   // 屏幕之间的空隙用黑色填充，输出保持不透明
    for (const auto *s : hits) {
        // 该屏在输出像素中的起点取整，避免拼接处出现半像素错位。
        const int dx = qRound(s->geometry.left()*sx) - left;
        const int dy = qRound(s->geometry.top()*sy) - top;
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.translate(dx, dy);
        p.scale(sx / s->scaleX(), sy / s->scaleY());
        p.drawImage(QPointF(0, 0), s->image);
    }
    for (const auto &a : annotations) {
        if (a.tool == Tool::Mosaic) {
            const QRectF area = a.rect.normalized().intersected(sel);
            const QRect pixels(qFloor(area.left()*sx)-left, qFloor(area.top()*sy)-top,
                               qCeil(area.right()*sx)-qFloor(area.left()*sx),
                               qCeil(area.bottom()*sy)-qFloor(area.top()*sy));
            drawMosaic(out, out, pixels.intersected(out.rect()), qMin(sx, sy));
        } else {
            QPainter p(&out);
            p.translate(-left, -top); p.scale(sx, sy);
            drawAnnotation(p, a);
        }
    }
    return out;
}

void renderAnnotationLayer(QImage &out, const QImage &base, const QRect &crop, const QPoint &origin,
                           qreal scaleX, qreal scaleY, const QVector<Annotation> &annotations) {
    if (out.isNull() || base.isNull()) return;
    for (const auto &a : annotations) {
        if (a.tool == Tool::Mosaic) {
            // 马赛克区域先换算成源图像素，再裁到选区内，最后平移到底图坐标。
            const QRectF rect=a.rect.normalized().translated(-origin);
            const QRect pixels(qFloor(rect.left()*scaleX), qFloor(rect.top()*scaleY),
                               qCeil(rect.right()*scaleX)-qFloor(rect.left()*scaleX),
                               qCeil(rect.bottom()*scaleY)-qFloor(rect.top()*scaleY));
            drawMosaic(out, base, pixels.intersected(crop).translated(-crop.topLeft()), qMin(scaleX, scaleY));
        } else {
            // 平移量要在缩放之前算好：逻辑坐标 → 屏内像素 = (逻辑坐标 - 原点) * 比例 - 裁剪原点。
            QPainter p(&out);
            p.translate(-origin.x()*scaleX-crop.x(), -origin.y()*scaleY-crop.y());
            p.scale(scaleX, scaleY);
            drawAnnotation(p, a);
        }
    }
}
}
