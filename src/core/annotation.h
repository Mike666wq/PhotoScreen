#pragma once
#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QUndoStack>

#include "screen.h"

class QTextDocument;

namespace photo {
enum class Tool { Select, Rectangle, Ellipse, Arrow, Pen, Text, Mosaic };
using AnnotationId = quint64;
struct ToolStyle {
    QColor color = QColor(QStringLiteral("#ff3b3b"));
    int width = 3;
    int fontSize = 18;
    int fontWeight = 400;
};
QString toolSettingsKey(Tool tool);
struct Annotation {
    AnnotationId id = 0;
    Tool tool = Tool::Rectangle;
    QRectF rect;
    QVector<QPointF> points;
    QColor color = Qt::red;
    qreal width = 3;
    int fontSize = 18;
    int fontWeight = 400;
    QString fontFamily;
    QString text;
    QRectF bounds() const;
    void translate(const QPointF &delta);
    void setBounds(const QRectF &bounds);
};
class AnnotationDocument {
public:
    const QVector<Annotation> &items() const { return m_items; }
    AnnotationId add(const Annotation &item);
    int indexOf(AnnotationId id) const;
    void replace(int index, const Annotation &item);
    void remove(int index);
    QUndoStack *undoStack() { return &m_undo; }
    // 剩余可撤销的步数。已撤销的命令不计入，因此撤销一次该值就减一。
    int undoDepth() const { return m_undo.index(); }
    void setItemsForCommand(const QVector<Annotation> &items) { m_items = items; }
    // 样式编辑会话：会话期间的改动不立即入撤销栈，endEdit() 时合并为一条命令。
    // 连续拖动滑块因此只产生一步撤销，且会话不会泄漏到撤销栈状态里。
    void beginEdit();
    void endEdit();
    void cancelEdit();
    bool editing() const { return m_editing; }
    void setItemInPlace(int index, const Annotation &item);
private:
    QVector<Annotation> m_items;
    QUndoStack m_undo;
    bool m_editing = false;
    QVector<Annotation> m_editBase;
    AnnotationId m_nextId = 1;
};
void drawAnnotation(QPainter &painter, const Annotation &annotation);
void configureTextDocument(QTextDocument &document, const Annotation &annotation);
QSizeF textLayoutSize(const Annotation &annotation);
QPainterPath arrowPath(const Annotation &annotation);
bool annotationContains(const Annotation &annotation, const QPointF &point, qreal tolerance = 5);
struct SelectionMetrics {
    QSize pixelSize;
    qreal scaleX = 1;
    qreal scaleY = 1;
    int screenCount = 0;
};
SelectionMetrics selectionMetrics(const QVector<ScreenImage> &screens, const QRectF &selection);
QImage renderSelection(const QImage &source, const QSizeF &logicalSize,
                       const QRectF &selection, const QVector<Annotation> &annotations);
// 跨屏渲染：选区可以横跨多块屏幕，输出比例取这些屏幕中的最大值，
// 保证任何一屏都不被降采样。选区只落在一块屏幕上时与上面的单屏实现逐像素一致。
QImage renderSelection(const QVector<ScreenImage> &screens, const QRectF &selection,
                       const QVector<Annotation> &annotations);
// 标注图层：把标注画到 out 上（out 可复用，避免每帧重新分配）。
// base 是裁剪后的底图，马赛克从 base 取样，因此底图与标注可以分层缓存。
// crop 是选区在该屏源图中的像素矩形，scaleX/scaleY 是该屏像素与逻辑单位的比例。
// origin 是该屏的逻辑原点：标注坐标必须与 origin 处在同一坐标空间，
// 换算到该屏像素时原点与比例都要参与，否则原点不为 0 或比例不为 1 的屏会整块错位。
void renderAnnotationLayer(QImage &out, const QImage &base, const QRect &crop, const QPoint &origin,
                           qreal scaleX, qreal scaleY, const QVector<Annotation> &annotations);
}
