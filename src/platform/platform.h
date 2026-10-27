#pragma once
#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <QRect>
#include <QVector>
#include <functional>
#include <memory>

class QScreen;
namespace photo {
struct ScreenFrame {
    QImage image;
    QRect screenGeometry;
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

class HotkeyService {
public:
    virtual ~HotkeyService() = default;
    virtual bool registerShortcut(const QKeySequence &shortcut, QString *error) = 0;
    virtual void setHandler(std::function<void()> handler) = 0;
};
std::unique_ptr<HotkeyService> createHotkeyService();
QKeySequence defaultShortcut();
}
