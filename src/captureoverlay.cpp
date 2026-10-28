#include "captureoverlay.h"
#include "core/geometry.h"
#include "core/interaction.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QInputMethod>
#include <QLabel>
#include <QLinearGradient>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSettings>
#include <QSlider>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <memory>

namespace photo {
namespace {
QRectF normalizedAt(const QPointF &a, const QPointF &b) { return QRectF(a, b).normalized(); }
QRectF handleRect(const QPointF &p) { return QRectF(p.x()-5, p.y()-5, 10, 10); }
QVector<QPointF> handles(const QRectF &r) {
    return {r.topLeft(), QPointF(r.center().x(),r.top()), r.topRight(),
            QPointF(r.right(),r.center().y()), r.bottomRight(), QPointF(r.center().x(),r.bottom()),
            r.bottomLeft(), QPointF(r.left(),r.center().y())};
}
// 自绘滑块：轨道、已选部分、刻度、手柄全部显式绘制，不依赖平台主题，
// macOS 与 Windows 表现一致。轨道是 4px 圆角矩形，手柄是 14px 圆形。
// 关键点：轨道、手柄、命中、取值全部用 geometry() 这一套几何，
// 视觉与手感必然一致，不会出现「看到的」和「摸到的」不是同一个位置。
class StyledSlider : public QSlider {
public:
    explicit StyledSlider(QWidget *parent=nullptr) : QSlider(Qt::Horizontal,parent) { setMinimumHeight(26); }
    // 轨道填充渐变（色相条、明度条用）；设置后不再画“已选部分”填充。
    void setGrooveGradient(const QLinearGradient &gradient) { m_gradient=gradient; m_hasGradient=true; update(); }
    // 手柄中心 x 坐标：与 paintEvent 同一套计算，供测试核对命中位置。
    int handleCenterX() const { return geometry().handleCenter; }
protected:
    struct SliderGeometry {
        QRect groove;        // 轨道在控件内的水平范围
        int handleLength;    // 手柄直径，与 QSlider 命中判定用的是同一个值
        int handleCenter;    // 手柄中心的 x 坐标
        int handleRadius;
    };
    SliderGeometry geometry() const {
        const int radius=7;
        const int span=qMax(1,width()-1-2*radius);
        const QRect groove(radius,0,span+1,1);
        const int position=QStyle::sliderPositionFromValue(minimum(),maximum(),value(),span);
        return {groove,2*radius,groove.left()+position,radius};
    }
    int valueAtX(int x) const {
        const SliderGeometry geo=geometry();
        const int span=qMax(1,geo.groove.width()-1);
        const qreal position=qBound(0.0,qreal(x-geo.groove.left())/span,1.0);
        return minimum()+qRound(position*(maximum()-minimum()));
    }
    // 鼠标交互自己算值：QSlider 默认按下时会跳到点击位置，而绘制、命中、取值
    // 分别用各自的坐标，换算不一致时就会「按住了却自己动了」。
    // 这里三处统一用 geometry()，点手柄不改值，点轨道跳到该处，拖动跟手。
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button()!=Qt::LeftButton) { QSlider::mousePressEvent(event); return; }
        m_dragging=true;
        // 先发 sliderPressed 再改值：业务在按下时开启编辑会话，
        // 值变化落在会话内，拖一次只产生一步撤销。
        setFocus(Qt::MouseFocusReason);
        setSliderDown(true);
        const SliderGeometry geo=geometry();
        const bool onHandle=qAbs(event->position().x()-geo.handleCenter)<=geo.handleRadius;
        m_grabOffset=onHandle ? qRound(event->position().x())-geo.handleCenter : 0;
        if (!onHandle) setValue(valueAtX(qRound(event->position().x())));
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!m_dragging) { QSlider::mouseMoveEvent(event); return; }
        setValue(valueAtX(qRound(event->position().x())-m_grabOffset));
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (m_dragging && event->button()==Qt::LeftButton) {
            setValue(valueAtX(qRound(event->position().x())-m_grabOffset));
            m_dragging=false;
            setSliderDown(false);
            event->accept(); return;
        }
        QSlider::mouseReleaseEvent(event);
    }
    bool event(QEvent *event) override {
        if (m_dragging && (event->type()==QEvent::UngrabMouse || event->type()==QEvent::Hide || event->type()==QEvent::WindowDeactivate)) {
            m_dragging=false;
            setSliderDown(false);
        }
        return QSlider::event(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        // 先清空，避免上一次绘制的轨道与手柄残留。
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(),Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setRenderHint(QPainter::Antialiasing);
        const SliderGeometry geo=geometry();
        const int grooveHeight=4;
        const QRect groove(geo.groove.left(),(height()-grooveHeight)/2,geo.groove.width(),grooveHeight);
        const int span=qMax(1,groove.width()-1);
        // 轨道
        p.setPen(QPen(QColor("#9aa0a6"),1));
        p.setBrush(m_hasGradient ? QBrush(m_gradient) : QBrush(QColor("#dfe1e5")));
        p.drawRoundedRect(groove,2,2);
        // 已选部分：到当前手柄为止
        const int selected=geo.handleCenter-groove.left();
        if (!m_hasGradient && selected>0) {
            p.setPen(Qt::NoPen); p.setBrush(QColor("#1687ef"));
            p.drawRoundedRect(QRect(groove.left(),groove.top(),selected,groove.height()),2,2);
        }
        // 刻度
        if (tickPosition()!=NoTicks) {
            const int step=tickInterval()>0?tickInterval():qMax(1,(maximum()-minimum())/10);
            p.setPen(QPen(QColor("#8a8f96"),1));
            for (int v=minimum(); v<=maximum(); v+=step) {
                const int x=groove.left()+QStyle::sliderPositionFromValue(minimum(),maximum(),v,span,0);
                p.drawLine(x,groove.bottom()+4,x,groove.bottom()+8);
            }
        }
        // 手柄
        p.setPen(QPen(QColor("#5f6368"),1)); p.setBrush(QColor("#ffffff"));
        p.drawEllipse(QPoint(geo.handleCenter,groove.center().y()),geo.handleRadius,geo.handleRadius);
    }
private:
    QLinearGradient m_gradient;
    bool m_hasGradient=false;
    int m_grabOffset=0;
    bool m_dragging=false;   // 自己接管鼠标拖动，避免与 QSlider 内部状态打架
};
// 参数面板里的一行：名称 + 自绘滑块 + 紧贴滑块右端的数值。
StyledSlider *addStyledSlider(QHBoxLayout *row, const QString &title, int minimum, int maximum, int value, QLabel **valueLabel) {
    auto *name=new QLabel(title);
    auto *slider=new StyledSlider; slider->setRange(minimum,maximum);
    slider->setValue(value); slider->setAccessibleName(title); slider->setMinimumWidth(150);
    auto *number=new QLabel(QString::number(value));
    number->setAlignment(Qt::AlignRight | Qt::AlignVCenter); number->setMinimumWidth(30);
    row->addWidget(name); row->addWidget(slider,1); row->addWidget(number);
    *valueLabel=number;
    QObject::connect(slider,&QSlider::valueChanged,number,[number](int n) { number->setText(QString::number(n)); });
    return slider;
}
// 给自绘滑块设置轨道渐变。成员指针声明为 QSlider*，这里用 dynamic_cast 取回具体类型。
void setSliderGradient(QSlider *slider, const QLinearGradient &gradient) {
    if (auto *styled=dynamic_cast<StyledSlider *>(slider)) styled->setGrooveGradient(gradient);
}
// 预设色板：8 个色相 × 3 个深浅，按列排布。
QVector<QColor> presetColors() {
    static const char *names[] = {
        "#ff3b3b","#ff6b6b","#c62828",
        "#ff9500","#ffb340","#c76b00",
        "#ffcc00","#ffdd66","#b38f00",
        "#34c759","#6ee08a","#1f8f3c",
        "#00c7be","#5be0d8","#008f88",
        "#0a84ff","#66b3ff","#0060cc",
        "#bf5af2","#d79bff","#8a2fc4",
        "#ff2d55","#ff7a95","#cc0f36",
        "#000000","#ffffff","#808080","#404040"
    };
    QVector<QColor> result;
    for (const char *name : names) result.append(QColor(QString::fromLatin1(name)));
    return result;
}
// 设置里的颜色一律以十六进制字符串存放。
// Windows 注册表后端与 macOS plist 后端对 QColor / QStringList 的类型保留方式不同：
// 注册表会把它们降级成普通字符串，读回来时 QVariant 的实际类型与 plist 不一致，
// 导致同一份代码在两个平台上的表现不同。显式存字符串可保证双平台读写一致。
QColor colorFromSettings(const QVariant &value, const QColor &fallback) {
    // 新格式：十六进制字符串。
    const QColor parsed=QColor(value.toString().trimmed());
    if (parsed.isValid()) return parsed;
    // 旧格式：直接存的 QColor。
    const QColor legacy=value.value<QColor>();
    return legacy.isValid() ? legacy : fallback;
}
// 最近使用的颜色列表：兼容旧的 QStringList 格式与新的逗号分隔字符串。
QStringList colorNamesFromSettings(const QVariant &value) {
    QStringList names;
    // 新格式：逗号分隔的字符串。
    const QStringList parts=value.toString().split(',',Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString name=part.trimmed();
        if (!name.isEmpty()) names.append(name);
    }
    if (!names.isEmpty()) return names;
    // 旧格式：QStringList。
    const QStringList legacy=value.toStringList();
    for (const QString &name : legacy) {
        const QString trimmed=name.trimmed();
        if (!trimmed.isEmpty()) names.append(trimmed);
    }
    return names;
}
QIcon toolIcon(Tool tool) {
    QPixmap pixmap(24,24); pixmap.fill(Qt::transparent);
    QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#303640"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (tool) {
    case Tool::Select: {
        QPolygonF cursor; cursor<<QPointF(5,3)<<QPointF(18,13)<<QPointF(12,14)<<QPointF(10,20)<<QPointF(5,3);
        p.drawPolyline(cursor); break;
    }
    case Tool::Rectangle: p.drawRect(QRectF(3,5,18,14)); break;
    case Tool::Ellipse: p.drawEllipse(QRectF(3,5,18,14)); break;
    case Tool::Arrow: {
        Annotation a; a.tool=Tool::Arrow; a.rect=QRectF(QPointF(3,20),QPointF(21,4)); a.width=8; a.color=QColor("#303640");
        drawAnnotation(p,a); break;
    }
    case Tool::Pen: p.drawLine(QPointF(5,19),QPointF(18,5)); p.drawLine(QPointF(4,20),QPointF(8,19)); break;
    case Tool::Text: { QFont f=p.font(); f.setBold(true); f.setPixelSize(18); p.setFont(f); p.drawText(pixmap.rect(),Qt::AlignCenter,QStringLiteral("T")); break; }
    case Tool::Mosaic:
        p.setPen(Qt::NoPen); p.setBrush(QColor("#303640"));
        for (int y=0;y<3;++y) for(int x=0;x<3;++x) if ((x+y)%2==0) p.drawRect(QRectF(4+x*6,4+y*6,5,5));
        break;
    }
    return QIcon(pixmap);
}
}
// 自绘滑块的手柄中心 x 坐标；不是自绘滑块时返回 -1。
// 放在匿名 namespace 之外：测试目标需要链接到这个符号（内部链接会链不上）。
int sliderHandleCenterX(const QSlider *slider) {
    if (!slider) return -1;
    if (auto *styled=dynamic_cast<const StyledSlider *>(slider)) return styled->handleCenterX();
    return -1;
}
CaptureOverlay::CaptureOverlay(ScreenFrame frame, QScreen *screen, ClipboardWriter writer)
    : QWidget(nullptr), m_frame(std::move(frame)), m_clipboardWriter(writer ? std::move(writer) : ClipboardWriter(writeClipboard)), m_screen(screen) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // 覆盖整个虚拟桌面，选区因此可以横跨多块屏幕。
    if (m_frame.virtualGeometry.isEmpty()) m_frame.virtualGeometry=virtualGeometry(m_frame.screens);
    if (m_frame.virtualGeometry.isEmpty() && screen) m_frame.virtualGeometry=screen->geometry();
    setGeometry(m_frame.virtualGeometry);
    setCursor(Qt::CrossCursor);
    QSettings settings;
    m_color = colorFromSettings(settings.value("annotation/color"), QColor("#ff3b3b"));
    m_lineWidth = settings.value("annotation/width", 3).toInt();
    m_fontSize = settings.value("annotation/fontSize", 18).toInt();
    m_fontWeight = qBound(100,settings.value("annotation/fontWeight", 400).toInt(),900);
    for (int i=0;i<int(m_toolStyles.size());++i) {
        const QString prefix=QStringLiteral("annotation/tools/%1/").arg(toolSettingsKey(static_cast<Tool>(i)));
        ToolStyle &style=m_toolStyles[i];
        style.color=colorFromSettings(settings.value(prefix+QStringLiteral("color")),m_color);
        style.width=qBound(1,settings.value(prefix+QStringLiteral("width"),m_lineWidth).toInt(),20);
        style.fontSize=qBound(10,settings.value(prefix+QStringLiteral("fontSize"),m_fontSize).toInt(),72);
        const int weight=settings.value(prefix+QStringLiteral("fontWeight"),m_fontWeight).toInt();
        style.fontWeight=qBound(100,((weight+50)/100)*100,900);
    }
    buildToolbar();
    loadToolStyle();
    // 滑块拖动时每变化一格就写一次设置（Windows 是同步写注册表），会明显卡顿。
    // 改为停止变化 400ms 后统一落盘；覆盖层关闭时也会补写一次。
    m_settingsSaveTimer.setSingleShot(true);
    m_settingsSaveTimer.setInterval(400);
    connect(&m_settingsSaveTimer,&QTimer::timeout,this,[this] { saveStyleSettings(); });
    connect(m_document.undoStack(), &QUndoStack::indexChanged, this, [this] {
        if (selectedIndex()<0) m_selectedId=0;
        syncSelectedStyle(); m_undoButton->setEnabled(m_document.undoStack()->canUndo());
        m_redoButton->setEnabled(m_document.undoStack()->canRedo()); update();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) { cancelCapture(); });
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *) { cancelCapture(); });
    connect(screen, &QScreen::geometryChanged, this, [this](const QRect &) { cancelCapture(); });
    m_pointer = mapFromGlobal(QCursor::pos());
    m_toolbarAnchor=m_pointer;
    m_hoverWindow = windowAt(m_pointer);
}
void CaptureOverlay::buildToolbar() {
    m_toolbar=new QFrame(this);
    m_toolbar->setObjectName("captureToolbar");
    m_toolbar->setStyleSheet(QStringLiteral(
        "QFrame#captureToolbar{background:#f8f8f8;border:1px solid #aeb5be;border-radius:6px;}"
        "QLabel{color:#222;} QToolButton{padding:0;border:0;border-radius:4px;color:#222;}"
        "QToolButton:checked{background:#cde5ff;} QToolButton:hover{background:#e2eaf5;}"
        "QToolButton:focus{border:1px solid #1687ef;} QToolButton:disabled{color:#9299a2;}"));
    auto *layout=new QVBoxLayout(m_toolbar);
    layout->setContentsMargins(6,6,6,6); layout->setSpacing(4);
    m_primaryLayout=new QBoxLayout(QBoxLayout::LeftToRight);
    m_primaryLayout->setContentsMargins(0,0,0,0); m_primaryLayout->setSpacing(8);
    layout->addLayout(m_primaryLayout);
    m_toolArea=new QWidget(m_toolbar);
    m_toolLayout=new QGridLayout(m_toolArea);
    m_toolLayout->setContentsMargins(0,0,0,0); m_toolLayout->setSpacing(2);
    m_primaryLayout->addWidget(m_toolArea,0,Qt::AlignLeft);
    auto *group=new QButtonGroup(m_toolbar); group->setExclusive(true);
    const QStringList names={QStringLiteral("选择"),QStringLiteral("矩形"),QStringLiteral("椭圆"),QStringLiteral("箭头"),
                             QStringLiteral("画笔"),QStringLiteral("文字"),QStringLiteral("马赛克")};
    const QStringList keys={QStringLiteral("V"),QStringLiteral("R"),QStringLiteral("O"),QStringLiteral("A"),
                            QStringLiteral("P"),QStringLiteral("T"),QStringLiteral("M")};
    for (int i=0;i<names.size();++i) {
        const Tool tool=static_cast<Tool>(i);
        auto *button=new QToolButton(m_toolArea);
        button->setObjectName(QStringLiteral("tool_%1").arg(i));
        button->setIcon(toolIcon(tool)); button->setIconSize(QSize(22,22)); button->setFixedSize(32,32);
        button->setText(names[i]); button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setToolTip(QStringLiteral("%1 (%2)").arg(names[i],keys[i])); button->setAccessibleName(names[i]);
        button->setCheckable(true); button->setFocusPolicy(Qt::TabFocus);
        group->addButton(button); m_toolButtons.append(button); m_toolLayout->addWidget(button,0,i);
        if (tool==Tool::Select) button->setChecked(true);
        connect(button,&QToolButton::clicked,this,[this,tool] {
            const bool same=m_tool==tool;
            commitText(); commitStyleEdit();
            if (m_colorPanel) m_colorPanel->hide();
            setTool(tool);
            m_parametersExpanded=same ? !m_parametersExpanded : tool!=Tool::Select;
            updateStylePanel(); positionToolbar();
        });
    }
    auto *actions=new QWidget(m_toolbar);
    auto *actionRow=new QHBoxLayout(actions); actionRow->setContentsMargins(0,0,0,0); actionRow->setSpacing(2);
    m_primaryLayout->addWidget(actions,0,Qt::AlignRight);
    auto action=[actions,actionRow](const char *name,const QString &label,const QString &tip) {
        auto *button=new QToolButton(actions); button->setObjectName(name); button->setText(label);
        button->setToolTip(tip); button->setAccessibleName(label); button->setFixedSize(32,32);
        button->setFocusPolicy(Qt::TabFocus); actionRow->addWidget(button); return button;
    };
    m_colorButton=action("colorButton",QStringLiteral("颜色"),QStringLiteral("选择颜色"));
    updateColorSwatch();
    connect(m_colorButton,&QToolButton::clicked,this,&CaptureOverlay::toggleColorPanel);
    m_undoButton=action("undoButton",QStringLiteral("撤销"),QStringLiteral("撤销 (Ctrl/Command+Z)"));
    m_redoButton=action("redoButton",QStringLiteral("重做"),QStringLiteral("重做 (Ctrl/Command+Shift+Z)"));
    for (QToolButton *button : {m_undoButton,m_redoButton}) {
        const bool redo=button==m_redoButton;
        QPixmap image(24,24); image.fill(Qt::transparent);
        QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
        if (redo) { painter.translate(24,0); painter.scale(-1,1); }
        painter.setPen(QPen(QColor("#303640"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.drawArc(QRectF(5,7,15,13),-30*16,230*16);
        painter.drawLine(QPointF(4,5),QPointF(4,12)); painter.drawLine(QPointF(4,12),QPointF(11,12));
        painter.end(); button->setIcon(QIcon(image)); button->setIconSize(QSize(22,22));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly); button->setEnabled(false);
    }
    connect(m_undoButton,&QToolButton::clicked,this,[this] { commitText(); commitStyleEdit(); m_document.undoStack()->undo(); });
    connect(m_redoButton,&QToolButton::clicked,this,[this] { commitText(); commitStyleEdit(); m_document.undoStack()->redo(); });
    auto *cancel=action("cancelButton",QStringLiteral("×"),QStringLiteral("取消 (Esc)"));
    cancel->setAccessibleName(QStringLiteral("取消"));
    auto *done=action("doneButton",QStringLiteral("完成"),QStringLiteral("复制到剪贴板 (Enter)"));
    done->setFixedWidth(52);
    done->setStyleSheet(QStringLiteral("QToolButton{background:#1687ef;color:white;} QToolButton:hover{background:#0876d5;}"));
    connect(cancel,&QToolButton::clicked,this,&CaptureOverlay::cancelCapture);
    connect(done,&QToolButton::clicked,this,&CaptureOverlay::finishCapture);
    buildStylePanel(); layout->addWidget(m_stylePanel);
    buildColorPanel();
    connect(m_widthControl, &QSlider::valueChanged, this, [this](int n) {
        m_lineWidth=n; m_toolStyles[int(styleTool())].width=n; markStyleDirty();
        if (m_textEditor) {
            applyEditorTextStyle();
        } else if (selectedIndex()>=0 && selectedIndex()<m_document.items().size() && m_document.items()[selectedIndex()].tool!=Tool::Text) {
            const int i=selectedIndex(); auto item=m_document.items()[i]; item.width=n;
            applyStyleChange(i,item);
        }
    });
    connect(m_fontControl, &QSlider::valueChanged, this, [this](int n) {
        m_fontSize=n; m_toolStyles[int(styleTool())].fontSize=n; markStyleDirty();
        if (m_textEditor) {
            applyEditorTextStyle(); resizeTextEditor();
        } else if (selectedIndex()>=0 && selectedIndex()<m_document.items().size() && m_document.items()[selectedIndex()].tool==Tool::Text) {
            const int i=selectedIndex(); auto item=m_document.items()[i]; item.fontSize=n; resizeTextAnnotation(item);
            applyStyleChange(i,item);
        }
    });
    connect(m_fontWeightControl, &QSlider::valueChanged, this, [this](int n) {
        const int snapped=qBound(100,((n+50)/100)*100,900);
        if (n!=snapped) { m_fontWeightControl->setValue(snapped); return; }
        m_fontWeight=n; m_toolStyles[int(styleTool())].fontWeight=n; markStyleDirty();
        if (m_textEditor) {
            applyEditorTextStyle(); resizeTextEditor();
        } else if (selectedIndex()>=0 && selectedIndex()<m_document.items().size() && m_document.items()[selectedIndex()].tool==Tool::Text) {
            const int i=selectedIndex(); auto item=m_document.items()[i]; item.fontWeight=n; resizeTextAnnotation(item);
            applyStyleChange(i,item);
        }
    });
    // 滑块调整样式：按下时开启编辑会话，释放时提交一次，连续拖动只产生一步撤销。
    // 会话由 AnnotationDocument 管理，滑块丢失 grab 或覆盖层关闭时也不会泄漏到撤销栈。
    for (QSlider *slider : {m_widthControl,m_fontControl,m_fontWeightControl}) {
        connect(slider,&QSlider::sliderPressed,this,[this,slider] {
            if (m_textEditor) return;
            if (selectedIndex()<0 || selectedIndex()>=m_document.items().size()) return;
            const bool applies=(slider==m_widthControl) != (m_document.items()[selectedIndex()].tool==Tool::Text);
            if (!applies) return;
            m_document.beginEdit();
        });
        connect(slider,&QSlider::sliderReleased,this,[this] { commitStyleEdit(); });
    }
    updateStylePanel(); m_toolbar->adjustSize(); m_toolbar->hide();
}
void CaptureOverlay::buildStylePanel() {
    m_stylePanel=new QFrame(m_toolbar); m_stylePanel->setObjectName("stylePanel");
    auto *layout=new QVBoxLayout(m_stylePanel);
    layout->setContentsMargins(4,4,4,0); layout->setSpacing(2);
    m_toolTitle=new QLabel(m_stylePanel); m_toolTitle->setObjectName("toolTitle");
    m_toolTitle->setStyleSheet(QStringLiteral("color:#586373; font-size:11px;")); layout->addWidget(m_toolTitle);
    auto addRow=[this,layout](const char *name) {
        auto *row=new QFrame(m_stylePanel); row->setObjectName(name); layout->addWidget(row);
        auto *box=new QHBoxLayout(row); box->setContentsMargins(0,0,0,0); return box;
    };
    m_widthControl=addStyledSlider(addRow("widthRow"),QStringLiteral("粗细"),1,20,m_lineWidth,&m_widthValue);
    m_widthControl->setObjectName("widthControl"); m_widthValue->setObjectName("widthValue");
    m_fontControl=addStyledSlider(addRow("fontRow"),QStringLiteral("字号"),10,72,m_fontSize,&m_fontValue);
    m_fontControl->setObjectName("fontControl"); m_fontValue->setObjectName("fontValue");
    m_fontWeightControl=addStyledSlider(addRow("weightRow"),QStringLiteral("字重"),100,900,m_fontWeight,&m_fontWeightValue);
    m_fontWeightControl->setObjectName("fontWeightControl"); m_fontWeightValue->setObjectName("fontWeightValue");
    m_fontWeightControl->setSingleStep(100); m_fontWeightControl->setPageStep(100);
    m_fontWeightControl->setTickPosition(QSlider::TicksBelow); m_fontWeightControl->setTickInterval(100);
    m_stylePanel->hide();
}
void CaptureOverlay::updateStylePanel() {
    if (!m_stylePanel) return;
    const Tool tool=m_textEditor ? Tool::Text : (selectedIndex()>=0 ? m_document.items()[selectedIndex()].tool : m_tool);
    const bool text=tool==Tool::Text;
    const bool width=tool==Tool::Rectangle || tool==Tool::Ellipse || tool==Tool::Arrow || tool==Tool::Pen;
    const QStringList names={QStringLiteral("选择"),QStringLiteral("矩形"),QStringLiteral("椭圆"),QStringLiteral("箭头"),
                             QStringLiteral("画笔"),QStringLiteral("文字"),QStringLiteral("马赛克")};
    m_toolTitle->setText(names[int(tool)]);
    m_stylePanel->findChild<QFrame *>("widthRow")->setVisible(width);
    m_stylePanel->findChild<QFrame *>("fontRow")->setVisible(text);
    m_stylePanel->findChild<QFrame *>("weightRow")->setVisible(text);
    m_stylePanel->setVisible(m_parametersExpanded && (text || width));
    m_colorButton->setEnabled(tool!=Tool::Mosaic);
}
void CaptureOverlay::hidePanels() {
    m_parametersExpanded=false;
    if (m_stylePanel) m_stylePanel->hide();
    if (m_colorPanel) m_colorPanel->hide();
    if (m_toolbar && !m_selection.isEmpty() && !m_ending) positionToolbar();
}
bool CaptureOverlay::panelVisible() const {
    return (m_stylePanel && m_stylePanel->isVisible()) || (m_colorPanel && m_colorPanel->isVisible());
}
void CaptureOverlay::buildColorPanel() {
    // 自绘取色面板：原生 QColorDialog 在 macOS 会被覆盖层（NSScreenSaverWindowLevel）吞掉。
    m_colorPanel=new QFrame(this);
    m_colorPanel->setObjectName("colorPanel");
    m_colorPanel->setStyleSheet("QFrame#colorPanel{background:#f8f8f8;border:1px solid #aaa;border-radius:6px;}"
                                "QLabel{color:#222;}");
    auto *outer=new QVBoxLayout(m_colorPanel); outer->setContentsMargins(0,0,0,0);
    auto *scroll=new QScrollArea(m_colorPanel); scroll->setObjectName("colorScroll");
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content=new QWidget; scroll->setWidget(content); outer->addWidget(scroll);
    auto *layout=new QVBoxLayout(content);
    layout->setContentsMargins(12,10,12,10); layout->setSpacing(8);

    auto *grid=new QGridLayout; grid->setSpacing(4);
    const QVector<QColor> presets=presetColors();
    for (int i=0;i<presets.size();++i) {
        auto *button=new QToolButton(m_colorPanel);
        button->setObjectName(QStringLiteral("preset_%1").arg(i));
        button->setFixedSize(22,22);
        button->setToolTip(presets.at(i).name());
        button->setAccessibleName(presets.at(i).name());
        connect(button,&QToolButton::clicked,this,[this,color=presets.at(i)] { applyChosenColor(color); });
        grid->addWidget(button,i/8,i%8);
    }
    layout->addLayout(grid);

    auto *advancedButton=new QToolButton(m_colorPanel);
    advancedButton->setObjectName("advancedColorButton"); advancedButton->setText(QStringLiteral("更多颜色"));
    advancedButton->setCheckable(true); layout->addWidget(advancedButton);
    m_colorAdvanced=new QFrame(m_colorPanel); m_colorAdvanced->setObjectName("advancedColors");
    auto *advancedLayout=new QVBoxLayout(m_colorAdvanced); advancedLayout->setContentsMargins(0,0,0,0);
    auto *hueRow=new QHBoxLayout;
    m_hueControl=addStyledSlider(hueRow,QStringLiteral("色相"),0,359,0,&m_hueValue);
    m_hueControl->setObjectName("hueControl"); advancedLayout->addLayout(hueRow);
    QLinearGradient hue; hue.setCoordinateMode(QGradient::ObjectBoundingMode);
    hue.setColorAt(0.0,QColor("#ff0000")); hue.setColorAt(0.17,QColor("#ffff00"));
    hue.setColorAt(0.33,QColor("#00ff00")); hue.setColorAt(0.5,QColor("#00ffff"));
    hue.setColorAt(0.67,QColor("#0000ff")); hue.setColorAt(0.83,QColor("#ff00ff")); hue.setColorAt(1.0,QColor("#ff0000"));
    setSliderGradient(m_hueControl,hue);
    auto *saturationRow=new QHBoxLayout;
    m_saturationControl=addStyledSlider(saturationRow,QStringLiteral("饱和度"),0,100,100,&m_saturationValue);
    m_saturationControl->setObjectName("saturationControl"); advancedLayout->addLayout(saturationRow);
    auto *valueRow=new QHBoxLayout;
    m_valueControl=addStyledSlider(valueRow,QStringLiteral("明度"),0,100,100,&m_valueValue);
    m_valueControl->setObjectName("valueControl"); advancedLayout->addLayout(valueRow);
    layout->addWidget(m_colorAdvanced); m_colorAdvanced->hide();
    connect(advancedButton,&QToolButton::toggled,this,[this](bool visible) {
        m_colorAdvanced->setVisible(visible);
        const QPoint anchor=m_colorButton->mapTo(this,QPoint(m_colorButton->width()/2,m_colorButton->height()));
        positionPanel(m_colorPanel,anchor,false);
    });
    m_colorPreview=new QToolButton(m_colorPanel); m_colorPreview->setObjectName("colorPreview");
    m_colorPreview->setFixedHeight(24); m_colorPreview->setAccessibleName(QStringLiteral("当前颜色值"));
    layout->addWidget(m_colorPreview);

    auto *recent=new QHBoxLayout; recent->setSpacing(4);
    for (int i=0;i<8;++i) {
        auto *button=new QToolButton(m_colorPanel);
        button->setObjectName(QStringLiteral("recent_%1").arg(i));
        button->setFixedSize(18,18);
        connect(button,&QToolButton::clicked,this,[this,i] { applyRecentColor(i); });
        recent->addWidget(button);
    }
    layout->addLayout(recent);

    for (QSlider *slider : {m_hueControl,m_saturationControl,m_valueControl}) {
        connect(slider,&QSlider::sliderPressed,this,[this] {
            if (!m_textEditor && selectedIndex()>=0) m_document.beginEdit();
        });
        connect(slider,&QSlider::valueChanged,this,[this] { applyColor(previewColor(),false); });
        connect(slider,&QSlider::sliderReleased,this,[this] { commitStyleEdit(); rememberColor(m_color); });
    }
    syncColorControls();
    updateRecentButtons();
    updatePresetHighlight();
    m_colorPanel->hide();
}
void CaptureOverlay::toggleColorPanel() {
    if (m_colorPanel->isVisible()) { hidePanels(); return; }
    syncColorControls();
    m_colorPanel->show();
    updateRecentButtons();
    updatePresetHighlight();
    const QPoint anchor=m_colorButton->mapTo(this,QPoint(m_colorButton->width()/2,m_colorButton->height()));
    positionPanel(m_colorPanel,anchor,false);
}
QColor CaptureOverlay::previewColor() const {
    return QColor::fromHsv(m_hueControl->value(),qRound(m_saturationControl->value()*2.55),qRound(m_valueControl->value()*2.55));
}
void CaptureOverlay::syncColorControls() {
    if (!m_hueControl) return;
    const QSignalBlocker hue(m_hueControl),saturation(m_saturationControl),value(m_valueControl);
    if (m_color.hsvHue()>=0) m_hueControl->setValue(m_color.hsvHue());
    m_saturationControl->setValue(qRound(m_color.hsvSaturationF()*100));
    m_valueControl->setValue(qRound(m_color.valueF()*100));
    m_hueValue->setText(QString::number(m_hueControl->value()));
    m_saturationValue->setText(QString::number(m_saturationControl->value()));
    m_valueValue->setText(QString::number(m_valueControl->value()));
    updateColorPreview();
}
void CaptureOverlay::updateColorPreview() {
    if (!m_colorPreview) return;
    m_colorPreview->setText(m_color.name().toUpper());
    m_colorPreview->setStyleSheet(QStringLiteral("QToolButton#colorPreview{background:%1;color:%2;border:1px solid #999;border-radius:3px;}")
                                .arg(m_color.name(),m_color.lightness()<130?QStringLiteral("white"):QStringLiteral("black")));
    const int hue=m_hueControl->value();
    const qreal saturation=m_saturationControl->value()/100.0,value=m_valueControl->value()/100.0;
    QLinearGradient saturationGradient; saturationGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    saturationGradient.setColorAt(0,QColor::fromHsvF(hue/360.0,0,value));
    saturationGradient.setColorAt(1,QColor::fromHsvF(hue/360.0,1,value));
    setSliderGradient(m_saturationControl,saturationGradient);
    QLinearGradient valueGradient; valueGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    valueGradient.setColorAt(0,Qt::black); valueGradient.setColorAt(1,QColor::fromHsvF(hue/360.0,saturation,1));
    setSliderGradient(m_valueControl,valueGradient);
}
QVector<QColor> CaptureOverlay::recentColors() const {
    QVector<QColor> result;
    const QStringList stored=colorNamesFromSettings(QSettings().value("annotation/recentColors"));
    for (const QString &name : stored) { const QColor color(name); if (color.isValid()) result.append(color); }
    return result;
}
void CaptureOverlay::rememberColor(const QColor &color) {
    if (!color.isValid()) return;
    QStringList stored=colorNamesFromSettings(QSettings().value("annotation/recentColors"));
    stored.removeAll(color.name());
    stored.prepend(color.name());
    while (stored.size()>8) stored.removeLast();
    // 用逗号分隔的字符串存放：跨平台读回时列表类型不会被后端改写。
    QSettings().setValue("annotation/recentColors",stored.join(','));
    updateRecentButtons();
}
void CaptureOverlay::updateRecentButtons() {
    if (!m_colorPanel) return;
    const QVector<QColor> colors=recentColors();
    for (int i=0;i<8;++i) {
        auto *button=m_colorPanel->findChild<QToolButton *>(QStringLiteral("recent_%1").arg(i));
        if (!button) continue;
        const QColor color=i<colors.size()?colors.at(i):QColor();
        if (color.isValid()) {
            button->setStyleSheet(QStringLiteral("QToolButton{background:%1;border:1px solid #999;}").arg(color.name()));
            button->setToolTip(color.name());
            button->setEnabled(true);
        } else {
            button->setStyleSheet(QStringLiteral("QToolButton{background:transparent;border:1px dashed #ccc;}"));
            button->setToolTip(QString());
            button->setEnabled(false);
        }
    }
}
void CaptureOverlay::updatePresetHighlight() {
    if (!m_colorPanel) return;
    const QVector<QColor> presets=presetColors();
    for (int i=0;i<presets.size();++i) {
        auto *button=m_colorPanel->findChild<QToolButton *>(QStringLiteral("preset_%1").arg(i));
        if (!button) continue;
        const bool current=(presets.at(i)==m_color);
        button->setStyleSheet(QStringLiteral("QToolButton{background:%1;border:%2 solid %3;border-radius:3px;}")
                              .arg(presets.at(i).name(),current?"2px":"1px",current?"#1687ef":"#999"));
    }
}
void CaptureOverlay::applyRecentColor(int index) {
    const QVector<QColor> colors=recentColors();
    if (index<0 || index>=colors.size()) return;
    applyChosenColor(colors.at(index));
}
QRect CaptureOverlay::uiScreenBounds() const {
    QVector<QRect> screens;
    for (const auto &screen : m_frame.screens) screens.append(screen.geometry);
    const QRect host=interactionScreen(screens,toVirtual(m_toolbarAnchor),m_selection.translated(m_frame.virtualGeometry.topLeft()));
    return host.translated(-m_frame.virtualGeometry.topLeft());
}
void CaptureOverlay::positionPanel(QFrame *panel, const QPoint &anchor, bool preferAbove) {
    const QRect host=uiScreenBounds();
    if (host.isEmpty()) return;
    auto *scroll=panel->findChild<QScrollArea *>("colorScroll");
    const int requestedHeight=scroll ? scroll->widget()->sizeHint().height()+4 : panel->sizeHint().height();
    const QSize wanted(qMin(300,qMax(1,host.width()-16)),requestedHeight);
    const QRect placed=popupPlacement(wanted,m_toolbar->geometry(),anchor,host,preferAbove);
    panel->setGeometry(placed); panel->raise();
}
void CaptureOverlay::updateColorSwatch() {
    if (!m_colorButton) return;
    QPixmap swatch(20,20); swatch.fill(Qt::transparent); QPainter p(&swatch);
    p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(QColor("#666666"),1)); p.setBrush(m_color); p.drawEllipse(QRectF(3,3,14,14));
    m_colorButton->setIcon(QIcon(swatch)); m_colorButton->setIconSize(QSize(20,20));
}
void CaptureOverlay::applyChosenColor(const QColor &color) { applyColor(color,true); }
void CaptureOverlay::applyColor(const QColor &color, bool commitRecent) {
    if (!color.isValid() || styleTool()==Tool::Mosaic) return;
    m_color=color; m_toolStyles[int(styleTool())].color=color; markStyleDirty(); updateColorSwatch();
    if (m_textEditor) {
        m_textColor=color; applyEditorTextStyle();
    } else if (selectedIndex()>=0) {
        const int index=selectedIndex(); auto item=m_document.items()[index]; item.color=color;
        applyStyleChange(index,item);
    }
    if (commitRecent) { commitStyleEdit(); rememberColor(color); syncColorControls(); }
    else updateColorPreview();
    updatePresetHighlight();
}
void CaptureOverlay::positionToolbar() {
    if (m_selection.isEmpty() || m_ending) { m_toolbar->hide(); return; }
    const QRect host=uiScreenBounds();
    if (host.isEmpty()) return;
    const int available=qMax(1,host.width()-16);
    const int toolbarWidth=qMin(460,available);
    const bool narrow=toolbarWidth<448;
    m_primaryLayout->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    const int columns=narrow ? qMax(1,qMin(7,(toolbarWidth-12)/34)) : 7;
    for (int i=0;i<m_toolButtons.size();++i) {
        m_toolLayout->removeWidget(m_toolButtons[i]);
        m_toolLayout->addWidget(m_toolButtons[i],i/columns,i%columns);
    }
    m_toolbar->setFixedWidth(toolbarWidth);
    m_toolbar->layout()->invalidate(); m_toolbar->layout()->activate(); m_toolbar->adjustSize();
    const QRect placed=toolbarPlacement(m_toolbar->size(),m_selection,host);
    m_toolbar->move(placed.topLeft()); m_toolbar->show(); m_toolbar->raise();
    if (m_colorPanel->isVisible()) {
        const QPoint anchor=m_colorButton->mapTo(this,QPoint(m_colorButton->width()/2,m_colorButton->height()));
        positionPanel(m_colorPanel,anchor,false);
    }
}
int CaptureOverlay::selectedIndex() const { return m_document.indexOf(m_selectedId); }
int CaptureOverlay::editingTextIndex() const { return m_document.indexOf(m_editingTextId); }
void CaptureOverlay::selectObject(int index) {
    m_selectedId=index>=0 && index<m_document.items().size() ? m_document.items()[index].id : 0;
}
void CaptureOverlay::syncSelectedStyle() {
    const int index=selectedIndex();
    if (index<0) { loadToolStyle(); return; }
    const Annotation &item=m_document.items()[index];
    const QSignalBlocker widthBlock(m_widthControl),fontBlock(m_fontControl),weightBlock(m_fontWeightControl);
    m_color=item.color;
    updateColorSwatch();
    if (item.tool==Tool::Text) {
        m_fontSize=item.fontSize; m_fontWeight=item.fontWeight;
        m_fontControl->setValue(m_fontSize); m_fontWeightControl->setValue(m_fontWeight);
        m_fontValue->setText(QString::number(m_fontSize)); m_fontWeightValue->setText(QString::number(m_fontWeight));
    } else {
        m_lineWidth=qRound(item.width); m_widthControl->setValue(m_lineWidth);
        m_widthValue->setText(QString::number(m_lineWidth));
    }
    syncColorControls(); updateStylePanel();
}
void CaptureOverlay::applyStyleChange(int index, const Annotation &item) {
    // 拖动中（会话已开启）就地修改，释放时统一提交为一步撤销；
    // 键盘或滚轮调整没有会话，立即提交为一步撤销。
    const bool editing = m_document.editing();
    if (!editing) m_document.beginEdit();
    m_document.setItemInPlace(index, item);
    selectObject(index);
    if (!editing) commitStyleEdit();
    // 样式变化只影响选区内的标注：只重绘选区与该标注覆盖的范围。
    // 否则每滑动一格都要重贴整块虚拟桌面的多屏大图（3 屏时是几十 MB 像素），拖起来会明显发涩。
    // 标注坐标与 m_selection 一样都是覆盖层局部逻辑坐标，无需再平移。
    const QRectF localBounds=item.bounds();
    const QRect dirty=m_selection.united(localBounds).adjusted(-8,-8,8,8).toAlignedRect().intersected(rect());
    update(dirty);
}
void CaptureOverlay::commitStyleEdit() {
    if (!m_document.editing()) return;
    const AnnotationId selected = m_selectedId;
    m_document.endEdit();
    m_selectedId = selected;
    update();
}
Tool CaptureOverlay::styleTool() const {
    if (m_textEditor) return Tool::Text;
    if (selectedIndex()>=0) return m_document.items()[selectedIndex()].tool;
    return m_tool==Tool::Select ? m_lastDrawingTool : m_tool;
}
void CaptureOverlay::markStyleDirty() {
    m_styleDirty[int(styleTool())]=true;
    m_settingsSaveTimer.start();
}
void CaptureOverlay::loadToolStyle() {
    const ToolStyle &style=m_toolStyles[int(styleTool())];
    const QSignalBlocker width(m_widthControl),font(m_fontControl),weight(m_fontWeightControl);
    m_color=style.color; m_lineWidth=style.width; m_fontSize=style.fontSize; m_fontWeight=style.fontWeight;
    m_widthControl->setValue(m_lineWidth); m_fontControl->setValue(m_fontSize); m_fontWeightControl->setValue(m_fontWeight);
    m_widthValue->setText(QString::number(m_lineWidth)); m_fontValue->setText(QString::number(m_fontSize));
    m_fontWeightValue->setText(QString::number(m_fontWeight));
    updateColorSwatch(); syncColorControls(); updateStylePanel();
}
void CaptureOverlay::saveStyleSettings() {
    QSettings settings;
    for (int i=0;i<int(m_toolStyles.size());++i) {
        if (!m_styleDirty[i]) continue;
        const ToolStyle &style=m_toolStyles[i];
        const QString prefix=QStringLiteral("annotation/tools/%1/").arg(toolSettingsKey(static_cast<Tool>(i)));
        settings.setValue(prefix+QStringLiteral("color"),style.color.name());
        settings.setValue(prefix+QStringLiteral("width"),style.width);
        settings.setValue(prefix+QStringLiteral("fontSize"),style.fontSize);
        settings.setValue(prefix+QStringLiteral("fontWeight"),style.fontWeight);
        m_styleDirty[i]=false;
    }
    m_persistedWidth=m_toolStyles[int(styleTool())].width;
}
void CaptureOverlay::setTool(Tool tool) {
    m_tool=tool; m_selectedId=0;
    if (tool!=Tool::Select) m_lastDrawingTool=tool;
    loadToolStyle();
    setCursor(tool==Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor); update();
}
QPointF CaptureOverlay::boundedPoint(const QPointF &p) const { return {qBound(0.0,p.x(),double(width())), qBound(0.0,p.y(),double(height()))}; }
QRectF CaptureOverlay::windowAt(const QPointF &p) const {
    // 窗口矩形是虚拟桌面逻辑坐标，比较前先换算到覆盖层局部坐标。
    const QPoint origin=m_frame.virtualGeometry.topLeft();
    for (const QRect &window : m_frame.windowRects) {
        const QRect local=window.translated(-origin);
        if (local.contains(p.toPoint())) return QRectF(local);
    }
    return QRectF(QPointF(0,0),size());
}
int CaptureOverlay::objectAt(const QPointF &p) const {
    const auto &items=m_document.items();
    for (int i=int(items.size())-1; i>=0; --i) {
        if (annotationContains(items[i],p)) return i;
    }
    return -1;
}
int CaptureOverlay::selectionHandleAt(const QPointF &p) const {
    const auto points=handles(m_selection);
    for (int i=0;i<points.size();++i) if (handleRect(points[i]).contains(p)) return i;
    return -1;
}
QRectF CaptureOverlay::resizeRect(const QRectF &start, int handle, const QPointF &p) const {
    QRectF r=start;
    if (handle==0||handle==6||handle==7) r.setLeft(p.x());
    if (handle==2||handle==3||handle==4) r.setRight(p.x());
    if (handle<=2) r.setTop(p.y());
    if (handle>=4&&handle<=6) r.setBottom(p.y());
    return r.normalized().intersected(QRectF(QPointF(0,0),size()));
}
QVector<Annotation> CaptureOverlay::previewItems() const {
    auto items=m_document.items();
    if (m_hasPreview) {
        if (m_drag==Drag::MoveObject || m_drag==Drag::ResizeObject || m_drag==Drag::ArrowStart || m_drag==Drag::ArrowEnd) {
            if (selectedIndex()>=0 && selectedIndex()<items.size()) items[selectedIndex()]=m_preview;
        } else items.append(m_preview);
    }
    return items;
}
void CaptureOverlay::ensureRenderCache() {
    // 选区没变时只重绘标注图层，底图不重新裁剪（B6）。
    if (m_cacheSelection!=m_selection) {
        m_cacheSelection=m_selection;
        m_caches.clear();
        const QPoint origin=m_frame.virtualGeometry.topLeft();
        for (const auto &screen : m_frame.screens) {
            if (screen.isEmpty()) continue;
            const QRect screenLocal=screen.geometry.translated(-origin);
            const QRectF selected=m_selection.intersected(screenLocal);
            if (selected.isEmpty()) continue;
            ScreenCache cache;
            cache.logicalRect=selected;
            cache.origin=screenLocal.topLeft();
            cache.scaleX=screen.scaleX(); cache.scaleY=screen.scaleY();
            cache.crop=pixelRect(selected.translated(-screenLocal.topLeft()),screen.geometry.size(),screen.image.size());
            if (cache.crop.isEmpty()) continue;
            cache.base=screen.image.copy(cache.crop);
            cache.annotations=QImage(cache.base.size(),QImage::Format_ARGB32_Premultiplied);
            m_caches.append(cache);
            ++m_cropCount;
        }
    }
    const QVector<Annotation> items=previewItems();
    for (auto &cache : m_caches) {
        if (cache.annotations.isNull()) cache.annotations=QImage(cache.base.size(),QImage::Format_ARGB32_Premultiplied);
        cache.annotations.fill(Qt::transparent);
        renderAnnotationLayer(cache.annotations,cache.base,cache.crop,cache.origin,cache.scaleX,cache.scaleY,items);
    }
}
QVector<Annotation> CaptureOverlay::virtualAnnotations() const {
    QVector<Annotation> items=m_document.items();
    const QPointF delta=m_frame.virtualGeometry.topLeft();
    if (!delta.isNull()) for (auto &item : items) item.translate(delta);
    return items;
}
void CaptureOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const QPoint origin=m_frame.virtualGeometry.topLeft();
    // 各屏内容按各自的逻辑位置铺开。
    for (const auto &screen : m_frame.screens) {
        if (screen.isEmpty()) continue;
        p.drawImage(screen.geometry.translated(-origin),screen.image);
    }
    p.fillRect(rect(),QColor(0,0,0,110));
    if (!m_selection.isEmpty()) {
        ensureRenderCache();
        for (const auto &cache : m_caches) {
            p.drawImage(cache.logicalRect,cache.base);
            p.drawImage(cache.logicalRect,cache.annotations);
        }
        p.setPen(QPen(QColor("#1687ef"),2)); p.setBrush(Qt::NoBrush); p.drawRect(m_selection);
        p.setBrush(Qt::white);
        for (const auto &point : handles(m_selection)) p.drawRect(handleRect(point));
        if (selectedIndex()>=0 && selectedIndex()<m_document.items().size()) {
            const QRectF box=(m_hasPreview ? m_preview : m_document.items()[selectedIndex()]).bounds();
            p.setPen(QPen(QColor("#1687ef"),1,Qt::DashLine)); p.setBrush(Qt::NoBrush); p.drawRect(box);
            p.setBrush(Qt::white);
            const Annotation &a=(m_hasPreview ? m_preview : m_document.items()[selectedIndex()]);
            if (a.tool==Tool::Arrow) { p.drawRect(handleRect(a.rect.topLeft())); p.drawRect(handleRect(a.rect.bottomRight())); }
            else p.drawRect(handleRect(box.bottomRight()));
        }
        const SelectionMetrics metrics=selectionMetrics(m_frame.screens,m_selection.translated(origin));
        QString label=QStringLiteral("%1 × %2 px").arg(metrics.pixelSize.width()).arg(metrics.pixelSize.height());
        if (metrics.screenCount>1) label+=QStringLiteral(" · 跨 %1 屏").arg(metrics.screenCount);
        const QRect desired(qRound(m_selection.left()),qFloor(m_selection.top())-24,p.fontMetrics().horizontalAdvance(label)+12,22);
        const QRect tag=fitInsideScreen(desired,uiScreenBounds());
        p.setPen(QColor("#1687ef")); p.setBrush(Qt::white); p.drawRoundedRect(tag,3,3); p.drawText(tag,Qt::AlignCenter,label);
    } else if (!m_hoverWindow.isEmpty()) {
        p.setPen(QPen(QColor("#1687ef"),2)); p.setBrush(Qt::NoBrush); p.drawRect(m_hoverWindow);
    }
    if (m_selection.isEmpty() || m_drag==Drag::NewSelection) {
        // 放大镜按指针所在的那块屏取样：跨屏时不能拿另一块屏的像素。
        const ScreenImage *screen=screenAt(m_frame.screens,toVirtual(m_pointer));
        if (screen) {
            const QPointF local=toVirtual(m_pointer)-screen->geometry.topLeft();
            const int px=qBound(0,int(local.x()*screen->scaleX()),screen->image.width()-1);
            const int py=qBound(0,int(local.y()*screen->scaleY()),screen->image.height()-1);
            const QColor c=screen->image.pixelColor(px,py);
            const QImage sample=magnifierSample(screen->image,QPoint(px,py));
            const QRect screenLocal=screen->geometry.translated(-origin);
            const QRect box=fitInsideScreen(QRect(m_pointer.toPoint()+QPoint(18,18),QSize(146,144)),screenLocal);
            const int x=box.x(),y=box.y(); p.fillRect(box,QColor(20,20,20,230));
            p.drawImage(QRect(x+19,y+4,108,108),sample);
            p.setPen(Qt::red); p.drawLine(x+73,y+49,x+73,y+67); p.drawLine(x+64,y+58,x+82,y+58);
            p.setPen(Qt::white); p.drawText(QRect(x+4,y+115,138,24),Qt::AlignCenter,QStringLiteral("%1, %2  %3").arg(px).arg(py).arg(c.name().toUpper()));
        }
    }
}
void CaptureOverlay::clearSelection() {
    // 只清空选区与工具栏，保留已画的标注和撤销历史。
    // 样式编辑会话必须先提交，否则会话会残留在文档里。
    commitStyleEdit();
    m_selection = {};
    m_selectedId = 0;
    m_hoverWindow = {};
    loadToolStyle();
    m_toolbar->hide();
    hidePanels();
    m_drag=Drag::None; m_hasPreview=false;
    update();
}
void CaptureOverlay::mousePressEvent(QMouseEvent *event) {
    // 右键：清空选区重新框选，但保留已画的标注与撤销历史。
    if (event->button()==Qt::RightButton) { if (m_selection.isEmpty()) cancelCapture(); else { commitText(); clearSelection(); } return; }
    if (event->button()!=Qt::LeftButton) return;
    // 颜色板外的首次点击只收起颜色板，不误画标注。
    if (m_colorPanel->isVisible()) { m_colorPanel->hide(); return; }
    commitText(); m_press=boundedPoint(event->position()); m_pointer=m_press;
    if (m_selection.isEmpty()) { m_drag=Drag::NewSelection; m_startRect={}; return; }
    if (m_tool==Tool::Select) {
        if (selectedIndex()>=0 && selectedIndex()<m_document.items().size() && m_document.items()[selectedIndex()].tool==Tool::Arrow) {
            const auto &arrow=m_document.items()[selectedIndex()];
            if (handleRect(arrow.rect.topLeft()).contains(m_press) || handleRect(arrow.rect.bottomRight()).contains(m_press)) {
                m_drag=handleRect(arrow.rect.topLeft()).contains(m_press) ? Drag::ArrowStart : Drag::ArrowEnd;
                m_startAnnotation=arrow; return;
            }
        }
        if (selectedIndex()>=0 && selectedIndex()<m_document.items().size() && handleRect(m_document.items()[selectedIndex()].bounds().bottomRight()).contains(m_press)) {
            m_drag=Drag::ResizeObject; m_startAnnotation=m_document.items()[selectedIndex()]; m_startRect=m_startAnnotation.bounds(); return;
        }
        m_handle=selectionHandleAt(m_press);
        if (m_handle>=0) { m_selectedId=0; m_startRect=m_selection; m_drag=Drag::ResizeSelection; update(); return; }
        const int object=objectAt(m_press);
        if (object>=0) {
            selectObject(object); m_startAnnotation=m_document.items()[object];
            syncSelectedStyle();
            m_parametersExpanded=true; updateStylePanel(); positionToolbar();
            m_drag=Drag::MoveObject; update(); return;
        }
        m_selectedId=0; m_startRect=m_selection;
        if (m_selection.contains(m_press)) m_drag=Drag::MoveSelection;
        else { clearSelection(); m_drag=Drag::NewSelection; }
    } else if (m_selection.contains(m_press)) {
        if (m_tool==Tool::Text) { startText(m_press); return; }
        m_selectedId=0; loadToolStyle(); m_drag=Drag::Draw; m_preview={}; m_preview.tool=m_tool; m_preview.color=m_color; m_preview.width=m_lineWidth;
        m_preview.fontSize=m_fontSize; m_preview.rect=QRectF(m_press,m_press); if (m_tool==Tool::Pen) m_preview.points.append(m_press);
        m_hasPreview=true;
    }
}
void CaptureOverlay::updateDrag(const QPointF &point) {
    m_pointer=boundedPoint(point);
    if (m_drag==Drag::NewSelection) { if (QLineF(m_press,m_pointer).length()>=3) m_selection=normalizedAt(m_press,m_pointer); }
    else if (m_drag==Drag::MoveSelection) {
        QRectF r=m_startRect.translated(m_pointer-m_press);
        r.moveLeft(qBound(0.0,r.left(),qMax(0.0,width()-r.width()))); r.moveTop(qBound(0.0,r.top(),qMax(0.0,height()-r.height()))); m_selection=r;
    } else if (m_drag==Drag::ResizeSelection) m_selection=resizeRect(m_startRect,m_handle,m_pointer);
    else if (m_drag==Drag::Draw) { m_preview.rect=QRectF(m_press,m_pointer); if (m_tool==Tool::Pen) m_preview.points.append(m_pointer); }
    else if (m_drag==Drag::MoveObject) { m_preview=m_startAnnotation; m_preview.translate(m_pointer-m_press); m_hasPreview=true; }
    else if (m_drag==Drag::ResizeObject) { m_preview=m_startAnnotation; m_preview.setBounds(resizeRect(m_startRect,4,m_pointer)); m_hasPreview=true; }
    else if (m_drag==Drag::ArrowStart) { m_preview=m_startAnnotation; m_preview.rect.setTopLeft(m_pointer); m_hasPreview=true; }
    else if (m_drag==Drag::ArrowEnd) { m_preview=m_startAnnotation; m_preview.rect.setBottomRight(m_pointer); m_hasPreview=true; }
    else if (m_selection.isEmpty()) m_hoverWindow=windowAt(m_pointer);
    if (m_drag==Drag::MoveSelection || m_drag==Drag::ResizeSelection) positionToolbar();
    update();
}
void CaptureOverlay::mouseMoveEvent(QMouseEvent *event) { updateDrag(event->position()); }
void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button()!=Qt::LeftButton) return;
    if (m_drag!=Drag::None) updateDrag(event->position());
    if (m_drag==Drag::NewSelection) {
        if (QLineF(m_press,m_pointer).length()<3) m_selection=windowAt(m_press);
        if (m_selection.width()<2 || m_selection.height()<2) m_selection={};
        m_toolbarAnchor=m_pointer;
        positionToolbar();
    } else if (m_drag==Drag::Draw && m_hasPreview) {
        if (m_preview.tool==Tool::Pen || (m_preview.tool==Tool::Arrow && QLineF(m_preview.rect.topLeft(),m_preview.rect.bottomRight()).length()>=2) ||
            (m_preview.rect.normalized().width()>=2 && m_preview.rect.normalized().height()>=2)) { m_selectedId=m_document.add(m_preview); syncSelectedStyle(); }
    } else if ((m_drag==Drag::MoveObject || m_drag==Drag::ResizeObject || m_drag==Drag::ArrowStart || m_drag==Drag::ArrowEnd) && m_hasPreview && QLineF(m_press,m_pointer).length()>=0.5) {
        const int selected=selectedIndex(); m_document.replace(selected,m_preview); selectObject(selected);
    }
    m_drag=Drag::None; m_hasPreview=false; update();
}
void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent *event) {
    if (m_tool==Tool::Select) {
        const int i=objectAt(event->position());
        if (i>=0 && m_document.items()[i].tool==Tool::Text) { startText(event->position(),i); return; }
    }
    QWidget::mouseDoubleClickEvent(event);
}
void CaptureOverlay::startText(const QPointF &point, int existing) {
    commitText(); m_editingTextId=existing>=0 ? m_document.items()[existing].id : 0;
    const qreal textWidth=qMin(220.0,m_selection.width());
    const QPointF anchor(qBound(m_selection.left(),point.x(),m_selection.right()-textWidth),point.y());
    const QRectF bounds=existing>=0 ? m_document.items()[existing].bounds() : QRectF(anchor,QSizeF(textWidth,40));
    m_textAnchor=bounds.topLeft();
    m_textEditor=new QTextEdit(this); m_textEditor->setAcceptRichText(false);
    m_textEditor->setObjectName("annotationTextEditor");
    m_textEditor->setFrameShape(QFrame::NoFrame); m_textEditor->document()->setDocumentMargin(0);
    m_textEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEditor->setGeometry(bounds.toAlignedRect().intersected(rect()));
    QFont font=QApplication::font();
    if (existing>=0) {
        const auto &item=m_document.items()[existing];
        if (!item.fontFamily.isEmpty()) font.setFamily(item.fontFamily);
        m_fontSize=item.fontSize; m_fontWeight=item.fontWeight;
        const QSignalBlocker b1(m_fontControl), b2(m_fontWeightControl);
        m_fontControl->setValue(m_fontSize); m_fontWeightControl->setValue(m_fontWeight);
        m_fontValue->setText(QString::number(m_fontSize)); m_fontWeightValue->setText(QString::number(m_fontWeight));
    }
    font.setPixelSize(m_fontSize); font.setWeight(static_cast<QFont::Weight>(m_fontWeight)); m_textEditor->setFont(font);
    // 编辑已有文字时沿用该标注自身的颜色，避免被工具栏当前颜色污染。
    const QColor textColor=existing>=0 ? m_document.items()[existing].color : m_color;
    m_textColor = textColor;
    m_textEditor->setStyleSheet(QStringLiteral("QTextEdit{border:0;background:rgba(255,255,255,210);color:%1;}").arg(textColor.name()));
    if (existing>=0) m_textEditor->setPlainText(m_document.items()[existing].text);
    applyEditorTextStyle();
    connect(m_textEditor,&QTextEdit::textChanged,this,&CaptureOverlay::resizeTextEditor);
    resizeTextEditor();
    m_textEditor->installEventFilter(this); m_textEditor->show(); m_textEditor->raise(); m_textEditor->setFocus();
}
void CaptureOverlay::applyEditorTextStyle() {
    if (!m_textEditor) return;
    QFont font=m_textEditor->font();
    font.setPixelSize(m_fontSize);
    font.setWeight(static_cast<QFont::Weight>(m_fontWeight));
    m_textEditor->setFont(font);
    QTextCharFormat format;
    format.setFont(font);
    // 字号与字重调整不得改变文字颜色；颜色只在颜色面板被显式选中时改变。
    format.setForeground(m_textColor);
    QTextCursor documentCursor(m_textEditor->document());
    documentCursor.select(QTextCursor::Document);
    if (documentCursor.hasSelection()) documentCursor.mergeCharFormat(format);
    QTextCursor current=m_textEditor->textCursor();
    current.mergeCharFormat(format);
    m_textEditor->setCurrentCharFormat(format);
    m_textEditor->setTextCursor(current);
}
void CaptureOverlay::resizeTextEditor() {
    if (!m_textEditor) return;
    m_textEditor->document()->setTextWidth(m_textEditor->viewport()->width());
    const int wanted=qMax(1,qCeil(m_textEditor->document()->documentLayout()->documentSize().height())+2);
    const int height=qMin(qMax(24,wanted),qMax(1,qFloor(m_selection.height())));
    const int y=qRound(qBound(m_selection.top(),m_textAnchor.y(),m_selection.bottom()-height));
    QRect bounds=m_textEditor->geometry(); bounds.setY(y); bounds.setHeight(height);
    if (bounds!=m_textEditor->geometry()) m_textEditor->setGeometry(bounds);
}
void CaptureOverlay::resizeTextAnnotation(Annotation &annotation) const {
    QTextDocument document;
    configureTextDocument(document,annotation);
    const qreal height=qMin(qMax(24,qCeil(document.documentLayout()->documentSize().height())+2),qMax(1,qFloor(m_selection.height())));
    annotation.rect.moveTop(qBound(m_selection.top(),annotation.rect.top(),m_selection.bottom()-height));
    annotation.rect.setHeight(height);
}
void CaptureOverlay::commitText() {
    if (!m_textEditor) return;
    // 文字编辑会打断进行中的样式调整，先提交会话保证不丢失。
    commitStyleEdit();
    if (QGuiApplication::inputMethod()) QGuiApplication::inputMethod()->commit();
    QTextEdit *editor=m_textEditor; m_textEditor=nullptr;
    const QString text=editor->toPlainText(); const QRectF bounds=editor->geometry(); editor->removeEventFilter(this); editor->deleteLater();
    if (!text.trimmed().isEmpty()) {
        Annotation a; if (editingTextIndex()>=0) a=m_document.items()[editingTextIndex()];
        else { a.tool=Tool::Text; a.color=m_textColor; }
        a.rect=bounds; a.text=text; a.fontFamily=editor->font().family();
        a.fontSize=editor->font().pixelSize(); a.fontWeight=int(editor->font().weight());
        // 提交的颜色与编辑器预览保持一致。
        a.color=m_textColor;
        if (editingTextIndex()>=0) { m_document.replace(editingTextIndex(),a); m_selectedId=a.id; }
        else m_selectedId=m_document.add(a);
    } else if (editingTextIndex()>=0) m_document.remove(editingTextIndex());
    m_editingTextId=0; setFocus(); update();
}
bool CaptureOverlay::eventFilter(QObject *obj, QEvent *event) {
    if (obj==m_textEditor && event->type()==QEvent::KeyPress) {
        auto *key=static_cast<QKeyEvent *>(event);
        if (key->key()==Qt::Key_Escape) { cancelCapture(); return true; }
        if ((key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter) && key->modifiers().testFlag(Qt::ControlModifier)) { commitText(); return true; }
    }
    return QWidget::eventFilter(obj,event);
}
void CaptureOverlay::keyPressEvent(QKeyEvent *event) {
    if (event->key()==Qt::Key_Escape) {
        // 面板打开时 Esc 只收起面板，不取消截图。
        if (panelVisible()) { hidePanels(); return; }
        cancelCapture(); return;
    }
    if (event->matches(QKeySequence::Undo)) { commitStyleEdit(); m_document.undoStack()->undo(); return; }
    if (event->matches(QKeySequence::Redo)) { commitStyleEdit(); m_document.undoStack()->redo(); return; }
    if (event->key()==Qt::Key_Delete || event->key()==Qt::Key_Backspace) { if (selectedIndex()>=0) m_document.remove(selectedIndex()); return; }
    if (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) { finishCapture(); return; }
    QWidget::keyPressEvent(event);
}
void CaptureOverlay::showError(const QString &message) {
    if (!m_errorBanner) {
        m_errorBanner=new QFrame(this);
        m_errorBanner->setObjectName("copyErrorBanner");
        m_errorBanner->setStyleSheet(QStringLiteral("QFrame#copyErrorBanner{background:#fff3e0;border:1px solid #e8a14a;border-radius:6px;} QLabel{color:#6b3800;}"));
        auto *row=new QHBoxLayout(m_errorBanner);
        m_errorMessage=new QLabel(m_errorBanner);
        m_errorMessage->setObjectName("copyErrorMessage");
        m_errorMessage->setWordWrap(true);
        row->addWidget(m_errorMessage,1);
        auto *retry=new QToolButton(m_errorBanner);
        retry->setObjectName("retryButton");
        retry->setText(QStringLiteral("重试复制"));
        row->addWidget(retry);
        connect(retry,&QToolButton::clicked,this,&CaptureOverlay::finishCapture);
    }
    m_errorMessage->setText(message);
    const QRect host=uiScreenBounds();
    m_errorBanner->setMaximumWidth(qMax(1,host.width()-16));
    m_errorBanner->adjustSize();
    m_errorBanner->move(fitInsideScreen(QRect(host.topLeft(),m_errorBanner->size()),host).topLeft());
    m_errorBanner->show();
    m_errorBanner->raise();
    setFocus();
}
void CaptureOverlay::finishCapture() {
    if (m_ending || m_copying) return;
    m_copying=true;
    commitText();
    commitStyleEdit();
    if (m_settingsSaveTimer.isActive()) { m_settingsSaveTimer.stop(); saveStyleSettings(); }
    if (m_selection.isEmpty()) { m_copying=false; return; }
    // 导出走跨屏渲染：选区与标注都换算成虚拟桌面逻辑坐标。
    const QImage image=renderSelection(m_frame.screens,m_selection.translated(m_frame.virtualGeometry.topLeft()),virtualAnnotations());
    QString error;
    if (image.isNull() || !m_clipboardWriter(image,&error)) { m_copying=false; showError(error.isEmpty()?QStringLiteral("无法复制图像，请重试。") : error); return; }
    m_copying=false;
    endSession();
    close();
}
void CaptureOverlay::cancelCapture() { endSession(); close(); }
void CaptureOverlay::endSession() {
    if (m_ending) return;
    m_ending=true;
    commitStyleEdit();
    if (m_settingsSaveTimer.isActive()) { m_settingsSaveTimer.stop(); saveStyleSettings(); }
    hidePanels();
    if (m_textEditor) {
        m_textEditor->removeEventFilter(this);
        delete m_textEditor;
        m_textEditor=nullptr;
    }
    m_hasPreview=false;
    m_drag=Drag::None;
    m_caches.clear();
    m_frame.screens.clear();
    m_document.undoStack()->clear();
    m_document.setItemsForCommand({});
    emit finished();
}
void CaptureOverlay::closeEvent(QCloseEvent *event) {
    endSession();
    QWidget::closeEvent(event);
}
}
