#include "captureoverlay.h"
#include "core/geometry.h"
#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QInputMethod>
#include <QLabel>
#include <QMenu>
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
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
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
QSlider *addSlider(QVBoxLayout *layout, const QString &title, int minimum, int maximum, int value, QLabel **valueLabel) {
    auto *heading=new QHBoxLayout;
    auto *name=new QLabel(title); auto *number=new QLabel(QString::number(value));
    number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    heading->addWidget(name); heading->addStretch(); heading->addWidget(number);
    layout->addLayout(heading);
    auto *slider=new QSlider(Qt::Horizontal); slider->setRange(minimum,maximum);
    slider->setValue(value); slider->setAccessibleName(title); slider->setMinimumWidth(170);
    layout->addWidget(slider); *valueLabel=number;
    QObject::connect(slider,&QSlider::valueChanged,number,[number](int n) { number->setText(QString::number(n)); });
    return slider;
}
QVBoxLayout *sliderMenu(QToolButton *button) {
    auto *menu=new QMenu(button);
    auto *panel=new QWidget(menu);
    auto *layout=new QVBoxLayout(panel); layout->setContentsMargins(12,10,12,10); layout->setSpacing(5);
    auto *action=new QWidgetAction(menu); action->setDefaultWidget(panel); menu->addAction(action);
    button->setMenu(menu); button->setPopupMode(QToolButton::InstantPopup);
    return layout;
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
    m_color = settings.value("annotation/color", QColor("#ff3b3b")).value<QColor>();
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
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setMinimumWidth(36); scroll->setFixedHeight(48); scroll->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    auto *toolArea=new QWidget; auto *toolsLayout=new QHBoxLayout(toolArea);
    toolsLayout->setContentsMargins(0,0,0,0); toolsLayout->setSpacing(2); layout->addWidget(scroll,1);
    auto *group = new QButtonGroup(m_toolbar); group->setExclusive(true);
    const QVector<QPair<QString,Tool>> tools = {{QStringLiteral("选择"),Tool::Select},{QStringLiteral("矩形"),Tool::Rectangle},
       {QStringLiteral("椭圆"),Tool::Ellipse},{QStringLiteral("箭头"),Tool::Arrow},{QStringLiteral("画笔"),Tool::Pen},
       {QStringLiteral("文字"),Tool::Text},{QStringLiteral("马赛克"),Tool::Mosaic}};
    for (const auto &entry : tools) {
        auto *button = new QToolButton(toolArea); button->setIcon(toolIcon(entry.second)); button->setIconSize(QSize(22,22));
        button->setToolTip(entry.first); button->setAccessibleName(entry.first); button->setCheckable(true);
        button->setObjectName(QStringLiteral("tool_%1").arg(int(entry.second)));
        group->addButton(button); toolsLayout->addWidget(button);
        if (entry.second == Tool::Select) button->setChecked(true);
        connect(button, &QToolButton::clicked, this, [this, tool=entry.second] { commitText(); setTool(tool); });
    }
    m_colorButton = new QToolButton(toolArea); m_colorButton->setToolTip(QStringLiteral("颜色")); m_colorButton->setAccessibleName(QStringLiteral("颜色"));
    m_colorButton->setObjectName("colorButton"); toolsLayout->addWidget(m_colorButton); updateColorSwatch();
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(m_color, this, QStringLiteral("标注颜色"));
        if (chosen.isValid()) {
            m_color=chosen; QSettings().setValue("annotation/color", chosen); updateColorSwatch();
            if (m_textEditor) {
                m_textEditor->setStyleSheet(QStringLiteral("QTextEdit{border:0;background:rgba(255,255,255,210);color:%1;}").arg(m_color.name()));
                applyEditorTextStyle();
            } else if (m_selected>=0 && m_selected<m_document.items().size()) {
                const int i=m_selected; auto item=m_document.items()[i]; item.color=chosen; m_document.replace(i,item); m_selected=i; update();
            }
        }
    });
    m_widthButton=new QToolButton(toolArea); m_widthButton->setObjectName("widthButton");
    m_widthButton->setText(QStringLiteral("粗细 %1").arg(m_lineWidth)); m_widthButton->setToolTip(QStringLiteral("线宽 / 箭头粗细"));
    m_widthButton->setAccessibleName(QStringLiteral("线宽 / 箭头粗细")); toolsLayout->addWidget(m_widthButton);
    m_widthControl=addSlider(sliderMenu(m_widthButton),QStringLiteral("线宽 / 箭头粗细"),1,20,m_lineWidth,&m_widthValue);
    m_widthControl->setObjectName("widthControl"); m_widthValue->setObjectName("widthValue");
    connect(m_widthControl, &QSlider::valueChanged, this, [this](int n) {
        m_lineWidth=n; QSettings().setValue("annotation/width",n);
        m_widthButton->setText(QStringLiteral("粗细 %1").arg(n));
        if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool!=Tool::Text) {
            const int i=m_selected; auto item=m_document.items()[i]; item.width=n; m_document.replace(i,item); m_selected=i; update();
        }
    });
    m_fontButton=new QToolButton(toolArea); m_fontButton->setObjectName("fontButton"); m_fontButton->setText(QStringLiteral("文字"));
    m_fontButton->setToolTip(QStringLiteral("文字大小和粗细")); m_fontButton->setAccessibleName(QStringLiteral("文字大小和粗细")); toolsLayout->addWidget(m_fontButton);
    auto *fontLayout=sliderMenu(m_fontButton);
    m_fontControl=addSlider(fontLayout,QStringLiteral("字号"),10,72,m_fontSize,&m_fontValue);
    m_fontControl->setObjectName("fontControl"); m_fontValue->setObjectName("fontValue");
    m_fontWeightControl=addSlider(fontLayout,QStringLiteral("字体粗细"),100,900,m_fontWeight,&m_fontWeightValue);
    m_fontWeightControl->setObjectName("fontWeightControl"); m_fontWeightValue->setObjectName("fontWeightValue");
    m_fontWeightControl->setSingleStep(100); m_fontWeightControl->setPageStep(100);
    m_fontWeightControl->setTickPosition(QSlider::TicksBelow); m_fontWeightControl->setTickInterval(100);
    connect(m_fontControl, &QSlider::valueChanged, this, [this](int n) {
        m_fontSize=n; QSettings().setValue("annotation/fontSize",n);
        if (m_textEditor) {
            applyEditorTextStyle(); resizeTextEditor();
        } else if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool==Tool::Text) {
            const int i=m_selected; auto item=m_document.items()[i]; item.fontSize=n; resizeTextAnnotation(item); m_document.replace(i,item); m_selected=i; update();
        }
    });
    connect(m_fontWeightControl, &QSlider::valueChanged, this, [this](int n) {
        const int snapped=qBound(100,((n+50)/100)*100,900);
        if (n!=snapped) { m_fontWeightControl->setValue(snapped); return; }
        m_fontWeight=n; QSettings().setValue("annotation/fontWeight",n);
        if (m_textEditor) {
            applyEditorTextStyle(); resizeTextEditor();
        } else if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool==Tool::Text) {
            const int i=m_selected; auto item=m_document.items()[i]; item.fontWeight=n; resizeTextAnnotation(item); m_document.replace(i,item); m_selected=i; update();
        }
    });
    for (QSlider *slider : {m_widthControl,m_fontControl,m_fontWeightControl}) {
        auto dragging=std::make_shared<bool>(false);
        connect(slider,&QSlider::sliderPressed,this,[this,slider,dragging] {
            if (!m_textEditor && m_selected>=0 && m_selected<m_document.items().size()) {
                const bool applies=(slider==m_widthControl) != (m_document.items()[m_selected].tool==Tool::Text);
                if (!applies) return;
                m_document.undoStack()->beginMacro(QStringLiteral("调整样式")); *dragging=true;
            }
        });
        connect(slider,&QSlider::sliderReleased,this,[this,dragging] {
            if (*dragging) {
                const int selected=m_selected; m_document.undoStack()->endMacro(); m_selected=selected;
                *dragging=false; update();
            }
        });
    }
    toolArea->adjustSize(); scroll->setWidget(toolArea); scroll->setMaximumWidth(toolArea->sizeHint().width()+2);
    m_undoButton = new QToolButton(m_toolbar); m_undoButton->setObjectName("undoButton"); m_undoButton->setText(QStringLiteral("↶")); m_undoButton->setToolTip(QStringLiteral("撤销")); m_undoButton->setEnabled(false); layout->addWidget(m_undoButton);
    m_redoButton = new QToolButton(m_toolbar); m_redoButton->setObjectName("redoButton"); m_redoButton->setText(QStringLiteral("↷")); m_redoButton->setToolTip(QStringLiteral("重做")); m_redoButton->setEnabled(false); layout->addWidget(m_redoButton);
    connect(m_undoButton, &QToolButton::clicked, this, [this] { commitText(); m_document.undoStack()->undo(); });
    connect(m_redoButton, &QToolButton::clicked, this, [this] { commitText(); m_document.undoStack()->redo(); });
    auto *cancel = new QToolButton(m_toolbar); cancel->setObjectName("cancelButton"); cancel->setText(QStringLiteral("×")); cancel->setToolTip(QStringLiteral("取消")); cancel->setAccessibleName(QStringLiteral("取消")); layout->addWidget(cancel);
    auto *done = new QToolButton(m_toolbar); done->setObjectName("doneButton"); done->setText(QStringLiteral("✓")); done->setToolTip(QStringLiteral("完成")); done->setAccessibleName(QStringLiteral("完成")); layout->addWidget(done);
    connect(cancel, &QToolButton::clicked, this, &CaptureOverlay::cancelCapture);
    connect(done, &QToolButton::clicked, this, &CaptureOverlay::finishCapture);
    m_toolbar->adjustSize(); m_toolbar->hide();
}
void CaptureOverlay::updateColorSwatch() {
    if (!m_colorButton) return;
    QPixmap swatch(20,20); swatch.fill(Qt::transparent); QPainter p(&swatch);
    p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(QColor("#666666"),1)); p.setBrush(m_color); p.drawEllipse(QRectF(3,3,14,14));
    m_colorButton->setIcon(QIcon(swatch)); m_colorButton->setIconSize(QSize(20,20));
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
void CaptureOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(rect(),m_frame.image);
    p.fillRect(rect(),QColor(0,0,0,110));
    if (!m_selection.isEmpty()) {
        const QImage selected=renderSelection(m_frame.image,size(),m_selection,previewItems());
        if (!selected.isNull()) p.drawImage(m_selection,selected);
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
void CaptureOverlay::resetDocument() { m_document.undoStack()->clear(); m_document.setItemsForCommand({}); m_selected=-1; m_hasPreview=false; }
void CaptureOverlay::mousePressEvent(QMouseEvent *event) {
    if (event->button()==Qt::RightButton) { if (m_selection.isEmpty()) cancelCapture(); else { commitText(); m_selection={}; resetDocument(); m_toolbar->hide(); update(); } return; }
    if (event->button()!=Qt::LeftButton) return;
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
        else { resetDocument(); m_selection={}; m_toolbar->hide(); m_drag=Drag::NewSelection; }
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
    const QColor textColor=existing>=0 ? m_document.items()[existing].color : m_color;
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
    format.setForeground(m_color);
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
    if (QGuiApplication::inputMethod()) QGuiApplication::inputMethod()->commit();
    QTextEdit *editor=m_textEditor; m_textEditor=nullptr;
    const QString text=editor->toPlainText(); const QRectF bounds=editor->geometry(); editor->removeEventFilter(this); editor->deleteLater();
    if (!text.trimmed().isEmpty()) {
        Annotation a; if (m_editingText>=0 && m_editingText<m_document.items().size()) a=m_document.items()[m_editingText];
        else { a.tool=Tool::Text; a.color=m_color; }
        a.rect=bounds; a.text=text; a.fontFamily=editor->font().family();
        a.fontSize=editor->font().pixelSize(); a.fontWeight=int(editor->font().weight());
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
    if (event->key()==Qt::Key_Escape) { cancelCapture(); return; }
    if (event->matches(QKeySequence::Undo)) { m_document.undoStack()->undo(); return; }
    if (event->matches(QKeySequence::Redo)) { m_document.undoStack()->redo(); return; }
    if (event->key()==Qt::Key_Delete || event->key()==Qt::Key_Backspace) { if (m_selected>=0) m_document.remove(m_selected); return; }
    if (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) { finishCapture(); return; }
    QWidget::keyPressEvent(event);
}
void CaptureOverlay::showError(const QString &message) { QMessageBox::warning(this,QStringLiteral("复制失败"),message); activateWindow(); setFocus(); }
void CaptureOverlay::finishCapture() {
    commitText();
    if (m_selection.isEmpty()) return;
    const QImage image=renderSelection(m_frame.image,size(),m_selection,m_document.items()); QString error;
    if (image.isNull() || !m_clipboardWriter(image,&error)) { showError(error.isEmpty()?QStringLiteral("无法复制图像，请重试。") : error); return; }
    emit finished(); close();
}
void CaptureOverlay::cancelCapture() { emit finished(); close(); }
}
