#pragma once
#include "core/annotation.h"
#include "platform/platform.h"
#include <QTextCursor>
#include <QTimer>
#include <QWidget>
#include <array>
#include <functional>

class QBoxLayout;
class QButtonGroup;
class QGridLayout;
class QFrame;
class QLabel;
class QSlider;
class QTextEdit;
class QToolButton;
class QScreen;

namespace photo {
// 自绘滑块手柄中心的 x 坐标（与 paintEvent 用的是同一套几何）。
// 供测试核对「看到的」与「摸到的」是不是同一个位置：把鼠标按在这个坐标上，
// QSlider 内部算出的值应当等于滑块当前值。
int sliderHandleCenterX(const QSlider *slider);

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
    // 底图重新裁剪次数，仅供测试观察渲染缓存是否生效。
    int cropCount() const { return m_cropCount; }
    // 最近一次落盘的粗细值；0 表示本对象还没落盘过。
    // 供测试观察去抖保存是否真的发生，避免依赖全局 QSettings 状态。
    int persistedLineWidth() const { return m_persistedWidth; }
signals:
    void finished();
protected:
    void closeEvent(QCloseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    enum class Drag { None, NewSelection, MoveSelection, ResizeSelection, Draw, MoveObject, ResizeObject, ArrowStart, ArrowEnd };
    // 单块屏幕的渲染缓存：底图只在选区变化时重新裁剪，标注图层每帧重绘但复用缓冲区。
    // 跨屏选区时每块参与的屏幕各有一份，比例各自独立。
    struct ScreenCache {
        QRectF logicalRect;     // 该屏上与选区相交的部分（覆盖层局部逻辑坐标）
        QPoint origin;          // 该屏在覆盖层局部坐标中的原点
        QRect crop;             // 选区在该屏像素坐标中的矩形
        qreal scaleX = 1.0, scaleY = 1.0;
        QImage base;            // 底图裁剪
        QImage annotations;     // 标注图层
    };
    void buildToolbar();
    void ensureRenderCache();
    void buildStylePanel();
    void buildColorPanel();
    void updateStylePanel();
    QRect uiScreenBounds() const;
    void toggleColorPanel();
    void hidePanels();
    bool panelVisible() const;
    void positionPanel(QFrame *panel, const QPoint &anchor, bool preferAbove);
    QColor previewColor() const;
    void applyColor(const QColor &color, bool commitRecent);
    void syncColorControls();
    Tool styleTool() const;
    void markStyleDirty();
    void loadToolStyle();
    void updateColorPreview();
    QVector<QColor> recentColors() const;
    void rememberColor(const QColor &color);
    void updateRecentButtons();
    void updatePresetHighlight();
    void applyRecentColor(int index);
    void updateColorSwatch();
    void positionToolbar();
    void setTool(Tool tool);
    int selectedIndex() const;
    int editingTextIndex() const;
    void selectObject(int index);
    void syncSelectedStyle();
    void endSession();
    void applyStyleChange(int index, const Annotation &item);
    void commitStyleEdit();
    // 样式设置的持久化：滑块拖动时每次变化都写盘会明显卡顿，改为去抖后统一写入。
    void saveStyleSettings();
    void startText(const QPointF &point, int existing = -1);
    void applyEditorTextStyle();
    Annotation textDraftFromEditor() const;
    void beginEditorStyleEdit();
    void endEditorStyleEdit();
    void syncTextEditorStyle();
    void updateUndoButtons();
    void undoOrRedo(bool redo);
    void resizeTextEditor();
    void resizeTextAnnotation(Annotation &annotation) const;
    void commitText();
    void finishCapture();
    void cancelCapture();
    void clearSelection();
    void updateDrag(const QPointF &point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void updatePointerCursor(const QPointF &point);
    void cancelDrag();
    QPointF boundedPoint(const QPointF &p) const;
    // 覆盖层局部逻辑坐标 → 虚拟桌面逻辑坐标。
    QPointF toVirtual(const QPointF &local) const { return local + m_frame.virtualGeometry.topLeft(); }
    // 标注在虚拟桌面逻辑坐标下的副本，导出图片时要用。
    QVector<Annotation> virtualAnnotations() const;
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
    QRectF m_pressSelection;
    AnnotationId m_pressSelectedId = 0;
    Drag m_drag = Drag::None;
    int m_handle = -1;
    AnnotationId m_selectedId = 0;
    Annotation m_startAnnotation, m_preview;
    bool m_hasPreview = false;
    Tool m_tool = Tool::Select;
    Tool m_lastDrawingTool = Tool::Rectangle;
    std::array<ToolStyle,7> m_toolStyles;
    std::array<bool,7> m_styleDirty{};
    QColor m_color = Qt::red;
    QColor m_textColor = Qt::red;   // 正在编辑的文字颜色，与工具栏当前色解耦
    int m_lineWidth = 3;
    int m_fontSize = 18;
    int m_fontWeight = 400;
    QRectF m_cacheSelection;    // 已缓存底图对应的选区，变化时才重新裁剪
    QVector<ScreenCache> m_caches;  // 跨屏时每块参与的屏幕一份缓存
    int m_cropCount = 0;        // 底图重新裁剪的次数，仅供测试观察
    int m_persistedWidth = 0;   // 最近一次落盘的粗细值，仅供测试观察
    bool m_copying = false;
    bool m_ending = false;
    QFrame *m_errorBanner = nullptr;
    QLabel *m_errorMessage = nullptr;
    QFrame *m_toolbar = nullptr;
    QFrame *m_stylePanel = nullptr;
    QFrame *m_colorPanel = nullptr;
    QSlider *m_hueControl = nullptr;
    QSlider *m_valueControl = nullptr;
    QSlider *m_saturationControl = nullptr;
    QLabel *m_hueValue = nullptr;
    QLabel *m_saturationValue = nullptr;
    QLabel *m_valueValue = nullptr;
    QFrame *m_colorAdvanced = nullptr;
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
    QBoxLayout *m_primaryLayout = nullptr;
    QGridLayout *m_toolLayout = nullptr;
    QWidget *m_toolArea = nullptr;
    QVector<QToolButton *> m_toolButtons;
    QLabel *m_toolTitle = nullptr;
    bool m_parametersExpanded = false;
    QPointF m_toolbarAnchor;
    QTextEdit *m_textEditor = nullptr;
    QPointF m_textAnchor;
    Annotation m_textDraft;
    QTextCursor m_editorStyleCursor;
    bool m_editorStyleEditing = false;
    bool m_applyingTextStyle = false;
    bool m_resizingText = false;
    bool m_preeditActive = false;
    QLabel *m_textOverflowHint = nullptr;
    AnnotationId m_editingTextId = 0;
    QTimer m_settingsSaveTimer;   // 去抖：滑块停止变化后才落盘
};
}
