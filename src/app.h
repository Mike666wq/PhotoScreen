#pragma once
#include "platform/platform.h"
#include <QObject>
#include <QPointer>
#include <memory>

class QSystemTrayIcon;
namespace photo {
class CaptureOverlay;
class App final : public QObject {
    Q_OBJECT
public:
    explicit App(QObject *parent = nullptr);
    bool initialize();
private:
    void beginCapture();
    void invalidateCapture();
    void watchScreen(QScreen *screen);
    void showSettings();
    void reportCaptureError(const CaptureResult &result);
    std::unique_ptr<HotkeyService> m_hotkey;
    QSystemTrayIcon *m_tray = nullptr;
    QPointer<CaptureOverlay> m_overlay;
    bool m_capturing = false;
    quint64 m_captureGeneration = 0;
};
}
