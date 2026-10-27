#pragma once
#include "core/annotation.h"
#include "platform/platform.h"
#include <QWidget>
#include <functional>

class QButtonGroup;
class QFrame;
class QLabel;
class QSlider;
class QTextEdit;
class QToolButton;
class QScreen;

namespace photo {
class CaptureOverlay final : public QWidget {
    Q_OBJECT
public:
    using ClipboardWriter = std::function<bool(const QImage &, QString *)>;
    CaptureOverlay(ScreenFrame frame, QScreen *screen, ClipboardWriter writer = {});
    // 仅供测试观察撤销栈与标注状态。
    int undoDepth() const { return m_document.undoDepth(); }
    bool styleEditActive() const { return m_document.editing(); }
    const QVector<Annotation> &annotations() const { return m_document.items(); }
    // 应用一次“用户选定颜色”的动作。颜色面板与自动化测试共用同一条路径。
    void applyChosenColor(const QColor &color);
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
    void buildStylePanel();
    void buildColorPanel();
    void toggleStylePanel();
    void toggleColorPanel();
    void hidePanels();
    bool panelVisible() const;
    void positionPanel(QFrame *panel, const QPoint &anchor, bool preferAbove);
    QColor previewColor() const;
    void updateColorPreview();
    QVector<QColor> recentColors() const;
    void rememberColor(const QColor &color);
    void updateRecentButtons();
    void updatePresetHighlight();
    void applyRecentColor(int index);
    void updateColorSwatch();
    void positionToolbar();
    void setTool(Tool tool);
    void applyStyleChange(int index, const Annotation &item);
    void commitStyleEdit();
    void startText(const QPointF &point, int existing = -1);
    void applyEditorTextStyle();
    void resizeTextEditor();
    void resizeTextAnnotation(Annotation &annotation) const;
    void commitText();
    void finishCapture();
    void cancelCapture();
    void clearSelection();
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
    QColor m_textColor = Qt::red;   // 正在编辑的文字颜色，与工具栏当前色解耦
    int m_lineWidth = 3;
    int m_fontSize = 18;
    int m_fontWeight = 400;
    QFrame *m_toolbar = nullptr;
    QFrame *m_stylePanel = nullptr;
    QFrame *m_colorPanel = nullptr;
    QSlider *m_hueControl = nullptr;
    QSlider *m_valueControl = nullptr;
    QToolButton *m_colorPreview = nullptr;
    QToolButton *m_colorButton = nullptr;
    QToolButton *m_undoButton = nullptr;
    QToolButton *m_redoButton = nullptr;
    QSlider *m_widthControl = nullptr;
    QSlider *m_fontControl = nullptr;
    QSlider *m_fontWeightControl = nullptr;
    QLabel *m_widthValue = nullptr;
    QLabel *m_fontValue = nullptr;
    QLabel *m_fontWeightValue = nullptr;
    QToolButton *m_widthButton = nullptr;
    QToolButton *m_fontButton = nullptr;
    QTextEdit *m_textEditor = nullptr;
    QPointF m_textAnchor;
    int m_editingText = -1;
};
}
