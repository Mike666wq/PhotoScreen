#include "captureoverlay.h"
#include "core/geometry.h"
#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSignalBlocker>
#include <QSettings>
#include <QSpinBox>
#include <QTextEdit>
#include <QToolButton>
#include <algorithm>
#include <cmath>

namespace photo {
namespace {
QRectF normalizedAt(const QPointF &a, const QPointF &b) { return QRectF(a, b).normalized(); }
QRectF handleRect(const QPointF &p) { return QRectF(p.x()-5, p.y()-5, 10, 10); }
QVector<QPointF> handles(const QRectF &r) {
    return {r.topLeft(), QPointF(r.center().x(),r.top()), r.topRight(),
            QPointF(r.right(),r.center().y()), r.bottomRight(), QPointF(r.center().x(),r.bottom()),
            r.bottomLeft(), QPointF(r.left(),r.center().y())};
}
}
CaptureOverlay::CaptureOverlay(ScreenFrame frame, QScreen *screen)
    : QWidget(nullptr), m_frame(std::move(frame)), m_screen(screen) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(m_frame.screenGeometry);
    setCursor(Qt::CrossCursor);
    QSettings settings;
    m_color = settings.value("annotation/color", QColor(Qt::red)).value<QColor>();
    m_lineWidth = settings.value("annotation/width", 3).toInt();
    m_fontSize = settings.value("annotation/fontSize", 18).toInt();
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
    auto *group = new QButtonGroup(m_toolbar); group->setExclusive(true);
    const QVector<QPair<QString,Tool>> tools = {{QStringLiteral("选择"),Tool::Select},{QStringLiteral("矩形"),Tool::Rectangle},
       {QStringLiteral("椭圆"),Tool::Ellipse},{QStringLiteral("箭头"),Tool::Arrow},{QStringLiteral("画笔"),Tool::Pen},
       {QStringLiteral("文字"),Tool::Text},{QStringLiteral("马赛克"),Tool::Mosaic}};
    for (const auto &entry : tools) {
        auto *button = new QToolButton(m_toolbar); button->setText(entry.first); button->setCheckable(true);
        group->addButton(button); layout->addWidget(button);
        if (entry.second == Tool::Select) button->setChecked(true);
        connect(button, &QToolButton::clicked, this, [this, tool=entry.second] { commitText(); setTool(tool); });
    }
    m_colorButton = new QToolButton(m_toolbar); m_colorButton->setText(QStringLiteral("颜色")); layout->addWidget(m_colorButton);
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(m_color, this, QStringLiteral("标注颜色"));
        if (chosen.isValid()) {
            m_color=chosen; QSettings().setValue("annotation/color", chosen);
            if (m_selected>=0 && m_selected<m_document.items().size()) {
                const int i=m_selected; auto item=m_document.items()[i]; item.color=chosen; m_document.replace(i,item); m_selected=i; update();
            }
        }
    });
    m_widthControl = new QSpinBox(m_toolbar); m_widthControl->setRange(1,20); m_widthControl->setValue(m_lineWidth);
    m_widthControl->setToolTip(QStringLiteral("线宽")); m_widthControl->setFixedWidth(48); layout->addWidget(m_widthControl);
    connect(m_widthControl, &QSpinBox::valueChanged, this, [this](int n) {
        m_lineWidth=n; QSettings().setValue("annotation/width",n);
        if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool!=Tool::Text) {
            const int i=m_selected; auto item=m_document.items()[i]; item.width=n; m_document.replace(i,item); m_selected=i; update();
        }
    });
    m_fontControl = new QSpinBox(m_toolbar); m_fontControl->setRange(10,72); m_fontControl->setValue(m_fontSize);
    m_fontControl->setToolTip(QStringLiteral("字号")); m_fontControl->setFixedWidth(48); layout->addWidget(m_fontControl);
    connect(m_fontControl, &QSpinBox::valueChanged, this, [this](int n) {
        m_fontSize=n; QSettings().setValue("annotation/fontSize",n);
        if (m_selected>=0 && m_selected<m_document.items().size() && m_document.items()[m_selected].tool==Tool::Text) {
            const int i=m_selected; auto item=m_document.items()[i]; item.fontSize=n; m_document.replace(i,item); m_selected=i; update();
        }
    });
    m_undoButton = new QToolButton(m_toolbar); m_undoButton->setText(QStringLiteral("撤销")); m_undoButton->setEnabled(false); layout->addWidget(m_undoButton);
    m_redoButton = new QToolButton(m_toolbar); m_redoButton->setText(QStringLiteral("重做")); m_redoButton->setEnabled(false); layout->addWidget(m_redoButton);
    connect(m_undoButton, &QToolButton::clicked, this, [this] { commitText(); m_document.undoStack()->undo(); });
    connect(m_redoButton, &QToolButton::clicked, this, [this] { commitText(); m_document.undoStack()->redo(); });
    auto *cancel = new QToolButton(m_toolbar); cancel->setText(QStringLiteral("取消")); layout->addWidget(cancel);
    auto *done = new QToolButton(m_toolbar); done->setText(QStringLiteral("完成")); layout->addWidget(done);
    connect(cancel, &QToolButton::clicked, this, &CaptureOverlay::cancelCapture);
    connect(done, &QToolButton::clicked, this, &CaptureOverlay::finishCapture);
    m_toolbar->adjustSize(); m_toolbar->hide();
}
void CaptureOverlay::positionToolbar() {
    if (m_selection.isEmpty()) { m_toolbar->hide(); return; }
    m_toolbar->adjustSize();
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
        if (items[i].tool != Tool::Pen && items[i].tool != Tool::Mosaic && items[i].bounds().adjusted(-5,-5,5,5).contains(p)) return i;
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
        if (m_drag==Drag::MoveObject || m_drag==Drag::ResizeObject) {
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
            p.setBrush(Qt::white); p.drawRect(handleRect(box.bottomRight()));
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
        const QRect source(qMax(0,px-8),qMax(0,py-8),qMin(17,m_frame.image.width()-qMax(0,px-8)),qMin(17,m_frame.image.height()-qMax(0,py-8)));
        const int x=qBound(0,int(m_pointer.x())+18,qMax(0,width()-150)), y=qBound(0,int(m_pointer.y())+18,qMax(0,height()-148));
        const QRect box(x,y,146,144); p.fillRect(box,QColor(20,20,20,230));
        p.drawImage(QRect(x+4,y+4,136,108),m_frame.image,source);
        p.setPen(Qt::red); p.drawLine(x+72,y+49,x+72,y+67); p.drawLine(x+63,y+58,x+81,y+58);
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
        if (m_selected>=0 && m_selected<m_document.items().size() && handleRect(m_document.items()[m_selected].bounds().bottomRight()).contains(m_press)) {
            m_drag=Drag::ResizeObject; m_startAnnotation=m_document.items()[m_selected]; m_startRect=m_startAnnotation.bounds(); return;
        }
        const int object=objectAt(m_press);
        if (object>=0) {
            m_selected=object; m_startAnnotation=m_document.items()[object]; m_color=m_startAnnotation.color;
            const QSignalBlocker b1(m_widthControl), b2(m_fontControl);
            m_widthControl->setValue(qRound(m_startAnnotation.width)); m_fontControl->setValue(m_startAnnotation.fontSize);
            m_drag=Drag::MoveObject; update(); return;
        }
        m_selected=-1; m_handle=selectionHandleAt(m_press); m_startRect=m_selection;
        if (m_handle>=0) m_drag=Drag::ResizeSelection;
        else if (m_selection.contains(m_press)) m_drag=Drag::MoveSelection;
        else { resetDocument(); m_selection={}; m_toolbar->hide(); m_drag=Drag::NewSelection; }
    } else if (m_selection.contains(m_press)) {
        if (m_tool==Tool::Text) { startText(m_press); return; }
        m_drag=Drag::Draw; m_preview={}; m_preview.tool=m_tool; m_preview.color=m_color; m_preview.width=m_lineWidth;
        m_preview.fontSize=m_fontSize; m_preview.rect=QRectF(m_press,m_press); if (m_tool==Tool::Pen) m_preview.points.append(m_press);
        m_hasPreview=true;
    }
}
void CaptureOverlay::mouseMoveEvent(QMouseEvent *event) {
    m_pointer=boundedPoint(event->position());
    if (m_drag==Drag::NewSelection) { if (QLineF(m_press,m_pointer).length()>=3) m_selection=normalizedAt(m_press,m_pointer); }
    else if (m_drag==Drag::MoveSelection) {
        QRectF r=m_startRect.translated(m_pointer-m_press);
        r.moveLeft(qBound(0.0,r.left(),qMax(0.0,width()-r.width()))); r.moveTop(qBound(0.0,r.top(),qMax(0.0,height()-r.height())); m_selection=r;
    } else if (m_drag==Drag::ResizeSelection) m_selection=resizeRect(m_startRect,m_handle,m_pointer);
    else if (m_drag==Drag::Draw) { m_preview.rect=QRectF(m_press,m_pointer); if (m_tool==Tool::Pen) m_preview.points.append(m_pointer); }
    else if (m_drag==Drag::MoveObject) { m_preview=m_startAnnotation; m_preview.translate(m_pointer-m_press); m_hasPreview=true; }
    else if (m_drag==Drag::ResizeObject) { m_preview=m_startAnnotation; m_preview.setBounds(resizeRect(m_startRect,4,m_pointer)); m_hasPreview=true; }
    else if (m_selection.isEmpty()) m_hoverWindow=windowAt(m_pointer);
    if (!m_selection.isEmpty() && m_drag!=Drag::NewSelection) positionToolbar();
    update();
}
void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button()!=Qt::LeftButton) return;
    if (m_drag==Drag::NewSelection) {
        if (QLineF(m_press,m_pointer).length()<3) m_selection=windowAt(m_press);
        if (m_selection.width()<2 || m_selection.height()<2) m_selection={};
        positionToolbar();
    } else if (m_drag==Drag::Draw && m_hasPreview) {
        if (m_preview.tool==Tool::Pen || (m_preview.tool==Tool::Arrow && QLineF(m_preview.rect.topLeft(),m_preview.rect.bottomRight()).length()>=2) ||
            (m_preview.rect.normalized().width()>=2 && m_preview.rect.normalized().height()>=2)) m_document.add(m_preview);
    } else if ((m_drag==Drag::MoveObject || m_drag==Drag::ResizeObject) && m_hasPreview) {
        m_document.replace(m_selected,m_preview);
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
    const QRectF bounds=existing>=0 ? m_document.items()[existing].bounds() : QRectF(point,QSizeF(qMin(220.0,m_selection.right()-point.x()),80));
    m_textEditor=new QTextEdit(this); m_textEditor->setAcceptRichText(false);
    m_textEditor->setGeometry(bounds.toAlignedRect().intersected(rect()));
    QFont font=m_textEditor->font(); font.setPixelSize(existing>=0 ? m_document.items()[existing].fontSize : m_fontSize); m_textEditor->setFont(font);
    m_textEditor->setStyleSheet(QStringLiteral("QTextEdit{border:1px solid #1687ef;background:rgba(255,255,255,210);color:%1;}").arg(m_color.name()));
    if (existing>=0) m_textEditor->setPlainText(m_document.items()[existing].text);
    m_textEditor->installEventFilter(this); m_textEditor->show(); m_textEditor->raise(); m_textEditor->setFocus();
}
void CaptureOverlay::commitText() {
    if (!m_textEditor) return;
    QTextEdit *editor=m_textEditor; m_textEditor=nullptr;
    const QString text=editor->toPlainText(); const QRectF bounds=editor->geometry(); editor->removeEventFilter(this); editor->deleteLater();
    if (!text.trimmed().isEmpty()) {
        Annotation a; if (m_editingText>=0 && m_editingText<m_document.items().size()) a=m_document.items()[m_editingText];
        else { a.tool=Tool::Text; a.color=m_color; a.fontSize=m_fontSize; }
        a.rect=bounds; a.text=text;
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
    if (image.isNull() || !writeClipboard(image,&error)) { showError(error.isEmpty()?QStringLiteral("无法复制图像，请重试。") : error); return; }
    emit finished(); close();
}
void CaptureOverlay::cancelCapture() { emit finished(); close(); }
}
