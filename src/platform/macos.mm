#include "platform.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QMetaObject>
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

namespace photo {
namespace {
QString nsError(NSError *error, const QString &fallback) {
    return error ? QString::fromNSString(error.localizedDescription) : fallback;
}
CGDirectDisplayID displayForScreen(const QRect &geometry) {
    NSArray<NSScreen *> *screens=[NSScreen screens];
    if (screens.count==0) return 0;
    const CGFloat primaryHeight=screens[0].frame.size.height;
    for (NSScreen *screen in screens) {
        const NSRect frame=screen.frame;
        const QRect qtRect(qRound(frame.origin.x),qRound(primaryHeight-NSMaxY(frame)),qRound(frame.size.width),qRound(frame.size.height));
        if (qtRect==geometry) return [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    }
    return 0;
}
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
QImage imageFromCGImage(CGImageRef image) {
    if (!image) return {};
    const int width=int(CGImageGetWidth(image)), height=int(CGImageGetHeight(image));
    QImage result(width,height,QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return {};
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx=CGBitmapContextCreate(result.bits(),width,height,8,result.bytesPerLine(),color,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(color);
    if (!ctx) return {};
    CGContextTranslateCTM(ctx,0,height); CGContextScaleCTM(ctx,1,-1);
    CGContextDrawImage(ctx,CGRectMake(0,0,width,height),image); CGContextRelease(ctx);
    return result;
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
    const QRect geometry=screen->geometry(); const CGDirectDisplayID displayId=displayForScreen(geometry);
    if (!displayId) { callback({{},QStringLiteral("无法识别当前显示器。"),false}); return; }
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
        SCDisplay *chosen=nil;
        for (SCDisplay *display in content.displays) if (display.displayID==displayId) { chosen=display; break; }
        if (!chosen) {
            if (target) QMetaObject::invokeMethod(target,[target,callback] { if (target) callback({{},QStringLiteral("显示器已断开，请重试。"),false}); },Qt::QueuedConnection);
            return;
        }
        SCContentFilter *filter=[[SCContentFilter alloc] initWithDisplay:chosen excludingWindows:@[]];
        SCStreamConfiguration *config=[[SCStreamConfiguration alloc] init];
        config.width=CGDisplayPixelsWide(displayId); config.height=CGDisplayPixelsHigh(displayId); config.showsCursor=NO;
        [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef image, NSError *captureError) {
            const QImage pixels=imageFromCGImage(image);
            const QString message=nsError(captureError,QStringLiteral("无法截取屏幕。"));
            if (target) QMetaObject::invokeMethod(target,[target,callback,geometry,windows,pixels,message] {
                if (!target) return; CaptureResult result; result.frame={pixels,geometry,windows};
                if (pixels.isNull()) result.error=message; callback(result);
            },Qt::QueuedConnection);
        }];
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
