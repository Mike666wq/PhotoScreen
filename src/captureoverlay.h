#pragma once
#include "core/annotation.h"
#include "platform/platform.h"
#include <QWidget>
#include <functional>

class QButtonGroup;
class QFrame;
class QSpinBox;
class QTextEdit;
class QToolButton;
class QScreen;

namespace photo {
class CaptureOverlay final : public QWidget {
    Q_OBJECT
public:
    using ClipboardWriter = std::function<bool(const QImage &, QString *)>;
    CaptureOverlay(ScreenFrame frame, QScreen *screen, ClipboardWriter writer = {});
signals:
    void finished();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    enum class Drag { None, NewSelection, MoveSelection, ResizeSelection, Draw, MoveObject, ResizeObject, ArrowStart, ArrowEnd };
    void buildToolbar();
    void updateColorSwatch();
    void positionToolbar();
    void setTool(Tool tool);
    void startText(const QPointF &point, int existing = -1);
    void resizeTextEditor();
    void commitText();
    void finishCapture();
    void cancelCapture();
    void resetDocument();
    void updateDrag(const QPointF &point);
    QPointF boundedPoint(const QPointF &p) const;
    QRectF windowAt(const QPointF &p) const;
    int objectAt(const QPointF &p) const;
    int selectionHandleAt(const QPointF &p) const;
    QRectF resizeRect(const QRectF &start, int handle, const QPointF &point) const;
    QVector<Annotation> previewItems() const;
    void showError(const QString &message);

    ScreenFrame m_frame;
    ClipboardWriter m_clipboardWriter;
    QScreen *m_screen;
    AnnotationDocument m_document;
    QRectF m_selection;
    QRectF m_hoverWindow;
    QPointF m_pointer, m_press;
    QRectF m_startRect;
    Drag m_drag = Drag::None;
    int m_handle = -1;
    int m_selected = -1;
    Annotation m_startAnnotation, m_preview;
    bool m_hasPreview = false;
    Tool m_tool = Tool::Select;
    QColor m_color = Qt::red;
    int m_lineWidth = 3;
    int m_fontSize = 18;
    QFrame *m_toolbar = nullptr;
    QToolButton *m_colorButton = nullptr;
    QToolButton *m_undoButton = nullptr;
    QToolButton *m_redoButton = nullptr;
    QSpinBox *m_widthControl = nullptr;
    QSpinBox *m_fontControl = nullptr;
    QTextEdit *m_textEditor = nullptr;
    QPointF m_textAnchor;
    int m_editingText = -1;
};
}
