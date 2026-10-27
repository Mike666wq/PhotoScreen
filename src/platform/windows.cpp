#include "platform.h"
#include "core/geometry.h"
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QStringList>
#include <QTransform>
#include <QWidget>
#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstring>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace photo {
namespace {
QString failure(const QString &operation, HRESULT hr) { return QStringLiteral("%1（错误 0x%2）").arg(operation).arg(quint32(hr),8,16,QChar('0')); }
struct WindowListContext { RECT desktop; QRect logical; QVector<QRect> result; };
BOOL CALLBACK collectWindow(HWND hwnd, LPARAM param) {
    auto *ctx=reinterpret_cast<WindowListContext *>(param);
    DWORD process=0; GetWindowThreadProcessId(hwnd,&process);
    if (process==GetCurrentProcessId() || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    DWORD cloaked=0; DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)); if (cloaked) return TRUE;
    RECT r{}; if (FAILED(DwmGetWindowAttribute(hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r))) && !GetWindowRect(hwnd,&r)) return TRUE;
    RECT clipped{}; if (!IntersectRect(&clipped,&r,&ctx->desktop)) return TRUE;
    const QRect physicalScreen(ctx->desktop.left,ctx->desktop.top,ctx->desktop.right-ctx->desktop.left,ctx->desktop.bottom-ctx->desktop.top);
    const QRect physicalWindow(clipped.left,clipped.top,clipped.right-clipped.left,clipped.bottom-clipped.top);
    const QRect rect=nativeWindowToLogical(physicalWindow,physicalScreen,ctx->logical);
    if (rect.width()>=2 && rect.height()>=2) ctx->result.append(rect);
    return TRUE;
}
// Qt 屏幕快照：QScreen 属于 GUI 线程，必须在主线程取，之后交给工作线程只用数值。
struct ScreenTarget { QString deviceName; QRect logical; qreal ratio; };
QVector<ScreenTarget> screenTargets() {
    QVector<ScreenTarget> result;
    for (QScreen *screen : QGuiApplication::screens())
        result.append(ScreenTarget{screen->name(),screen->geometry(),screen->devicePixelRatio()});
    return result;
}
// 把 DXGI 输出对应到 Qt 的屏幕。
// 设备名是首选锚点：Qt 的 QScreen::name() 与 DXGI 的 DeviceName 同源（\\.\DISPLAYn）。
bool matchScreen(const QVector<ScreenTarget> &targets, const QString &deviceName, const QRect &physical, QRect &logical) {
    for (const ScreenTarget &target : targets)
        if (target.deviceName.compare(deviceName,Qt::CaseInsensitive)==0) { logical=target.logical; return true; }
    // 设备名对不上时按物理位置兜底：逻辑矩形乘以缩放比例应与物理矩形一致。
    for (const ScreenTarget &target : targets) {
        const QRect expected(qRound(target.logical.left()*target.ratio),qRound(target.logical.top()*target.ratio),
                             qRound(target.logical.width()*target.ratio),qRound(target.logical.height()*target.ratio));
        if (expected==physical) { logical=target.logical; return true; }
    }
    return false;
}
// 捕获单块 DXGI 输出。adapter 与 output 由调用方持有。
// 每个失败点都写诊断日志，错误码是远程定位的唯一线索。
bool captureOutput(IDXGIAdapter1 *adapter, const ComPtr<IDXGIOutput> &output, const DXGI_OUTPUT_DESC &desc, QImage &result) {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> deviceContext; D3D_FEATURE_LEVEL level;
    HRESULT hr=D3D11CreateDevice(adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&deviceContext);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("创建 D3D11 设备"),hr)); return false; }
    ComPtr<IDXGIOutput1> output1; hr=output.As(&output1);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("获取 IDXGIOutput1"),hr)); return false; }
    ComPtr<IDXGIOutputDuplication> duplication; hr=output1->DuplicateOutput(device.Get(),&duplication);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("DuplicateOutput（复制桌面输出）"),hr)); return false; }
    DXGI_OUTDUPL_FRAME_INFO info{}; ComPtr<IDXGIResource> desktop;
    hr=duplication->AcquireNextFrame(2500,&info,&desktop);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("AcquireNextFrame"),hr)); return false; }
    ComPtr<ID3D11Texture2D> texture; hr=desktop.As(&texture);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("获取桌面纹理"),hr)); duplication->ReleaseFrame(); return false; }
    D3D11_TEXTURE2D_DESC texDesc{}; texture->GetDesc(&texDesc);
    texDesc.Usage=D3D11_USAGE_STAGING; texDesc.BindFlags=0; texDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; texDesc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging; hr=device->CreateTexture2D(&texDesc,nullptr,&staging);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("创建暂存纹理"),hr)); duplication->ReleaseFrame(); return false; }
    deviceContext->CopyResource(staging.Get(),texture.Get()); D3D11_MAPPED_SUBRESOURCE map{};
    hr=deviceContext->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("读取暂存纹理"),hr)); duplication->ReleaseFrame(); return false; }
    QImage image(static_cast<const uchar *>(map.pData),int(texDesc.Width),int(texDesc.Height),int(map.RowPitch),QImage::Format_RGB32);
    result=image.copy();
    for (int y=0;y<result.height();++y) {
        auto *line=reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x=0;x<result.width();++x) line[x]|=0xff000000u;
    }
    deviceContext->Unmap(staging.Get(),0); duplication->ReleaseFrame();
    int rotation=0;
    if (desc.Rotation==DXGI_MODE_ROTATION_ROTATE90) rotation=90;
    if (desc.Rotation==DXGI_MODE_ROTATION_ROTATE180) rotation=180;
    if (desc.Rotation==DXGI_MODE_ROTATION_ROTATE270) rotation=270;
    if (rotation) result=result.transformed(QTransform().rotate(rotation));
    return true;
}
// 捕获整个虚拟桌面：遍历所有挂载在桌面上的 DXGI 输出。
// targets 是主线程取好的 Qt 屏幕快照，工作线程里不再碰 GUI 对象。
CaptureResult captureAllFrames(const QVector<ScreenTarget> &targets) {
    appendDiagnosticLog(QStringLiteral("captureAllFrames 开始：Qt 屏 %1 块").arg(targets.size()));
    CaptureResult result;
    ComPtr<IDXGIFactory1> factory; HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("CreateDXGIFactory1"),hr));
        result.error=failure(QStringLiteral("无法初始化屏幕捕获"),hr); return result; }
    // 虚拟桌面：物理矩形来自系统，逻辑矩形来自 Qt 的屏幕列表。
    const QRect physicalVirtual(GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),
                                GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN));
    QRect logicalVirtual;
    for (const ScreenTarget &target : targets)
        logicalVirtual=logicalVirtual.isNull()?target.logical:logicalVirtual.united(target.logical);
    appendDiagnosticLog(QStringLiteral("虚拟桌面：物理(%1,%2 %3x%4) 逻辑(%5,%6 %7x%8)")
        .arg(physicalVirtual.left()).arg(physicalVirtual.top()).arg(physicalVirtual.width()).arg(physicalVirtual.height())
        .arg(logicalVirtual.left()).arg(logicalVirtual.top()).arg(logicalVirtual.width()).arg(logicalVirtual.height()));
    if (!logicalVirtual.isEmpty() && !physicalVirtual.isEmpty()) {
        WindowListContext list{RECT{physicalVirtual.left(),physicalVirtual.top(),physicalVirtual.right()+1,physicalVirtual.bottom()+1},logicalVirtual,{}};
        EnumWindows(collectWindow,reinterpret_cast<LPARAM>(&list));
        result.frame.windowRects=list.result;
        appendDiagnosticLog(QStringLiteral("窗口枚举完成：%1 个").arg(list.result.size()));
    }
    ComPtr<IDXGIAdapter1> adapter; ComPtr<IDXGIOutput> output;
    for (UINT a=0; factory->EnumAdapters1(a,&adapter)!=DXGI_ERROR_NOT_FOUND; ++a) {
        DXGI_ADAPTER_DESC1 adapterDesc{}; adapter->GetDesc1(&adapterDesc);
        const QString adapterName=QString::fromWCharArray(adapterDesc.Description);
        for (UINT o=0; adapter->EnumOutputs(o,&output)!=DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{}; output->GetDesc(&desc);
            const QString deviceName=QString::fromWCharArray(desc.DeviceName);
            const QRect physical(desc.DesktopCoordinates.left,desc.DesktopCoordinates.top,
                                 desc.DesktopCoordinates.right-desc.DesktopCoordinates.left,
                                 desc.DesktopCoordinates.bottom-desc.DesktopCoordinates.top);
            QRect logical;
            QImage image;
            const bool matched=matchScreen(targets,deviceName,physical,logical);
            appendDiagnosticLog(QStringLiteral("适配器「%1」输出 %2：attached=%3 物理(%4,%5 %6x%7) 匹配=%8")
                .arg(adapterName,deviceName).arg(desc.AttachedToDesktop)
                .arg(physical.left()).arg(physical.top()).arg(physical.width()).arg(physical.height())
                .arg(matched
                    ? QStringLiteral("是，逻辑(%1,%2 %3x%4)").arg(logical.left()).arg(logical.top()).arg(logical.width()).arg(logical.height())
                    : QStringLiteral("否")));
            if (desc.AttachedToDesktop && matched && captureOutput(adapter.Get(),output,desc,image)) {
                result.frame.screens.append(ScreenImage{logical,image});
                appendDiagnosticLog(QStringLiteral("输出 %1 捕获成功：%2x%3").arg(deviceName).arg(image.width()).arg(image.height()));
            }
            output.Reset();
        }
        adapter.Reset();
    }
    if (result.frame.screens.isEmpty()) {
        appendDiagnosticLog(QStringLiteral("captureAllFrames 失败：没有捕获到任何输出"));
        result.error=QStringLiteral("无法识别当前显示器，请重新截图。"); return result;
    }
    appendDiagnosticLog(QStringLiteral("captureAllFrames 完成：%1 块屏幕").arg(result.frame.screens.size()));
    result.frame.virtualGeometry=virtualGeometry(result.frame.screens);
    return result;
}
int virtualKey(Qt::Key key) {
    if (key>=Qt::Key_A && key<=Qt::Key_Z) return 'A'+int(key)-int(Qt::Key_A);
    if (key>=Qt::Key_0 && key<=Qt::Key_9) return '0'+int(key)-int(Qt::Key_0);
    if (key>=Qt::Key_F1 && key<=Qt::Key_F12) return VK_F1+int(key)-int(Qt::Key_F1);
    return 0;
}
class WinHotkey final : public HotkeyService, public QAbstractNativeEventFilter {
public:
    WinHotkey() { QCoreApplication::instance()->installNativeEventFilter(this); }
    ~WinHotkey() override { if (m_id) UnregisterHotKey(nullptr,m_id); QCoreApplication::instance()->removeNativeEventFilter(this); }
    void setHandler(std::function<void()> handler) override { m_handler=std::move(handler); }
    bool registerShortcut(const QKeySequence &shortcut, QString *error) override {
        if (shortcut==m_shortcut && m_id) return true;
        if (shortcut.count()!=1) { *error=QStringLiteral("请选择一个快捷键组合。"); return false; }
        const auto combo=shortcut[0]; const int vk=virtualKey(combo.key()); const auto mods=combo.keyboardModifiers();
        if (!vk || mods==Qt::NoModifier) { *error=QStringLiteral("请选择修饰键加字母、数字或 F1–F12。"); return false; }
        UINT flags=MOD_NOREPEAT;
        if (mods.testFlag(Qt::ControlModifier)) flags|=MOD_CONTROL;
        if (mods.testFlag(Qt::AltModifier)) flags|=MOD_ALT;
        if (mods.testFlag(Qt::ShiftModifier)) flags|=MOD_SHIFT;
        if (mods.testFlag(Qt::MetaModifier)) flags|=MOD_WIN;
        const int next=++m_nextId;
        if (!RegisterHotKey(nullptr,next,flags,UINT(vk))) { *error=QStringLiteral("快捷键注册失败，可能已被其他程序占用（%1）。").arg(GetLastError()); return false; }
        if (m_id) UnregisterHotKey(nullptr,m_id);
        m_id=next; m_shortcut=shortcut; return true;
    }
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override {
        const MSG *msg=static_cast<MSG *>(message);
        if (msg->message==WM_HOTKEY && int(msg->wParam)==m_id) { if (m_handler) m_handler(); return true; }
        return false;
    }
private:
    int m_id=0, m_nextId=0x4e00; QKeySequence m_shortcut; std::function<void()> m_handler;
};
class WinPowerMonitor final : public QObject, public QAbstractNativeEventFilter {
public:
    WinPowerMonitor(QObject *parent,std::function<void()> handler) : QObject(parent),m_handler(std::move(handler)) { QCoreApplication::instance()->installNativeEventFilter(this); }
    ~WinPowerMonitor() override { QCoreApplication::instance()->removeNativeEventFilter(this); }
    bool nativeEventFilter(const QByteArray &,void *message,qintptr *) override {
        const MSG *msg=static_cast<MSG *>(message);
        if (msg->message==WM_POWERBROADCAST && msg->wParam==PBT_APMSUSPEND && m_handler) m_handler();
        return false;
    }
private: std::function<void()> m_handler;
};
HGLOBAL allocMemory(const void *data, size_t bytes) {
    HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes); if (!mem) return nullptr;
    void *ptr=GlobalLock(mem); if (!ptr) { GlobalFree(mem); return nullptr; }
    std::memcpy(ptr,data,bytes); GlobalUnlock(mem); return mem;
}
}

// 诊断日志：追加写入 %LOCALAPPDATA%\PhotoScreen\debug.log。
// 用于远程定位「触发截图无反应」类问题：捕获链路上每一步的结果都落盘。
// 日志自身失败一律静默放弃，不允许影响截图主流程。
void appendDiagnosticLog(const QString &line) {
    static QMutex mutex; QMutexLocker locker(&mutex);
    const QString root=qEnvironmentVariable("LOCALAPPDATA");
    if (root.isEmpty()) return;
    const QString dir=root+QStringLiteral("\\PhotoScreen");
    if (!QDir().mkpath(dir)) return;
    QFile file(dir+QStringLiteral("\\debug.log"));
    if (!file.open(QIODevice::Append|QIODevice::Text)) return;
    file.write(QStringLiteral("[%1] %2\n").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),line).toUtf8());
}

void captureScreen(QScreen *screen, QObject *context, CaptureCallback callback) {
    // 捕获的是整个虚拟桌面，screen 只用于参数兼容，截图范围不再受它限制。
    Q_UNUSED(screen);
    // QScreen 属于 GUI 线程：先在主线程取好快照，再交给工作线程。
    const QVector<ScreenTarget> targets=screenTargets();
    QStringList detail;
    for (const ScreenTarget &t : targets)
        detail << QStringLiteral("%1 逻辑(%2,%3 %4x%5) 缩放%6")
            .arg(t.deviceName).arg(t.logical.left()).arg(t.logical.top()).arg(t.logical.width()).arg(t.logical.height()).arg(t.ratio);
    appendDiagnosticLog(QStringLiteral("captureScreen：主线程屏快照 [%1]").arg(detail.join(QStringLiteral(" | "))));
    QPointer<QObject> target(context);
    std::thread([target,callback,targets] {
        const qint64 started=QDateTime::currentMSecsSinceEpoch();
        appendDiagnosticLog(QStringLiteral("captureScreen：工作线程开始"));
        CaptureResult result=captureAllFrames(targets);
        appendDiagnosticLog(QStringLiteral("captureScreen：工作线程完成，耗时 %1ms，屏数 %2，错误=%3")
            .arg(QDateTime::currentMSecsSinceEpoch()-started).arg(result.frame.screens.size()).arg(result.error.isEmpty()?QStringLiteral("无"):result.error));
        if (target) {
            QMetaObject::invokeMethod(target,[target,callback,result] { if (target) callback(result); },Qt::QueuedConnection);
        } else {
            appendDiagnosticLog(QStringLiteral("captureScreen：目标对象已销毁，丢弃捕获结果"));
        }
    }).detach();
}
bool writeClipboard(const QImage &image, QString *error) {
    const QImage rgba=image.convertToFormat(QImage::Format_RGB32);
    if (rgba.isNull()) { *error=QStringLiteral("无法生成剪贴板图片。"); return false; }
    const int width=rgba.width(), height=rgba.height(), stride=width*4;
    QByteArray dibv5(int(sizeof(BITMAPV5HEADER))+stride*height,Qt::Uninitialized);
    auto *v5=reinterpret_cast<BITMAPV5HEADER *>(dibv5.data()); std::memset(v5,0,sizeof(*v5));
    v5->bV5Size=sizeof(*v5); v5->bV5Width=width; v5->bV5Height=-height; v5->bV5Planes=1; v5->bV5BitCount=32;
    v5->bV5Compression=BI_BITFIELDS; v5->bV5SizeImage=stride*height; v5->bV5RedMask=0x00ff0000; v5->bV5GreenMask=0x0000ff00;
    v5->bV5BlueMask=0x000000ff; v5->bV5AlphaMask=0xff000000; v5->bV5CSType=LCS_sRGB;
    for (int y=0;y<height;++y) std::memcpy(dibv5.data()+sizeof(*v5)+y*stride,rgba.constScanLine(y),stride);
    QByteArray dib(int(sizeof(BITMAPINFOHEADER))+stride*height,Qt::Uninitialized);
    auto *header=reinterpret_cast<BITMAPINFOHEADER *>(dib.data()); std::memset(header,0,sizeof(*header));
    header->biSize=sizeof(*header); header->biWidth=width; header->biHeight=height; header->biPlanes=1; header->biBitCount=32;
    header->biCompression=BI_RGB; header->biSizeImage=stride*height;
    for (int y=0;y<height;++y) std::memcpy(dib.data()+sizeof(*header)+y*stride,rgba.constScanLine(height-y-1),stride);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); rgba.save(&buffer,"PNG");
    HGLOBAL v5Memory=allocMemory(dibv5.constData(),dibv5.size()), dibMemory=allocMemory(dib.constData(),dib.size());
    HGLOBAL pngMemory=png.isEmpty()?nullptr:allocMemory(png.constData(),png.size());
    if (!v5Memory || !dibMemory || !pngMemory) {
        if (v5Memory) GlobalFree(v5Memory); if (dibMemory) GlobalFree(dibMemory); if (pngMemory) GlobalFree(pngMemory);
        *error=QStringLiteral("无法分配剪贴板内存。" ); return false;
    }
    QWidget *active=QApplication::activeWindow();
    HWND owner=active ? reinterpret_cast<HWND>(active->winId()) : nullptr;
    if (!owner || !OpenClipboard(owner)) { GlobalFree(v5Memory); GlobalFree(dibMemory); GlobalFree(pngMemory); *error=QStringLiteral("剪贴板正被其他程序占用，请重试。" ); return false; }
    if (!EmptyClipboard()) { CloseClipboard(); GlobalFree(v5Memory); GlobalFree(dibMemory); GlobalFree(pngMemory); *error=QStringLiteral("无法打开剪贴板，请重试。" ); return false; }
    bool ok=false;
    if (SetClipboardData(CF_DIBV5,v5Memory)) ok=true; else GlobalFree(v5Memory);
    if (SetClipboardData(CF_DIB,dibMemory)) ok=true; else GlobalFree(dibMemory);
    const UINT pngType=RegisterClipboardFormatW(L"PNG");
    if (pngType && SetClipboardData(pngType,pngMemory)) ok=true; else GlobalFree(pngMemory);
    CloseClipboard();
    if (!ok) *error=QStringLiteral("系统剪贴板写入失败，请重试。");
    return ok;
}
bool isStartupEnabled() {
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),QSettings::NativeFormat);
    return run.contains(QStringLiteral("PhotoScreen"));
}
bool setStartupEnabled(bool enabled, QString *error) {
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),QSettings::NativeFormat);
    if (enabled) run.setValue(QStringLiteral("PhotoScreen"),QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else run.remove(QStringLiteral("PhotoScreen"));
    run.sync();
    if (run.status()!=QSettings::NoError) { *error=QStringLiteral("无法更新当前用户的启动项。" ); return false; }
    return true;
}
bool acquireSingleInstance() {
    static HANDLE mutex=CreateMutexW(nullptr,FALSE,L"PhotoScreen.App");
    if (!mutex) return false;
    if (GetLastError()==ERROR_ALREADY_EXISTS) { CloseHandle(mutex); mutex=nullptr; return false; }
    return true;
}
void installEnvironmentMonitor(QObject *context, std::function<void()> onSleep) { new WinPowerMonitor(context,std::move(onSleep)); }
void configureOverlay(QWidget *) {}
std::unique_ptr<HotkeyService> createHotkeyService() { return std::make_unique<WinHotkey>(); }
QKeySequence defaultShortcut() { return QKeySequence(QKeyCombination(Qt::ControlModifier|Qt::AltModifier,Qt::Key_A)); }
}
