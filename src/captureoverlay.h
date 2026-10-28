#pragma once
#include "core/annotation.h"
#include "platform/platform.h"
#include <QTimer>
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
    void finished();protected:
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
    // 样式设置的持久化：滑块拖动时每次变化都写盘会明显卡顿，改为去抖后统一写入。
    void saveStyleSettings();
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
    QRectF m_cacheSelection;    // 已缓存底图对应的选区，变化时才重新裁剪
    QVector<ScreenCache> m_caches;  // 跨屏时每块参与的屏幕一份缓存
    int m_cropCount = 0;        // 底图重新裁剪的次数，仅供测试观察
    int m_persistedWidth = 0;   // 最近一次落盘的粗细值，仅供测试观察
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
    QTimer m_settingsSaveTimer;   // 去抖：滑块停止变化后才落盘
};
}
