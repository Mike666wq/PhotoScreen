#include "platform.h"
#include "macimage.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLockFile>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QScreen>
#include <QStandardPaths>
#include <QWidget>
#include <AppKit/AppKit.h>
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include <ServiceManagement/ServiceManagement.h>
#include <unistd.h>
#include <memory>

namespace photo {
namespace {
QString nsError(NSError *error, const QString &fallback) {
    return error ? QString::fromNSString(error.localizedDescription) : fallback;
}
// NSScreen 在 Qt 逻辑坐标中的矩形。
// macOS 的全局显示坐标原点在主屏左上角、y 轴向下，与 Qt 逻辑坐标一致。
QRect qtGeometryForNSScreen(NSScreen *screen) {
    NSArray<NSScreen *> *screens=[NSScreen screens];
    const CGFloat primaryHeight=screens.count? screens[0].frame.size.height:0;
    const NSRect frame=screen.frame;
    return QRect(qRound(frame.origin.x),qRound(primaryHeight-NSMaxY(frame)),qRound(frame.size.width),qRound(frame.size.height));
}
// 稳定的显示器锚点：NSScreenNumber 就是 CGDirectDisplayID，与 SCDisplay.displayID 同源。
CGDirectDisplayID displayIdForNSScreen(NSScreen *screen) {
    NSNumber *number=screen.deviceDescription[@"NSScreenNumber"];
    return number? (CGDirectDisplayID)number.unsignedIntValue: 0;
}
// B7：矩形精确相等太脆弱（缩放取整会让边界差 1px），改成容错匹配。
// 中心点距离与宽高差都在 2px 以内即视为同一块屏。
bool nearSameGeometry(const QRect &a, const QRect &b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    const int dx=a.center().x()-b.center().x(), dy=a.center().y()-b.center().y();
    if (qAbs(dx)>2 || qAbs(dy)>2) return false;
    return qAbs(a.width()-b.width())<=2 && qAbs(a.height()-b.height())<=2;
}
CGDirectDisplayID displayIdForScreen(QScreen *screen) {
    const QRect geometry=screen->geometry();
    NSArray<NSScreen *> *screens=[NSScreen screens];
    // 先精确匹配，再容错匹配。
    for (NSScreen *native in screens) if (qtGeometryForNSScreen(native)==geometry) {
        const CGDirectDisplayID id=displayIdForNSScreen(native);
        if (id) return id;
    }
    for (NSScreen *native in screens) if (nearSameGeometry(qtGeometryForNSScreen(native),geometry)) {
        const CGDirectDisplayID id=displayIdForNSScreen(native);
        if (id) return id;
    }
    return 0;
}
// 虚拟桌面上的每块屏：Qt 逻辑矩形 + ScreenCaptureKit 的显示器对象。
struct DisplayJob {
    QRect geometry;
    CGDirectDisplayID displayId=0;
    SCDisplay *display=nil;
};
// 多屏同时捕获时的收集器：各屏的回调可能乱序返回，需要加锁按序号归位。
struct CaptureCollector {
    QMutex mutex;
    QVector<QImage> images;
    int remaining=0;
    QString error;
};
// 捕获回调可能晚于最外层 block 返回才触发，那时 content 若已释放，
// SCDisplay 就会悬空。用一个持有强引用的包装对象把生命周期绑到回调上。
// ARC 下 C++ 类的 ObjC 指针成员默认就是 __strong，构造时 retain、析构时 release。
struct ContentHolder {
    SCShareableContent *content=nil;
    explicit ContentHolder(SCShareableContent *value) : content(value) {}
};
QVector<QRect> windowRects() {
    QVector<QRect> result;
    CFArrayRef windows=CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,kCGNullWindowID);
    if (!windows) return result;
    const pid_t own=getpid();
    for (NSDictionary *info in (__bridge NSArray *)windows) {
        if ([info[(__bridge NSString *)kCGWindowOwnerPID] intValue]==own || [info[(__bridge NSString *)kCGWindowLayer] intValue]!=0 || [info[(__bridge NSString *)kCGWindowAlpha] doubleValue]<=0) continue;
        CGRect rect;
        if (CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString *)kCGWindowBounds],&rect) && rect.size.width>=2 && rect.size.height>=2)
            result.append(QRect(qRound(rect.origin.x),qRound(rect.origin.y),qRound(rect.size.width),qRound(rect.size.height)));
    }
    CFRelease(windows); return result;
}
int macKey(Qt::Key key) {
    static const int letters[] = {0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
    static const int digits[] = {29,18,19,20,21,23,22,26,28,25};
    static const int funcs[] = {122,120,99,118,96,97,98,100,101,109,103,111};
    if (key>=Qt::Key_A && key<=Qt::Key_Z) return letters[int(key)-int(Qt::Key_A)];
    if (key>=Qt::Key_0 && key<=Qt::Key_9) return digits[int(key)-int(Qt::Key_0)];
    if (key>=Qt::Key_F1 && key<=Qt::Key_F12) return funcs[int(key)-int(Qt::Key_F1)];
    return -1;
}
class MacHotkey final : public HotkeyService {
public:
    MacHotkey() {
        EventTypeSpec type={kEventClassKeyboard,kEventHotKeyPressed};
        m_upp=NewEventHandlerUPP(&MacHotkey::eventHandler);
        InstallEventHandler(GetApplicationEventTarget(),m_upp,1,&type,this,&m_eventHandler);
    }
    ~MacHotkey() override {
        if (m_key) UnregisterEventHotKey(m_key);
        if (m_eventHandler) RemoveEventHandler(m_eventHandler);
        if (m_upp) DisposeEventHandlerUPP(m_upp);
    }
    void setHandler(std::function<void()> handler) override { m_handler=std::move(handler); }
    bool registerShortcut(const QKeySequence &shortcut, QString *error) override {
        if (shortcut==m_shortcut && m_key) return true;
        if (shortcut.count()!=1) { *error=QStringLiteral("请选择一个快捷键组合。"); return false; }
        const auto combo=shortcut[0]; const int key=macKey(combo.key()); const auto mods=combo.keyboardModifiers();
        if (key<0 || mods==Qt::NoModifier) { *error=QStringLiteral("请选择修饰键加字母、数字或 F1–F12。"); return false; }
        UInt32 flags=0;
        // Qt maps ControlModifier to Command, and MetaModifier to Control on macOS.
        if (mods.testFlag(Qt::ControlModifier)) flags|=cmdKey;
        if (mods.testFlag(Qt::MetaModifier)) flags|=controlKey;
        if (mods.testFlag(Qt::AltModifier)) flags|=optionKey;
        if (mods.testFlag(Qt::ShiftModifier)) flags|=shiftKey;
        EventHotKeyRef next=nullptr; EventHotKeyID id={0x5053434e,++m_nextId};
        const OSStatus status=RegisterEventHotKey(UInt32(key),flags,id,GetApplicationEventTarget(),0,&next);
        if (status!=noErr) { *error=QStringLiteral("快捷键注册失败，可能已被其他程序占用（%1）。").arg(status); return false; }
        if (m_key) UnregisterEventHotKey(m_key);
        m_key=next; m_shortcut=shortcut; m_id=id.id; return true;
    }
private:
    static OSStatus eventHandler(EventHandlerCallRef, EventRef event, void *data) {
        auto *self=static_cast<MacHotkey *>(data); EventHotKeyID id{};
        if (GetEventParameter(event,kEventParamDirectObject,typeEventHotKeyID,nullptr,sizeof(id),nullptr,&id)==noErr && id.signature==0x5053434e && id.id==self->m_id) {
            if (self->m_handler) self->m_handler(); return noErr;
        }
        return eventNotHandledErr;
    }
    EventHotKeyRef m_key=nullptr; EventHandlerRef m_eventHandler=nullptr; EventHandlerUPP m_upp=nullptr;
    QKeySequence m_shortcut; UInt32 m_nextId=0, m_id=0; std::function<void()> m_handler;
};
}

void captureScreen(QScreen *screen, QObject *context, CaptureCallback callback) {
    // 捕获的是整个虚拟桌面，screen 只用于参数兼容，截图范围不再受它限制。
    Q_UNUSED(screen);
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        callback({{},QStringLiteral("请在系统设置的“隐私与安全性 → 屏幕与系统录音”中允许 PhotoScreen 截屏，然后重试。"),true}); return;
    }
    const auto windows=windowRects(); QPointer<QObject> target(context);
    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *error) {
        if (error || !content) {
            const QString message=nsError(error,QStringLiteral("获取屏幕列表失败。"));
            if (target) QMetaObject::invokeMethod(target,[target,callback,message] { if (target) callback({{},message,false}); },Qt::QueuedConnection);
            return;
        }
        // 虚拟桌面上的每块屏都参与捕获，选区因此可以横跨多屏。
        QVector<DisplayJob> jobs;
        for (QScreen *qscreen : QGuiApplication::screens()) {
            const CGDirectDisplayID displayId=displayIdForScreen(qscreen);
            if (!displayId) continue;
            SCDisplay *match=nil;
            for (SCDisplay *display in content.displays) if (display.displayID==displayId) { match=display; break; }
            if (!match) continue;
            jobs.append(DisplayJob{qscreen->geometry(),displayId,match});
        }
        if (jobs.isEmpty()) {
            if (target) QMetaObject::invokeMethod(target,[target,callback] { if (target) callback({{},QStringLiteral("无法识别当前显示器，请重试。"),false}); },Qt::QueuedConnection);
            return;
        }
        // 各屏同时发起捕获，尽量压缩屏与屏之间的时间差。
        auto collector=std::make_shared<CaptureCollector>();
        auto holder=std::make_shared<ContentHolder>(content);
        collector->images.resize(jobs.size());
        collector->remaining=int(jobs.size());
        for (int index=0;index<jobs.size();++index) {
            const DisplayJob job=jobs.at(index);
            SCContentFilter *filter=[[SCContentFilter alloc] initWithDisplay:job.display excludingWindows:@[]];
            SCStreamConfiguration *config=[[SCStreamConfiguration alloc] init];
            config.width=CGDisplayPixelsWide(job.displayId); config.height=CGDisplayPixelsHigh(job.displayId); config.showsCursor=NO;
            [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef image, NSError *captureError) {
                const QImage pixels=imageFromCGImage(image);
                const QString message=nsError(captureError,QStringLiteral("无法截取屏幕。"));
                bool finished=false;
                {
                    QMutexLocker lock(&collector->mutex);
                    if (pixels.isNull()) { if (collector->error.isEmpty()) collector->error=message; }
                    else collector->images[index]=pixels;
                    finished=(--collector->remaining==0);
                }
                // holder 持有 content 的强引用：回调触发时 SCDisplay 仍然有效。
                if (!finished || !target || !holder->content) return;
                QVector<ScreenImage> screens;
                for (int i=0;i<jobs.size();++i) {
                    if (collector->images.at(i).isNull()) continue;
                    screens.append(ScreenImage{jobs.at(i).geometry,collector->images.at(i)});
                }
                const QString failure=collector->error;
                QMetaObject::invokeMethod(target,[target,callback,screens,windows,failure] {
                    if (!target) return;
                    CaptureResult result;
                    result.frame.screens=screens;
                    result.frame.virtualGeometry=virtualGeometry(screens);
                    result.frame.windowRects=windows;
                    if (screens.isEmpty()) result.error=failure.isEmpty()?QStringLiteral("无法截取屏幕。"):failure;
                    callback(result);
                },Qt::QueuedConnection);
            }];
        }
    }];
}
bool writeClipboard(const QImage &image, QString *error) {
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer,"PNG")) { *error=QStringLiteral("无法编码图片。"); return false; }
    NSData *data=[NSData dataWithBytes:png.constData() length:NSUInteger(png.size())];
    NSImage *native=[[NSImage alloc] initWithData:data]; NSData *tiff=native.TIFFRepresentation;
    if (!tiff) { *error=QStringLiteral("无法生成剪贴板图片。"); return false; }
    NSPasteboard *pasteboard=[NSPasteboard generalPasteboard];
    [pasteboard clearContents]; [pasteboard declareTypes:@[NSPasteboardTypePNG,NSPasteboardTypeTIFF] owner:nil];
    const BOOL pngOK=[pasteboard setData:data forType:NSPasteboardTypePNG];
    const BOOL tiffOK=[pasteboard setData:tiff forType:NSPasteboardTypeTIFF];
    if (!pngOK && !tiffOK) { *error=QStringLiteral("系统剪贴板写入失败，请重试。"); return false; }
    return true;
}
bool isStartupEnabled() { return [SMAppService mainAppService].status==SMAppServiceStatusEnabled; }
bool setStartupEnabled(bool enabled, QString *error) {
    SMAppService *service=[SMAppService mainAppService];
    if (enabled==isStartupEnabled()) return true;
    NSError *nativeError=nil;
    const BOOL ok=enabled ? [service registerAndReturnError:&nativeError] : [service unregisterAndReturnError:&nativeError];
    if (!ok) { *error=nsError(nativeError,QStringLiteral("系统拒绝更改登录项。")); return false; }
    if (enabled && service.status!=SMAppServiceStatusEnabled) { *error=QStringLiteral("请在系统设置的“登录项”中允许 PhotoScreen。" ); return false; }
    return true;
}
bool acquireSingleInstance() {
    static std::unique_ptr<QLockFile> lock;
    const QString path=QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("photoscreen-%1.lock").arg(qEnvironmentVariable("USER")));
    lock=std::make_unique<QLockFile>(path); lock->setStaleLockTime(0); return lock->tryLock();
}
// 诊断日志：追加写入 Application Support/PhotoScreen/PhotoScreen/debug.log。
// 与 Windows 端同一套接口，链路排查时两端日志口径一致。
void appendDiagnosticLog(const QString &line) {
    static QMutex mutex; QMutexLocker locker(&mutex);
    const QString dir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty()) return;
    if (!QDir().mkpath(dir)) return;
    QFile file(dir+QStringLiteral("/debug.log"));
    if (!file.open(QIODevice::Append|QIODevice::Text)) return;
    file.write(QStringLiteral("[%1] %2\n").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),line).toUtf8());
}
void installEnvironmentMonitor(QObject *context, std::function<void()> onSleep) {
    NSNotificationCenter *center=[NSWorkspace sharedWorkspace].notificationCenter;
    id token=[center addObserverForName:NSWorkspaceWillSleepNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *) { onSleep(); }];
    QObject::connect(context,&QObject::destroyed,[center,token] { [center removeObserver:token]; });
}
void configureOverlay(QWidget *widget) {
    NSView *view=(__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    NSWindow *window=view.window;
    window.collectionBehavior=NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorIgnoresCycle;
    window.level=NSScreenSaverWindowLevel;
    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
}
std::unique_ptr<HotkeyService> createHotkeyService() { return std::make_unique<MacHotkey>(); }
QKeySequence defaultShortcut() { return QKeySequence(QKeyCombination(Qt::ControlModifier|Qt::MetaModifier,Qt::Key_A)); }
}
