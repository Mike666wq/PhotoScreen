#include "captureoverlay.h"
#include "core/geometry.h"
#include <QApplication>
#include <QButtonGroup>
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
class StyledSlider : public QSlider {
public:
    explicit StyledSlider(QWidget *parent=nullptr) : QSlider(Qt::Horizontal,parent) { setMinimumHeight(26); }
    // 轨道填充渐变（色相条、明度条用）；设置后不再画“已选部分”填充。
    void setGrooveGradient(const QLinearGradient &gradient) { m_gradient=gradient; m_hasGradient=true; update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        // 先清空，避免上一次绘制的轨道与手柄残留。
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(),Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setRenderHint(QPainter::Antialiasing);
        const int handleRadius=7, grooveHeight=4;
        const int travel=qMax(1,width()-2*handleRadius);
        const QRect groove(handleRadius,(height()-grooveHeight)/2,travel,grooveHeight);
        const int position=QStyle::sliderPositionFromValue(minimum(),maximum(),value(),travel,groove.width());
        // 轨道
        p.setPen(QPen(QColor("#9aa0a6"),1));
        p.setBrush(m_hasGradient ? QBrush(m_gradient) : QBrush(QColor("#dfe1e5")));
        p.drawRoundedRect(groove,2,2);
        // 已选部分
        if (!m_hasGradient && position>0) {
            p.setPen(Qt::NoPen); p.setBrush(QColor("#1687ef"));
            p.drawRoundedRect(QRect(groove.left(),groove.top(),position,groove.height()),2,2);
        }
        // 刻度
        if (tickPosition()!=NoTicks) {
            const int step=tickInterval()>0?tickInterval():qMax(1,(maximum()-minimum())/10);
            p.setPen(QPen(QColor("#8a8f96"),1));
            for (int v=minimum(); v<=maximum(); v+=step) {
                const int x=groove.left()+QStyle::sliderPositionFromValue(minimum(),maximum(),v,travel,groove.width());
                p.drawLine(x,groove.bottom()+4,x,groove.bottom()+8);
            }
        }
        // 手柄
        p.setPen(QPen(QColor("#5f6368"),1)); p.setBrush(QColor("#ffffff"));
        p.drawEllipse(QPoint(groove.left()+position,groove.center().y()),handleRadius,handleRadius);
    }
private:
    QLinearGradient m_gradient;
    bool m_hasGradient=false;
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
        "#ff2d55","#ff7a95","#cc0f36"
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
CaptureOverlay::CaptureOverlay(ScreenFrame frame, QScreen *screen, ClipboardWriter writer)
    : QWidget(nullptr), m_frame(std::move(frame)), m_clipboardWriter(writer ? std::move(writer) : ClipboardWriter(writeClipboard)), m_screen(screen) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(m_frame.screenGeometry);
    setCursor(Qt::CrossCursor);
    QSettings settings;
    m_color = colorFromSettings(settings.value("annotation/color"), QColor("#ff3b3b"));
    m_lineWidth = settings.value("annotation/width", 3).toInt();
    m_fontSize = settings.value("annotation/fontSize", 18).toInt();
    m_fontWeight = qBound(100,settings.value("annotation/fontWeight", 400).toInt(),900);
    buildToolbar();
    connect(m_document.undoStack(), &QUndoStack::indexChanged, this, [this] {
        m_selected = -1; m_undoButton->setEnabled(m_document.undoStack()->canUndo());
        m_redoButton->setEnabled(m_document.undoStack()->canRedo()); update();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) { cancelCapture(); });
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *) { cancelCapture(); });
    connect(screen, &QScreen::geometryChanged, this, [this](const QRect &) { cancelCapture(); });
    m_pointer = mapFromGlobal(QCursor::pos());
    m_hoverWindow = windowAt(m_pointer);
}
void CaptureOverlay::buildToolbar() {
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName("captureToolbar");
    m_toolbar->setStyleSheet("QFrame#captureToolbar{background:#f8f8f8;border:1px solid #aaa;border-radius:6px;}"
                            "QToolButton{padding:4px;border:0;border-radius:3px;color:#222;}"
                            "QToolButton:checked{background:#cde5ff;} QToolButton:hover{background:#e2eaf5;}");
    auto *layout = new QHBoxLayout(m_toolbar); layout->setContentsMargins(5,4,5,4); layout->setSpacing(2);
    auto *scroll=new QScrollArea(m_toolbar); scroll->setObjectName("toolScroll"); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 窄屏时工具区可横向滚动，滚动条始终可见，避免用户不知道可以横滚。
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll->setMinimumWidth(36); scroll->setFixedHeight(66); scroll->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    auto *toolArea=new QWidget; auto *toolsLayout=new QHBoxLayout(toolArea);
    toolsLayout->setContentsMargins(0,0,0,0); toolsLayout->setSpacing(2); layout->addWidget(scroll,1);
    auto *group = new QButtonGroup(m_toolbar); group->setExclusive(true);
    const QVector<QPair<QString,Tool>> tools = {{QStringLiteral("选择"),Tool::Select},{QStringLiteral("矩形"),Tool::Rectangle},
       {QStringLiteral("椭圆"),Tool::Ellipse},{QStringLiteral("箭头"),Tool::Arrow},{QStringLiteral("画笔"),Tool::Pen},
       {QStringLiteral("文字"),Tool::Text},{QStringLiteral("马赛克"),Tool::Mosaic}};
    for (const auto &entry : tools) {
        auto *button = new QToolButton(toolArea); button->setIcon(toolIcon(entry.second)); button->setIconSize(QSize(22,22));
        // 图标 + 中文文字标签：箭头与画笔不再只能靠 tooltip 区分。
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon); button->setText(entry.first);
        QFont labelFont=button->font(); labelFont.setPixelSize(11); button->setFont(labelFont);
        button->setToolTip(entry.first); button->setAccessibleName(entry.first); button->setCheckable(true);
        button->setObjectName(QStringLiteral("tool_%1").arg(int(entry.second)));
        group->addButton(button); toolsLayout->addWidget(button);
        if (entry.second == Tool::Select) button->setChecked(true);
        connect(button, &QToolButton::clicked, this, [this, tool=entry.second] { commitText(); commitStyleEdit(); hidePanels(); setTool(tool); });
    }
    m_colorButton = new QToolButton(toolArea); m_colorButton->setToolTip(QStringLiteral("颜色")); m_colorButton->setAccessibleName(QStringLiteral("颜色"));
    m_colorButton->setObjectName("colorButton"); toolsLayout->addWidget(m_colorButton); updateColorSwatch();
    connect(m_colorButton, &QToolButton::clicked, this, [this] { toggleColorPanel(); });
    m_widthButton=new QToolButton(toolArea); m_widthButton->setObjectName("widthButton");
    m_widthButton->setText(QStringLiteral("粗细 %1").arg(m_lineWidth)); m_widthButton->setToolTip(QStringLiteral("线宽 / 箭头粗细"));
    m_widthButton->setAccessibleName(QStringLiteral("线宽 / 箭头粗细")); toolsLayout->addWidget(m_widthButton);
    connect(m_widthButton, &QToolButton::clicked, this, [this] { toggleStylePanel(); });
    m_fontButton=new QToolButton(toolArea); m_fontButton->setObjectName("fontButton"); m_fontButton->setText(QStringLiteral("文字"));
    m_fontButton->setToolTip(QStringLiteral("文字大小和粗细")); m_fontButton->setAccessibleName(QStringLiteral("文字大小和粗细")); toolsLayout->addWidget(m_fontButton);
    connect(m_fontButton, &QToolButton::clicked, this, [this] { toggleStylePanel(); });
    buildStylePanel();
    buildColorPanel();
    connect(m_widthControl, &QSlider::valueChanged, this, [this](int n) {
        m_lineWidth=n; QSettings().setValue("annotation/width",n);
        m_widthButton->setText(QStringLiteral("粗细 %1").arg(n));
        if (m_textEditor) {
            applyEditorTextStyle();
        } else if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool!=Tool::Text) {
            const int i=m_selected; auto item=m_document.items()[i]; item.width=n;
            applyStyleChange(i,item);
        }
    });
    connect(m_fontControl, &QSlider::valueChanged, this, [this](int n) {
        m_fontSize=n; QSettings().setValue("annotation/fontSize",n);
        m_fontButton->setText(QStringLiteral("文字 %1").arg(n));
        if (m_textEditor) {
            applyEditorTextStyle(); resizeTextEditor();
        } else if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool==Tool::Text) {
            const int i=m_selected; auto item=m_document.items()[i]; item.fontSize=n; resizeTextAnnotation(item);
            applyStyleChange(i,item);
        }
    });
    connect(m_fontWeightControl, &QSlider::valueChanged, this, [this](int n) {
        const int snapped=qBound(100,((n+50)/100)*100,900);
        if (n!=snapped) { m_fontWeightControl->setValue(snapped); return; }
        m_fontWeight=n; QSettings().setValue("annotation/fontWeight",n);
        if (m_textEditor) {
            applyEditorTextStyle(); resizeTextEditor();
        } else if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool==Tool::Text) {
            const int i=m_selected; auto item=m_document.items()[i]; item.fontWeight=n; resizeTextAnnotation(item);
            applyStyleChange(i,item);
        }
    });
    // 滑块调整样式：按下时开启编辑会话，释放时提交一次，连续拖动只产生一步撤销。
    // 会话由 AnnotationDocument 管理，滑块丢失 grab 或覆盖层关闭时也不会泄漏到撤销栈。
    for (QSlider *slider : {m_widthControl,m_fontControl,m_fontWeightControl}) {
        connect(slider,&QSlider::sliderPressed,this,[this,slider] {
            if (m_textEditor) return;
            if (m_selected<0 || m_selected>=m_document.items().size()) return;
            const bool applies=(slider==m_widthControl) != (m_document.items()[m_selected].tool==Tool::Text);
            if (!applies) return;
            m_document.beginEdit();
        });
        connect(slider,&QSlider::sliderReleased,this,[this] { commitStyleEdit(); });
    }
    toolArea->adjustSize(); scroll->setWidget(toolArea); scroll->setMaximumWidth(toolArea->sizeHint().width()+2);
    // 撤销/重做改用文字：符号在部分字体下缺字形，渲染不一致。
    m_undoButton = new QToolButton(m_toolbar); m_undoButton->setObjectName("undoButton"); m_undoButton->setText(QStringLiteral("撤销")); m_undoButton->setToolTip(QStringLiteral("撤销")); m_undoButton->setAccessibleName(QStringLiteral("撤销")); m_undoButton->setEnabled(false); layout->addWidget(m_undoButton);
    m_redoButton = new QToolButton(m_toolbar); m_redoButton->setObjectName("redoButton"); m_redoButton->setText(QStringLiteral("重做")); m_redoButton->setToolTip(QStringLiteral("重做")); m_redoButton->setAccessibleName(QStringLiteral("重做")); m_redoButton->setEnabled(false); layout->addWidget(m_redoButton);
    connect(m_undoButton, &QToolButton::clicked, this, [this] { commitText(); commitStyleEdit(); m_document.undoStack()->undo(); });
    connect(m_redoButton, &QToolButton::clicked, this, [this] { commitText(); commitStyleEdit(); m_document.undoStack()->redo(); });
    auto *cancel = new QToolButton(m_toolbar); cancel->setObjectName("cancelButton"); cancel->setText(QStringLiteral("×")); cancel->setToolTip(QStringLiteral("取消")); cancel->setAccessibleName(QStringLiteral("取消")); layout->addWidget(cancel);
    auto *done = new QToolButton(m_toolbar); done->setObjectName("doneButton"); done->setText(QStringLiteral("✓")); done->setToolTip(QStringLiteral("完成")); done->setAccessibleName(QStringLiteral("完成")); layout->addWidget(done);
    connect(cancel, &QToolButton::clicked, this, &CaptureOverlay::cancelCapture);
    connect(done, &QToolButton::clicked, this, &CaptureOverlay::finishCapture);
    m_toolbar->adjustSize(); m_toolbar->hide();
}
void CaptureOverlay::buildStylePanel() {
    // 参数面板是覆盖层的普通子控件，不是原生菜单：
    // macOS 的 NSMenu 会接管鼠标，内嵌滑块拿不到可靠的拖拽事件。
    m_stylePanel=new QFrame(this);
    m_stylePanel->setObjectName("stylePanel");
    m_stylePanel->setStyleSheet("QFrame#stylePanel{background:#f8f8f8;border:1px solid #aaa;border-radius:6px;}"
                                "QLabel{color:#222;}");
    auto *layout=new QVBoxLayout(m_stylePanel);
    layout->setContentsMargins(12,10,12,10); layout->setSpacing(6);

    auto *widthRow=new QHBoxLayout;
    m_widthControl=addStyledSlider(widthRow,QStringLiteral("粗细"),1,20,m_lineWidth,&m_widthValue);
    m_widthControl->setObjectName("widthControl"); m_widthValue->setObjectName("widthValue");
    layout->addLayout(widthRow);

    auto *fontRow=new QHBoxLayout;
    m_fontControl=addStyledSlider(fontRow,QStringLiteral("字号"),10,72,m_fontSize,&m_fontValue);
    m_fontControl->setObjectName("fontControl"); m_fontValue->setObjectName("fontValue");
    layout->addLayout(fontRow);

    auto *weightRow=new QHBoxLayout;
    m_fontWeightControl=addStyledSlider(weightRow,QStringLiteral("字重"),100,900,m_fontWeight,&m_fontWeightValue);
    m_fontWeightControl->setObjectName("fontWeightControl"); m_fontWeightValue->setObjectName("fontWeightValue");
    m_fontWeightControl->setSingleStep(100); m_fontWeightControl->setPageStep(100);
    m_fontWeightControl->setTickPosition(QSlider::TicksBelow); m_fontWeightControl->setTickInterval(100);
    layout->addLayout(weightRow);

    m_stylePanel->hide();
}
void CaptureOverlay::toggleStylePanel() {
    if (m_stylePanel->isVisible()) { hidePanels(); return; }
    hidePanels();
    m_stylePanel->show();
    // 面板默认贴在工具栏上沿外侧，避免压住撤销/重做/取消/完成。
    const QPoint anchor=m_toolbar->mapTo(this,QPoint(m_toolbar->width()/2,0));
    positionPanel(m_stylePanel,anchor,true);
}
void CaptureOverlay::hidePanels() {
    if (m_stylePanel) m_stylePanel->hide();
    if (m_colorPanel) m_colorPanel->hide();
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
    auto *layout=new QVBoxLayout(m_colorPanel);
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

    auto *hueRow=new QHBoxLayout;
    auto *hueName=new QLabel(QStringLiteral("色相"));
    m_hueControl=new StyledSlider; m_hueControl->setRange(0,360); m_hueControl->setValue(0);
    m_hueControl->setObjectName("hueControl"); m_hueControl->setAccessibleName(QStringLiteral("色相"));
    QLinearGradient hue; hue.setCoordinateMode(QGradient::ObjectBoundingMode);
    hue.setColorAt(0.00,QColor("#ff0000")); hue.setColorAt(0.17,QColor("#ffff00"));
    hue.setColorAt(0.33,QColor("#00ff00")); hue.setColorAt(0.50,QColor("#00ffff"));
    hue.setColorAt(0.67,QColor("#0000ff")); hue.setColorAt(0.83,QColor("#ff00ff"));
    hue.setColorAt(1.00,QColor("#ff0000"));
    setSliderGradient(m_hueControl,hue);
    hueRow->addWidget(hueName); hueRow->addWidget(m_hueControl,1);
    layout->addLayout(hueRow);

    auto *valueRow=new QHBoxLayout;
    auto *valueName=new QLabel(QStringLiteral("明度"));
    m_valueControl=new StyledSlider; m_valueControl->setRange(0,100); m_valueControl->setValue(100);
    m_valueControl->setObjectName("valueControl"); m_valueControl->setAccessibleName(QStringLiteral("明度"));
    valueRow->addWidget(valueName); valueRow->addWidget(m_valueControl,1);
    layout->addLayout(valueRow);

    m_colorPreview=new QToolButton(m_colorPanel);
    m_colorPreview->setObjectName("colorPreview");
    m_colorPreview->setFixedHeight(24);
    m_colorPreview->setToolTip(QStringLiteral("使用当前颜色"));
    m_colorPreview->setAccessibleName(QStringLiteral("颜色预览"));
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

    connect(m_hueControl,&QSlider::valueChanged,this,[this](int) { updateColorPreview(); });
    connect(m_valueControl,&QSlider::valueChanged,this,[this](int) { updateColorPreview(); });
    connect(m_hueControl,&QSlider::sliderReleased,this,[this] { applyChosenColor(previewColor()); });
    connect(m_valueControl,&QSlider::sliderReleased,this,[this] { applyChosenColor(previewColor()); });
    connect(m_colorPreview,&QToolButton::clicked,this,[this] { applyChosenColor(previewColor()); });

    updateColorPreview();
    updateRecentButtons();
    updatePresetHighlight();
    m_colorPanel->hide();
}
void CaptureOverlay::toggleColorPanel() {
    if (m_colorPanel->isVisible()) { hidePanels(); return; }
    hidePanels();
    m_colorPanel->show();
    updateColorPreview();
    updateRecentButtons();
    updatePresetHighlight();
    const QPoint anchor=m_colorButton->mapTo(this,QPoint(m_colorButton->width()/2,m_colorButton->height()));
    positionPanel(m_colorPanel,anchor,false);
}
QColor CaptureOverlay::previewColor() const {
    const int h=m_hueControl ? m_hueControl->value() : 0;
    const int v=m_valueControl ? m_valueControl->value() : 100;
    return QColor::fromHsvF(qBound(0,h,360)/360.0, 1.0, qBound(0,v,100)/100.0);
}
void CaptureOverlay::updateColorPreview() {
    if (!m_colorPreview) return;
    const QColor color=previewColor();
    m_colorPreview->setStyleSheet(QStringLiteral("QToolButton#colorPreview{background:%1;border:1px solid #999;border-radius:3px;}").arg(color.name()));
    if (m_valueControl && m_hueControl) {
        // 明度条：白 → 当前色相的纯色。
        QLinearGradient gradient; gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
        gradient.setColorAt(0.0,QColor(Qt::white));
        gradient.setColorAt(1.0,QColor::fromHsvF(m_hueControl->value()/360.0,1.0,1.0));
        setSliderGradient(m_valueControl,gradient);
    }
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
void CaptureOverlay::positionPanel(QFrame *panel, const QPoint &anchor, bool preferAbove) {
    // 上次定位留下的最大高度限制要先清掉，否则面板无法恢复原尺寸。
    panel->setMaximumHeight(QWIDGETSIZE_MAX);
    panel->adjustSize();
    const QRect bounds=rect();
    const QRect toolbarRect=m_toolbar->geometry();
    const int gap=6;
    // 工具栏所占的横带是禁区：面板只能整块落在工具栏上方或下方，
    // 候选位置直接相对工具栏计算，不再依赖锚点，跨平台结果一致。
    const int aboveY=toolbarRect.top()-gap-panel->height();
    const int belowY=toolbarRect.bottom()+1+gap;
    const bool aboveFits=aboveY>=0 && aboveY+panel->height()<=bounds.height();
    const bool belowFits=belowY>=0 && belowY+panel->height()<=bounds.height();
    int y=-1;
    if (preferAbove ? aboveFits : belowFits) y=preferAbove?aboveY:belowY;
    else if (preferAbove ? belowFits : aboveFits) y=preferAbove?belowY:aboveY;
    if (y<0) {
        // 两侧都放不下时，先压缩面板高度再放进较空的一侧，保证不压住工具栏。
        const int room=qMax(toolbarRect.top()-gap, bounds.height()-(toolbarRect.bottom()+1)-gap);
        if (room>0 && room<panel->height() && room>=panel->minimumSizeHint().height()) {
            panel->setMaximumHeight(room);
            panel->adjustSize();
        }
        // 面板高度变了，候选位置要按新高度重算。
        y=preferAbove ? toolbarRect.top()-gap-panel->height() : toolbarRect.bottom()+1+gap;
        y=qBound(0,y,qMax(0,bounds.height()-panel->height()));
    }
    const int x=qBound(0,anchor.x()-panel->width()/2,qMax(0,bounds.width()-panel->width()));
    panel->move(x,y);
    panel->raise();
}
void CaptureOverlay::updateColorSwatch() {
    if (!m_colorButton) return;
    QPixmap swatch(20,20); swatch.fill(Qt::transparent); QPainter p(&swatch);
    p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(QColor("#666666"),1)); p.setBrush(m_color); p.drawEllipse(QRectF(3,3,14,14));
    m_colorButton->setIcon(QIcon(swatch)); m_colorButton->setIconSize(QSize(20,20));
}
void CaptureOverlay::applyChosenColor(const QColor &color) {
    if (!color.isValid()) return;
    m_color=color; QSettings().setValue("annotation/color",color.name()); updateColorSwatch();
    if (m_textEditor) {
        // 显式改色才更新正在编辑的文字颜色，颜色与预览始终保持一致。
        m_textColor=color;
        m_textEditor->setStyleSheet(QStringLiteral("QTextEdit{border:0;background:rgba(255,255,255,210);color:%1;}").arg(color.name()));
        applyEditorTextStyle();
    } else if (m_selected>=0 && m_selected<m_document.items().size()) {
        const int i=m_selected; auto item=m_document.items()[i]; item.color=color; m_document.replace(i,item); m_selected=i; update();
    }
    rememberColor(color);
    updatePresetHighlight();
}
void CaptureOverlay::positionToolbar() {
    if (m_selection.isEmpty()) { m_toolbar->hide(); return; }
    m_toolbar->setMaximumWidth(qMax(1,width()-4)); m_toolbar->adjustSize();
    const int x = qBound(0, qRound(m_selection.left()), qMax(0,width()-m_toolbar->width()));
    int y = qRound(m_selection.bottom()) + 8;
    if (y + m_toolbar->height() > height()) y = qRound(m_selection.top()) - m_toolbar->height() - 8;
    y = qBound(0, y, qMax(0,height()-m_toolbar->height()));
    m_toolbar->move(x,y); m_toolbar->show(); m_toolbar->raise();
}
void CaptureOverlay::applyStyleChange(int index, const Annotation &item) {
    // 拖动中（会话已开启）就地修改，释放时统一提交为一步撤销；
    // 键盘或滚轮调整没有会话，立即提交为一步撤销。
    const bool editing = m_document.editing();
    if (!editing) m_document.beginEdit();
    m_document.setItemInPlace(index, item);
    m_selected = index;
    if (!editing) commitStyleEdit();
    update();
}
void CaptureOverlay::commitStyleEdit() {
    if (!m_document.editing()) return;
    const int selected = m_selected;
    m_document.endEdit();
    m_selected = selected;
    update();
}
void CaptureOverlay::setTool(Tool tool) { m_tool=tool; m_selected=-1; setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor); update(); }
QPointF CaptureOverlay::boundedPoint(const QPointF &p) const { return {qBound(0.0,p.x(),double(width())), qBound(0.0,p.y(),double(height()))}; }
QRectF CaptureOverlay::windowAt(const QPointF &p) const {
    const QPoint global = m_frame.screenGeometry.topLeft() + p.toPoint();
    for (const QRect &window : m_frame.windowRects) {
        if (window.contains(global)) return window.intersected(m_frame.screenGeometry).translated(-m_frame.screenGeometry.topLeft());
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
            if (m_selected>=0 && m_selected<items.size()) items[m_selected]=m_preview;
        } else items.append(m_preview);
    }
    return items;
}
void CaptureOverlay::ensureRenderCache() {
    const QRect crop=pixelRect(m_selection,size(),m_frame.image.size());
    if (crop.isEmpty()) { m_baseCache=QImage(); m_annoCache=QImage(); return; }
    if (m_cacheSelection!=m_selection || m_cacheRect!=crop) {
        m_cacheSelection=m_selection; m_cacheRect=crop;
        // 底图只在选区变化时重新裁剪：拖动标注时不再每帧分配并复制整块选区。
        m_baseCache=cropPixels(m_frame.image,size(),m_selection);
        ++m_cropCount;
        m_annoCache=QImage(m_baseCache.size(),QImage::Format_ARGB32_Premultiplied);
    }
    if (m_annoCache.isNull()) m_annoCache=QImage(m_baseCache.size(),QImage::Format_ARGB32_Premultiplied);
    m_annoCache.fill(Qt::transparent);
    const qreal scaleX=m_frame.image.width()/qMax<qreal>(1,size().width());
    const qreal scaleY=m_frame.image.height()/qMax<qreal>(1,size().height());
    renderAnnotationLayer(m_annoCache,m_baseCache,crop,scaleX,scaleY,previewItems());
}
void CaptureOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(rect(),m_frame.image);
    p.fillRect(rect(),QColor(0,0,0,110));
    if (!m_selection.isEmpty()) {
        ensureRenderCache();
        if (!m_baseCache.isNull()) { p.drawImage(m_selection,m_baseCache); p.drawImage(m_selection,m_annoCache); }
        p.setPen(QPen(QColor("#1687ef"),2)); p.setBrush(Qt::NoBrush); p.drawRect(m_selection);
        p.setBrush(Qt::white);
        for (const auto &point : handles(m_selection)) p.drawRect(handleRect(point));
        if (m_selected>=0 && m_selected<m_document.items().size()) {
            const QRectF box=(m_hasPreview ? m_preview : m_document.items()[m_selected]).bounds();
            p.setPen(QPen(QColor("#1687ef"),1,Qt::DashLine)); p.setBrush(Qt::NoBrush); p.drawRect(box);
            p.setBrush(Qt::white);
            const Annotation &a=(m_hasPreview ? m_preview : m_document.items()[m_selected]);
            if (a.tool==Tool::Arrow) { p.drawRect(handleRect(a.rect.topLeft())); p.drawRect(handleRect(a.rect.bottomRight())); }
            else p.drawRect(handleRect(box.bottomRight()));
        }
        const QRect pixels=pixelRect(m_selection,size(),m_frame.image.size());
        p.setPen(Qt::white); p.setBrush(QColor(0,0,0,180));
        const QString label=QStringLiteral("%1 × %2 px").arg(pixels.width()).arg(pixels.height());
        QRectF l(m_selection.left(),qMax(0.0,m_selection.top()-24),p.fontMetrics().horizontalAdvance(label)+12,22);
        p.drawRoundedRect(l,3,3); p.drawText(l,Qt::AlignCenter,label);
    } else if (!m_hoverWindow.isEmpty()) {
        p.setPen(QPen(QColor("#1687ef"),2)); p.setBrush(Qt::NoBrush); p.drawRect(m_hoverWindow);
    }
    if (m_selection.isEmpty() || m_drag==Drag::NewSelection) {
        const qreal sx=double(m_frame.image.width())/width(), sy=double(m_frame.image.height())/height();
        const int px=qBound(0,int(m_pointer.x()*sx),m_frame.image.width()-1), py=qBound(0,int(m_pointer.y()*sy),m_frame.image.height()-1);
        const QColor c=m_frame.image.pixelColor(px,py);
        const QImage sample=magnifierSample(m_frame.image,QPoint(px,py));
        const int x=qBound(0,int(m_pointer.x())+18,qMax(0,width()-150)), y=qBound(0,int(m_pointer.y())+18,qMax(0,height()-148));
        const QRect box(x,y,146,144); p.fillRect(box,QColor(20,20,20,230));
        p.drawImage(QRect(x+19,y+4,108,108),sample);
        p.setPen(Qt::red); p.drawLine(x+73,y+49,x+73,y+67); p.drawLine(x+64,y+58,x+82,y+58);
        p.setPen(Qt::white); p.drawText(QRect(x+4,y+115,138,24),Qt::AlignCenter,QStringLiteral("%1, %2  %3").arg(px).arg(py).arg(c.name().toUpper()));
    }
}
void CaptureOverlay::clearSelection() {
    // 只清空选区与工具栏，保留已画的标注和撤销历史。
    // 样式编辑会话必须先提交，否则会话会残留在文档里。
    commitStyleEdit();
    m_selection = {};
    m_selected = -1;
    m_hoverWindow = {};
    m_toolbar->hide();
    update();
}
void CaptureOverlay::mousePressEvent(QMouseEvent *event) {
    // 右键：清空选区重新框选，但保留已画的标注与撤销历史。
    if (event->button()==Qt::RightButton) { if (m_selection.isEmpty()) cancelCapture(); else { commitText(); clearSelection(); } return; }
    if (event->button()!=Qt::LeftButton) return;
    // 点到画布就收起参数面板。
    hidePanels();
    commitText(); m_press=boundedPoint(event->position()); m_pointer=m_press;
    if (m_selection.isEmpty()) { m_drag=Drag::NewSelection; m_startRect={}; return; }
    if (m_tool==Tool::Select) {
        if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool==Tool::Arrow) {
            const auto &arrow=m_document.items()[m_selected];
            if (handleRect(arrow.rect.topLeft()).contains(m_press) || handleRect(arrow.rect.bottomRight()).contains(m_press)) {
                m_drag=handleRect(arrow.rect.topLeft()).contains(m_press) ? Drag::ArrowStart : Drag::ArrowEnd;
                m_startAnnotation=arrow; return;
            }
        }
        if (m_selected>=0 && m_selected<m_document.items().size() && handleRect(m_document.items()[m_selected].bounds().bottomRight()).contains(m_press)) {
            m_drag=Drag::ResizeObject; m_startAnnotation=m_document.items()[m_selected]; m_startRect=m_startAnnotation.bounds(); return;
        }
        m_handle=selectionHandleAt(m_press);
        if (m_handle>=0) { m_selected=-1; m_startRect=m_selection; m_drag=Drag::ResizeSelection; update(); return; }
        const int object=objectAt(m_press);
        if (object>=0) {
            m_selected=object; m_startAnnotation=m_document.items()[object]; m_color=m_startAnnotation.color; updateColorSwatch();
            const QSignalBlocker b1(m_widthControl), b2(m_fontControl), b3(m_fontWeightControl);
            if (m_startAnnotation.tool==Tool::Text) {
                m_fontSize=m_startAnnotation.fontSize; m_fontWeight=m_startAnnotation.fontWeight;
                m_fontControl->setValue(m_fontSize); m_fontWeightControl->setValue(m_fontWeight);
                m_fontValue->setText(QString::number(m_fontSize)); m_fontWeightValue->setText(QString::number(m_fontWeight));
            } else {
                m_lineWidth=qRound(m_startAnnotation.width); m_widthControl->setValue(m_lineWidth);
                m_widthValue->setText(QString::number(m_lineWidth)); m_widthButton->setText(QStringLiteral("粗细 %1").arg(m_lineWidth));
            }
            m_drag=Drag::MoveObject; update(); return;
        }
        m_selected=-1; m_startRect=m_selection;
        if (m_selection.contains(m_press)) m_drag=Drag::MoveSelection;
        else { clearSelection(); m_drag=Drag::NewSelection; }
    } else if (m_selection.contains(m_press)) {
        if (m_tool==Tool::Text) { startText(m_press); return; }
        m_drag=Drag::Draw; m_preview={}; m_preview.tool=m_tool; m_preview.color=m_color; m_preview.width=m_lineWidth;
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
    if (!m_selection.isEmpty() && m_drag!=Drag::NewSelection) positionToolbar();
    update();
}
void CaptureOverlay::mouseMoveEvent(QMouseEvent *event) { updateDrag(event->position()); }
void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button()!=Qt::LeftButton) return;
    if (m_drag!=Drag::None) updateDrag(event->position());
    if (m_drag==Drag::NewSelection) {
        if (QLineF(m_press,m_pointer).length()<3) m_selection=windowAt(m_press);
        if (m_selection.width()<2 || m_selection.height()<2) m_selection={};
        positionToolbar();
    } else if (m_drag==Drag::Draw && m_hasPreview) {
        if (m_preview.tool==Tool::Pen || (m_preview.tool==Tool::Arrow && QLineF(m_preview.rect.topLeft(),m_preview.rect.bottomRight()).length()>=2) ||
            (m_preview.rect.normalized().width()>=2 && m_preview.rect.normalized().height()>=2)) m_document.add(m_preview);
    } else if ((m_drag==Drag::MoveObject || m_drag==Drag::ResizeObject || m_drag==Drag::ArrowStart || m_drag==Drag::ArrowEnd) && m_hasPreview && QLineF(m_press,m_pointer).length()>=0.5) {
        const int selected=m_selected; m_document.replace(selected,m_preview); m_selected=selected;
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
    commitText(); m_editingText=existing;
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
        Annotation a; if (m_editingText>=0 && m_editingText<m_document.items().size()) a=m_document.items()[m_editingText];
        else { a.tool=Tool::Text; a.color=m_textColor; }
        a.rect=bounds; a.text=text; a.fontFamily=editor->font().family();
        a.fontSize=editor->font().pixelSize(); a.fontWeight=int(editor->font().weight());
        // 提交的颜色与编辑器预览保持一致。
        a.color=m_textColor;
        if (m_editingText>=0) m_document.replace(m_editingText,a); else m_document.add(a);
    } else if (m_editingText>=0) m_document.remove(m_editingText);
    m_editingText=-1; setFocus(); update();
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
    if (event->key()==Qt::Key_Delete || event->key()==Qt::Key_Backspace) { if (m_selected>=0) m_document.remove(m_selected); return; }
    if (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) { finishCapture(); return; }
    QWidget::keyPressEvent(event);
}
void CaptureOverlay::showError(const QString &message) { QMessageBox::warning(this,QStringLiteral("复制失败"),message); activateWindow(); setFocus(); }
void CaptureOverlay::finishCapture() {
    commitText();
    commitStyleEdit();
    if (m_selection.isEmpty()) return;
    const QImage image=renderSelection(m_frame.image,size(),m_selection,m_document.items()); QString error;
    if (image.isNull() || !m_clipboardWriter(image,&error)) { showError(error.isEmpty()?QStringLiteral("无法复制图像，请重试。") : error); return; }
    emit finished(); close();
}
void CaptureOverlay::cancelCapture() { commitStyleEdit(); emit finished(); close(); }
}
