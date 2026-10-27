#pragma once
#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QUndoStack>

class QTextDocument;

namespace photo {
enum class Tool { Select, Rectangle, Ellipse, Arrow, Pen, Text, Mosaic };
struct Annotation {
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
    void add(const Annotation &item);
    void replace(int index, const Annotation &item);
    void remove(int index);
    QUndoStack *undoStack() { return &m_undo; }
    int undoDepth() const { return m_undo.count(); }
    void setItemsForCommand(const QVector<Annotation> &items) { m_items = items; }
    // 样式编辑会话：会话期间的改动不立即入撤销栈，endEdit() 时合并为一条命令。
    // 连续拖动滑块因此只产生一步撤销，且会话不会泄漏到撤销栈状态里。
    void beginEdit();
    void endEdit();
    bool editing() const { return m_editing; }
    void setItemInPlace(int index, const Annotation &item);
private:
    QVector<Annotation> m_items;
    QUndoStack m_undo;
    bool m_editing = false;
    QVector<Annotation> m_editBase;
};
void drawAnnotation(QPainter &painter, const Annotation &annotation);
void configureTextDocument(QTextDocument &document, const Annotation &annotation);
QPainterPath arrowPath(const Annotation &annotation);
bool annotationContains(const Annotation &annotation, const QPointF &point, qreal tolerance = 5);
QImage renderSelection(const QImage &source, const QSizeF &logicalSize,
                       const QRectF &selection, const QVector<Annotation> &annotations);
}
