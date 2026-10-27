#pragma once
#include "core/screen.h"
#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <QRect>
#include <QVector>
#include <functional>
#include <memory>

class QScreen;
class QWidget;
namespace photo {
// 一次捕获的完整结果：整个虚拟桌面，而不是单块屏幕。
// 所有矩形都是 Qt 逻辑全局坐标（虚拟桌面空间），图像是各屏的物理像素图。
struct ScreenFrame {
    QVector<ScreenImage> screens;
    QRect virtualGeometry;
    QVector<QRect> windowRects;
};
struct CaptureResult {
    ScreenFrame frame;
    QString error;
    bool permissionDenied = false;
};
using CaptureCallback = std::function<void(CaptureResult)>;
void captureScreen(QScreen *screen, QObject *context, CaptureCallback callback);
bool writeClipboard(const QImage &image, QString *error);
bool isStartupEnabled();
bool setStartupEnabled(bool enabled, QString *error);
bool acquireSingleInstance();
void installEnvironmentMonitor(QObject *context, std::function<void()> onSleep);
void configureOverlay(QWidget *widget);
// 诊断日志：追加一行到应用数据目录下的 debug.log
// （Windows：%LOCALAPPDATA%\PhotoScreen\debug.log；macOS：Application Support 下同名文件）。
// 线程安全；写入失败时静默放弃，绝不影响截图主流程。
void appendDiagnosticLog(const QString &line);

class HotkeyService {
public:
    virtual ~HotkeyService() = default;
    virtual bool registerShortcut(const QKeySequence &shortcut, QString *error) = 0;
    virtual void setHandler(std::function<void()> handler) = 0;
};
std::unique_ptr<HotkeyService> createHotkeyService();
QKeySequence defaultShortcut();
}
