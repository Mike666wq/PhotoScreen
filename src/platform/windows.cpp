#include "platform.h"
#include "core/geometry.h"
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QBuffer>
#include <QColor>
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
// 混合 DPI：每块屏的物理矩形与逻辑矩形配对，窗口换算按「主要所在屏」进行。
struct ScreenPair { QRect physical; QRect logical; };
struct WindowListContext { RECT desktop; QRect logical; QVector<ScreenPair> pairs; QVector<QRect> result; };
BOOL CALLBACK collectWindow(HWND hwnd, LPARAM param) {
    auto *ctx=reinterpret_cast<WindowListContext *>(param);
    DWORD process=0; GetWindowThreadProcessId(hwnd,&process);
    if (process==GetCurrentProcessId() || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    DWORD cloaked=0; DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)); if (cloaked) return TRUE;
    RECT r{}; if (FAILED(DwmGetWindowAttribute(hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r))) && !GetWindowRect(hwnd,&r)) return TRUE;
    RECT clipped{}; if (!IntersectRect(&clipped,&r,&ctx->desktop)) return TRUE;
    const QRect physicalWindow(clipped.left,clipped.top,clipped.right-clipped.left,clipped.bottom-clipped.top);
    // 混合 DPI 下桌面级统一缩放会错位：找与窗口相交面积最大的屏，用该屏的物理↔逻辑矩形换算。
    const ScreenPair *best=nullptr; qint64 bestArea=0;
    for (const ScreenPair &pair : ctx->pairs) {
        const QRect inter=physicalWindow.intersected(pair.physical);
        const qint64 area=qint64(inter.width())*inter.height();
        if (!inter.isEmpty() && area>bestArea) { bestArea=area; best=&pair; }
    }
    QRect rect;
    if (best) rect=nativeWindowToLogical(physicalWindow,best->physical,best->logical);
    else if (!ctx->logical.isEmpty()) {
        const QRect physicalScreen(ctx->desktop.left,ctx->desktop.top,ctx->desktop.right-ctx->desktop.left,ctx->desktop.bottom-ctx->desktop.top);
        rect=nativeWindowToLogical(physicalWindow,physicalScreen,ctx->logical);
    }
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
// 把 DXGI 输出对应到 Qt 的屏幕。策略按可靠性排序：
// 1) 设备名：QScreen::name() 与 DXGI DeviceName 理论上同源（\\.\DISPLAYn），
//    但部分环境下 Qt 返回 EDID 型号名（如 "B160QAN02.7"），此时对不上。
// 2) 位置+缩放尺寸：Qt 混合 DPI 下各屏逻辑位置保留原生坐标、尺寸按自身缩放折算，
//    因此物理位置 ≈ 逻辑位置、物理尺寸 ≈ 逻辑尺寸×缩放。±2px 容差吸收取整误差。
// 3) 整体乘缩放兜底：逻辑矩形整体乘缩放后与物理矩形一致（位置也被缩放的行为）。
bool matchScreen(const QVector<ScreenTarget> &targets, const QString &deviceName, const QRect &physical, QRect &logical) {
    for (const ScreenTarget &target : targets)
        if (target.deviceName.compare(deviceName,Qt::CaseInsensitive)==0) { logical=target.logical; return true; }
    for (const ScreenTarget &target : targets) {
        const int expectedWidth=qRound(target.logical.width()*target.ratio);
        const int expectedHeight=qRound(target.logical.height()*target.ratio);
        if (qAbs(target.logical.left()-physical.left())<=2 && qAbs(target.logical.top()-physical.top())<=2
            && qAbs(expectedWidth-physical.width())<=2 && qAbs(expectedHeight-physical.height())<=2) {
            logical=target.logical; return true;
        }
    }
    for (const ScreenTarget &target : targets) {
        const QRect expected(qRound(target.logical.left()*target.ratio),qRound(target.logical.top()*target.ratio),
                             qRound(target.logical.width()*target.ratio),qRound(target.logical.height()*target.ratio));
        if (qAbs(expected.left()-physical.left())<=2 && qAbs(expected.top()-physical.top())<=2
            && qAbs(expected.width()-physical.width())<=2 && qAbs(expected.height()-physical.height())<=2) {
            logical=target.logical; return true;
        }
    }
    return false;
}
// 采样统计：平均灰度与极暗像素占比。
// DXGI 桌面复制在多显卡笔记本（跨适配器）或开启 MPO 时会复制到黑帧，
// 用它把「捕获失败」和「捕获成功但内容全黑」区分开，写进日志便于远程定位。
struct LumaStats { int average=-1; int darkPercent=0; };
LumaStats lumaStats(const QImage &image) {
    LumaStats stats;
    if (image.isNull()) return stats;
    const int stepY=qMax(1,image.height()/48), stepX=qMax(1,image.width()/48);
    long long sum=0; int count=0, dark=0;
    for (int y=0;y<image.height();y+=stepY) {
        const QRgb *line=reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x=0;x<image.width();x+=stepX) {
            const QRgb p=line[x];
            const int value=(qRed(p)+qGreen(p)+qBlue(p))/3;
            sum+=value; ++count; if (value<=2) ++dark;
        }
    }
    if (!count) return stats;
    stats.average=int(sum/count);
    stats.darkPercent=dark*100/count;
    return stats;
}
// 整体全黑，或几乎全黑（残留几个亮点）都算黑帧。
// 误判的代价只是多抓一次 GDI，漏判的代价是继续黑屏，所以这里放宽。
bool looksBlack(const QImage &image) {
    const LumaStats stats=lumaStats(image);
    if (stats.average<0) return true;
    return stats.average<=2 || stats.darkPercent>=98;
}
// 灰度统计已足够区分「DXGI 黑帧 / GDI 兜底生效 / GDI 也是黑」三种情况，
// 因此不做诊断图落盘：产品约定「不保存截图历史或文件」，屏幕内容一律只在内存中。

// GDI 兜底：DXGI 复制到黑帧时，改用 GDI 抓整个虚拟桌面。
// GDI 走 DWM 合成路径，不受跨适配器复制与 MPO 影响。
// 返回的是物理像素图，坐标原点在虚拟桌面左上角。
QImage captureDesktopWithGdi() {
    const int x=GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width=GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width<=0 || height<=0) { appendDiagnosticLog(QStringLiteral("GDI 兜底：虚拟桌面尺寸无效")); return {}; }
    // 两条等价路径：CreateDC("DISPLAY") 的 DC 覆盖整个虚拟桌面且原点在虚拟桌面左上角；
    // GetDC(NULL) 的原点固定在主屏左上角，需要用虚拟桌面偏移做源坐标。
    HDC displayDC=CreateDCW(L"DISPLAY",nullptr,nullptr,nullptr);
    const bool useScreenDc=displayDC!=nullptr;
    if (!useScreenDc) {
        displayDC=GetDC(nullptr);
        if (!displayDC) { appendDiagnosticLog(QStringLiteral("GDI 兜底：GetDC(NULL) 失败，错误 %1").arg(GetLastError())); return {}; }
    }
    HDC memDC=CreateCompatibleDC(displayDC);
    if (!memDC) { appendDiagnosticLog(QStringLiteral("GDI 兜底：CreateCompatibleDC 失败，错误 %1").arg(GetLastError())); DeleteDC(displayDC); return {}; }
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;   // 负高度＝自上而下，与 QImage 行序一致
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void *bits=nullptr;
    HBITMAP dib=CreateDIBSection(displayDC,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if (!dib || !bits) {
        appendDiagnosticLog(QStringLiteral("GDI 兜底：CreateDIBSection 失败，错误 %1").arg(GetLastError()));
        if (dib) DeleteObject(dib); DeleteDC(memDC); DeleteDC(displayDC); return {};
    }
    HBITMAP previous=SelectObject(memDC,dib);
    const BOOL copied=BitBlt(memDC,0,0,width,height,displayDC,useScreenDc?0:x,useScreenDc?0:y,SRCCOPY|CAPTUREBLT);
    QImage result;
    if (copied) result=QImage(static_cast<const uchar *>(bits),width,height,width*4,QImage::Format_RGB32).copy();
    SelectObject(memDC,previous); DeleteObject(dib); DeleteDC(memDC);
    if (useScreenDc) DeleteDC(displayDC); else ReleaseDC(nullptr,displayDC);
    if (result.isNull()) { appendDiagnosticLog(QStringLiteral("GDI 兜底：BitBlt 失败，错误 %1").arg(GetLastError())); return {}; }
    const LumaStats stats=lumaStats(result);
    appendDiagnosticLog(QStringLiteral("GDI 兜底抓取成功：%1x%2（来源 %3，虚拟桌面原点 %4,%5，平均灰度 %6，暗像素 %7%）")
        .arg(result.width()).arg(result.height()).arg(useScreenDc?QStringLiteral("CreateDC(DISPLAY)"):QStringLiteral("GetDC(NULL)"))
        .arg(x).arg(y).arg(stats.average).arg(stats.darkPercent));
    return result;
}
// 捕获单块 DXGI 输出。adapter 与 output 由调用方持有。
// 每个失败点都写诊断日志，错误码是远程定位的唯一线索。
// 取到帧后若发现是黑帧，释放并重取（部分驱动首帧是无效的黑面）。
bool captureOutput(IDXGIAdapter1 *adapter, const ComPtr<IDXGIOutput> &output, const DXGI_OUTPUT_DESC &desc, QImage &result) {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> deviceContext; D3D_FEATURE_LEVEL level;
    HRESULT hr=D3D11CreateDevice(adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&deviceContext);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("创建 D3D11 设备"),hr)); return false; }
    ComPtr<IDXGIOutput1> output1; hr=output.As(&output1);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("获取 IDXGIOutput1"),hr)); return false; }
    ComPtr<IDXGIOutputDuplication> duplication; hr=output1->DuplicateOutput(device.Get(),&duplication);
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("DuplicateOutput（复制桌面输出）"),hr)); return false; }
    const QString deviceName=QString::fromWCharArray(desc.DeviceName);
    QImage frame; bool gotFrame=false;
    for (int attempt=1;attempt<=3;++attempt) {
        // 首帧给足等待；重试只等 300ms，避免黑帧时白等数秒。
        const UINT timeout=attempt==1?2500:300;
        DXGI_OUTDUPL_FRAME_INFO info{}; ComPtr<IDXGIResource> desktop;
        hr=duplication->AcquireNextFrame(timeout,&info,&desktop);
        if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("AcquireNextFrame"),hr)); break; }
        ComPtr<ID3D11Texture2D> texture; hr=desktop.As(&texture);
        if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("获取桌面纹理"),hr)); duplication->ReleaseFrame(); break; }
        D3D11_TEXTURE2D_DESC texDesc{}; texture->GetDesc(&texDesc);
        texDesc.Usage=D3D11_USAGE_STAGING; texDesc.BindFlags=0; texDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; texDesc.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging; hr=device->CreateTexture2D(&texDesc,nullptr,&staging);
        if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("创建暂存纹理"),hr)); duplication->ReleaseFrame(); break; }
        deviceContext->CopyResource(staging.Get(),texture.Get()); D3D11_MAPPED_SUBRESOURCE map{};
        hr=deviceContext->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
        if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("读取暂存纹理"),hr)); duplication->ReleaseFrame(); break; }
        QImage image(static_cast<const uchar *>(map.pData),int(texDesc.Width),int(texDesc.Height),int(map.RowPitch),QImage::Format_RGB32);
        frame=image.copy();
        for (int y=0;y<frame.height();++y) {
            auto *line=reinterpret_cast<QRgb *>(frame.scanLine(y));
            for (int x=0;x<frame.width();++x) line[x]|=0xff000000u;
        }
        deviceContext->Unmap(staging.Get(),0); duplication->ReleaseFrame();
        gotFrame=true;
        if (!looksBlack(frame)) break;
        const LumaStats stats=lumaStats(frame);
        appendDiagnosticLog(QStringLiteral("输出 %1 取到黑帧（第 %2/3 次尝试，平均灰度 %3，暗像素 %4%），释放后重取")
            .arg(deviceName).arg(attempt).arg(stats.average).arg(stats.darkPercent));
    }
    if (!gotFrame) return false;
    int rotation=0;
    if (desc.Rotation==DXGI_MODE_ROTATION_ROTATE90) rotation=90;
    if (desc.Rotation==DXGI_MODE_ROTATION_ROTATE180) rotation=180;
    if (desc.Rotation==DXGI_MODE_ROTATION_ROTATE270) rotation=270;
    if (rotation) frame=frame.transformed(QTransform().rotate(rotation));
    result=frame;
    return true;
}
// 捕获整个虚拟桌面：两遍式。
// 第一遍：枚举所有 DXGI 输出并用 matchScreen 配对收集（adapter/output 由本函数持有）。
// 窗口矩形按「所在屏」的物理↔逻辑对换算，混合 DPI 下不能用桌面级统一缩放。
// 第二遍：逐输出捕获；镜像输出物理矩形相同，复用同一帧，屏数仍与 Qt 报告一致。
// targets 是主线程取好的 Qt 屏幕快照，工作线程里不再碰 GUI 对象。
CaptureResult captureAllFrames(const QVector<ScreenTarget> &targets) {
    appendDiagnosticLog(QStringLiteral("captureAllFrames 开始：Qt 屏 %1 块").arg(targets.size()));
    CaptureResult result;
    ComPtr<IDXGIFactory1> factory; HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { appendDiagnosticLog(failure(QStringLiteral("CreateDXGIFactory1"),hr));
        result.error=failure(QStringLiteral("无法初始化屏幕捕获"),hr); return result; }
    struct OutputItem { ComPtr<IDXGIAdapter1> adapter; ComPtr<IDXGIOutput> output; DXGI_OUTPUT_DESC desc; QString deviceName; QRect physical; QRect logical; };
    QVector<OutputItem> items;
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
            const bool matched=matchScreen(targets,deviceName,physical,logical);
            appendDiagnosticLog(QStringLiteral("适配器「%1」输出 %2：attached=%3 物理(%4,%5 %6x%7) 匹配=%8")
                .arg(adapterName,deviceName).arg(desc.AttachedToDesktop)
                .arg(physical.left()).arg(physical.top()).arg(physical.width()).arg(physical.height())
                .arg(matched
                    ? QStringLiteral("是，逻辑(%1,%2 %3x%4)").arg(logical.left()).arg(logical.top()).arg(logical.width()).arg(logical.height())
                    : QStringLiteral("否")));
            if (desc.AttachedToDesktop && matched) {
                // ComPtr 带用户定义构造函数，OutputItem 不是聚合类型，需逐成员赋值。
                OutputItem item;
                item.adapter=adapter; item.output=output; item.desc=desc;
                item.deviceName=deviceName; item.physical=physical; item.logical=logical;
                items.append(item);
            }
            output.Reset();
        }
        adapter.Reset();
    }
    // 虚拟桌面：物理矩形来自系统，逻辑矩形来自 Qt 的屏幕列表。
    const QRect physicalVirtual(GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),
                                GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN));
    QRect logicalVirtual;
    for (const ScreenTarget &target : targets)
        logicalVirtual=logicalVirtual.isNull()?target.logical:logicalVirtual.united(target.logical);
    appendDiagnosticLog(QStringLiteral("虚拟桌面：物理(%1,%2 %3x%4) 逻辑(%5,%6 %7x%8)")
        .arg(physicalVirtual.left()).arg(physicalVirtual.top()).arg(physicalVirtual.width()).arg(physicalVirtual.height())
        .arg(logicalVirtual.left()).arg(logicalVirtual.top()).arg(logicalVirtual.width()).arg(logicalVirtual.height()));
    QVector<ScreenPair> pairs;
    for (const OutputItem &item : items) pairs.append(ScreenPair{item.physical,item.logical});
    if (!logicalVirtual.isEmpty() && !physicalVirtual.isEmpty()) {
        WindowListContext list{RECT{physicalVirtual.left(),physicalVirtual.top(),physicalVirtual.right()+1,physicalVirtual.bottom()+1},logicalVirtual,pairs,{}};
        EnumWindows(collectWindow,reinterpret_cast<LPARAM>(&list));
        result.frame.windowRects=list.result;
        appendDiagnosticLog(QStringLiteral("窗口枚举完成：%1 个").arg(list.result.size()));
    }
    // 第二遍：逐输出捕获。DXGI 在多显卡笔记本上可能复制到黑帧，
    // 因此先各自捕获并统计黑帧，必要时用 GDI 兜底补齐，再做镜像去重。
    QVector<QImage> images(items.size());
    for (int i=0;i<items.size();++i)
        if (!captureOutput(items[i].adapter.Get(),items[i].output,items[i].desc,images[i]))
            appendDiagnosticLog(QStringLiteral("输出 %1 捕获失败，未拿到画面").arg(items[i].deviceName));
    int blackCount=0;
    for (int i=0;i<items.size();++i)
        if (images[i].isNull() || looksBlack(images[i])) ++blackCount;
    if (blackCount>0) {
        appendDiagnosticLog(QStringLiteral("检测到 %1 块屏内容为黑或缺失，尝试 GDI 兜底抓取整桌面").arg(blackCount));
        const QImage gdi=captureDesktopWithGdi();
        if (gdi.isNull()) appendDiagnosticLog(QStringLiteral("GDI 兜底未取得画面，保留 DXGI 结果"));
        else if (gdi.size()!=physicalVirtual.size())
            appendDiagnosticLog(QStringLiteral("GDI 兜底尺寸 %1x%2 与物理虚拟桌面 %3x%4 不一致（可能是 DPI 虚拟化），放弃兜底")
                .arg(gdi.width()).arg(gdi.height()).arg(physicalVirtual.width()).arg(physicalVirtual.height()));
        else {
            for (int i=0;i<items.size();++i) {
                if (!images[i].isNull() && !looksBlack(images[i])) continue;
                const QRect source=items[i].physical.translated(-physicalVirtual.topLeft()).intersected(gdi.rect());
                if (source.isEmpty()) { appendDiagnosticLog(QStringLiteral("GDI 兜底：输出 %1 的物理矩形不在整桌面范围内").arg(items[i].deviceName)); continue; }
                const QImage piece=gdi.copy(source);
                if (piece.size()!=items[i].physical.size()) {
                    appendDiagnosticLog(QStringLiteral("GDI 兜底切片尺寸不符：%1x%2，期望 %3x%4")
                        .arg(piece.width()).arg(piece.height()).arg(items[i].physical.width()).arg(items[i].physical.height()));
                    continue;
                }
                images[i]=piece;
                const LumaStats stats=lumaStats(piece);
                appendDiagnosticLog(QStringLiteral("GDI 兜底补齐输出 %1：%2x%3，平均灰度 %4，暗像素 %5%")
                    .arg(items[i].deviceName).arg(piece.width()).arg(piece.height()).arg(stats.average).arg(stats.darkPercent));
            }
        }
    }
    // 镜像输出：物理矩形相同则复用同一帧，屏数仍与 Qt 报告一致。
    QVector<QRect> seenPhysical;
    for (int i=0;i<items.size();++i) {
        if (images[i].isNull()) continue;
        QImage image=images[i];
        int reuse=-1;
        for (int j=0;j<seenPhysical.size();++j)
            if (seenPhysical[j]==items[i].physical) { reuse=j; break; }
        if (reuse>=0) {
            image=images[reuse];
            appendDiagnosticLog(QStringLiteral("输出 %1 与其他输出物理矩形相同，复用同一帧").arg(items[i].deviceName));
        } else seenPhysical.append(items[i].physical);
        result.frame.screens.append(ScreenImage{items[i].logical,image});
        const LumaStats stats=lumaStats(image);
        appendDiagnosticLog(QStringLiteral("输出 %1 捕获成功：%2x%3，平均灰度 %4，暗像素 %5%")
            .arg(items[i].deviceName).arg(image.width()).arg(image.height()).arg(stats.average).arg(stats.darkPercent));
    }
    if (result.frame.screens.isEmpty()) {
        appendDiagnosticLog(QStringLiteral("captureAllFrames 失败：没有捕获到任何输出"));
        result.error=QStringLiteral("无法识别当前显示器，请重新截图。"); return result;
    }
    // 全黑时不给出画面：驱动复制不到内容，用户看到的只会是一块黑屏。
    // 明确报错比无声黑屏好，日志里保留灰度数据供排查。
    int stillBlack=0;
    for (const ScreenImage &screen : result.frame.screens)
        if (looksBlack(screen.image)) ++stillBlack;
    if (stillBlack==result.frame.screens.size()) {
        appendDiagnosticLog(QStringLiteral("captureAllFrames 完成：%1 块屏幕，但内容全部为黑").arg(result.frame.screens.size()));
        result.error=QStringLiteral("显卡驱动返回了黑画面，无法截图。请更新显卡驱动后重试。");
        return result;
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
    // DPI 感知级别：GDI 兜底只有在按物理像素工作（每显示器感知）时才对得上 DXGI 坐标。
    appendDiagnosticLog(QStringLiteral("进程 DPI 感知级别：%1（0=未感知 1=系统 2=每显示器）")
        .arg(int(GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()))));
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
