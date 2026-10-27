#include "app.h"
#include "captureoverlay.h"
#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

namespace photo {
namespace {
QIcon trayIcon() {
    QPixmap image(64,64); image.fill(Qt::transparent); QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen); p.setBrush(QColor("#1687ef"));
    p.drawRoundedRect(QRectF(3,3,58,58),14,14);
    p.setPen(QPen(Qt::white,5,Qt::SolidLine,Qt::RoundCap)); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(15,19,34,28),4,4); p.drawLine(QPointF(24,14),QPointF(40,14));
    p.drawEllipse(QPointF(32,33),8,8); return QIcon(image);
}
}
App::App(QObject *parent) : QObject(parent), m_hotkey(createHotkeyService()) {}
bool App::initialize() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr,QStringLiteral("PhotoScreen"),QStringLiteral("系统托盘不可用，无法启动 PhotoScreen。")); return false;
    }
    m_tray=new QSystemTrayIcon(trayIcon(),this); m_tray->setToolTip(QStringLiteral("PhotoScreen"));
    auto *menu=new QMenu;
    connect(menu,&QMenu::aboutToHide,menu,[]{});
    auto *capture=menu->addAction(QStringLiteral("截图"));
    auto *settings=menu->addAction(QStringLiteral("设置…"));
    menu->addSeparator(); auto *quit=menu->addAction(QStringLiteral("退出"));
    connect(capture,&QAction::triggered,this,&App::beginCapture);
    connect(settings,&QAction::triggered,this,&App::showSettings);
    connect(quit,&QAction::triggered,qApp,&QCoreApplication::quit);
    connect(qApp,&QCoreApplication::aboutToQuit,menu,&QObject::deleteLater);
    m_tray->setContextMenu(menu); m_tray->show();
    m_hotkey->setHandler([this] { beginCapture(); });
    const QKeySequence shortcut=QKeySequence::fromString(QSettings().value("shortcut",defaultShortcut().toString(QKeySequence::PortableText)).toString(),QKeySequence::PortableText);
    QString error;
    if (!m_hotkey->registerShortcut(shortcut,&error)) {
        QTimer::singleShot(0,this,[this,error] { QMessageBox::warning(nullptr,QStringLiteral("快捷键不可用"),error+QStringLiteral("\n请通过菜单栏或系统托盘中的“设置”更换快捷键。")); });
    }
    installEnvironmentMonitor(this,[this] { if (m_overlay) m_overlay->close(); });
    return true;
}
void App::beginCapture() {
    if (m_capturing || m_overlay) return;
    if (QApplication::activeModalWidget()) return;
    QScreen *screen=QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen=QGuiApplication::primaryScreen();
    if (!screen) { QMessageBox::warning(nullptr,QStringLiteral("截图失败"),QStringLiteral("未找到可用屏幕。")); return; }
    m_capturing=true;
    QPointer<QScreen> selected=screen;
    QTimer::singleShot(200,this,[this,selected] {
        if (!selected) { m_capturing=false; return; }
        captureScreen(selected,this,[this,selected](CaptureResult result) {
            m_capturing=false;
            if (!selected || result.frame.image.isNull()) { reportCaptureError(result); return; }
            if (selected->geometry()!=result.frame.screenGeometry) return;
            m_overlay=new CaptureOverlay(std::move(result.frame),selected);
            connect(m_overlay,&CaptureOverlay::finished,this,[this] { m_overlay=nullptr; });
            connect(m_overlay,&QObject::destroyed,this,[this] { m_overlay=nullptr; });
            m_overlay->show(); m_overlay->raise(); m_overlay->activateWindow(); m_overlay->setFocus();
        });
    });
}
void App::reportCaptureError(const CaptureResult &result) {
    QMessageBox box(QMessageBox::Warning,QStringLiteral("截图失败"),result.error.isEmpty()?QStringLiteral("无法捕获屏幕，请重试。") : result.error,
                    QMessageBox::Ok);
#ifdef Q_OS_MACOS
    if (result.permissionDenied) {
        auto *open=box.addButton(QStringLiteral("打开系统设置"),QMessageBox::ActionRole); box.exec();
        if (box.clickedButton()==open) QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture")));
        return;
    }
#endif
    box.exec();
}
void App::showSettings() {
    if (m_overlay || m_capturing) return;
    QDialog dialog; dialog.setWindowTitle(QStringLiteral("PhotoScreen 设置"));
    auto *form=new QFormLayout(&dialog);
    const QString stored=QSettings().value("shortcut",defaultShortcut().toString(QKeySequence::PortableText)).toString();
    const QKeySequence oldShortcut=QKeySequence::fromString(stored,QKeySequence::PortableText);
    auto *shortcut=new QKeySequenceEdit(oldShortcut,&dialog); shortcut->setMaximumSequenceLength(1);
    form->addRow(QStringLiteral("截图快捷键"),shortcut);
    auto *startup=new QCheckBox(QStringLiteral("登录系统时启动"),&dialog); startup->setChecked(isStartupEnabled());
    form->addRow(QString(),startup);
    auto *hint=new QLabel(QStringLiteral("快捷键需包含修饰键和字母、数字或 F1–F12。"),&dialog); hint->setWordWrap(true); form->addRow(hint);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog); form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QString error; const QKeySequence next=shortcut->keySequence();
        if (!m_hotkey->registerShortcut(next,&error)) { QMessageBox::warning(&dialog,QStringLiteral("快捷键不可用"),error); return; }
        if (!setStartupEnabled(startup->isChecked(),&error)) {
            QString ignored; m_hotkey->registerShortcut(oldShortcut,&ignored);
            QMessageBox::warning(&dialog,QStringLiteral("无法更新开机启动"),error); return;
        }
        QSettings().setValue("shortcut",next.toString(QKeySequence::PortableText)); dialog.accept();
    });
    dialog.exec();
}
}
