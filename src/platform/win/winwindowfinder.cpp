#include "winplatform.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <algorithm>
#include <thread>

#include <dwmapi.h>
#include <objbase.h> // CoInitializeEx（WIN32_LEAN_AND_MEAN 下 windows.h 不包含）

namespace {

constexpr int kMaxElementsPerWindow = 4000;

struct RawWindow {
    QRect rect; // 物理像素，虚拟桌面坐标
    int group;
    bool isChild;
};

struct EnumContext {
    QList<RawWindow>* out;
    QList<HWND>* topLevels;
};

bool isCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

QRect windowRect(HWND hwnd, bool topLevel)
{
    RECT r{};
    // 顶层窗口用 DWM 扩展边框，去掉 Win10/11 的透明阴影区域
    if (!topLevel || FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

BOOL CALLBACK enumChild(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<EnumContext*>(lp);
    if (!IsWindowVisible(hwnd))
        return TRUE;
    const QRect r = windowRect(hwnd, false);
    if (r.width() >= 4 && r.height() >= 4)
        ctx->out->append({r, int(ctx->topLevels->size()) - 1, true});
    return TRUE;
}

BOOL CALLBACK enumTopLevel(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<EnumContext*>(lp);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || isCloaked(hwnd))
        return TRUE;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) // 鼠标穿透的覆盖层不参与识别
        return TRUE;
    if (isInvisibleOverlayWindow(hwnd))
        return TRUE;
    const QRect r = windowRect(hwnd, true);
    if (r.width() < 4 || r.height() < 4)
        return TRUE;

    ctx->topLevels->append(hwnd);
    ctx->out->append({r, int(ctx->topLevels->size()) - 1, false});
    EnumChildWindows(hwnd, enumChild, lp);
    return TRUE;
}

// 物理像素矩形 -> 屏幕局部逻辑坐标；不在该屏上返回空矩形
QRect toScreenLocal(const QRect& physRect, const QRect& screenPhys, qreal dpr)
{
    const QRect clipped = physRect & screenPhys;
    if (clipped.isEmpty())
        return {};
    return QRectF((clipped.x() - screenPhys.x()) / dpr, (clipped.y() - screenPhys.y()) / dpr,
                  clipped.width() / dpr, clipped.height() / dpr).toRect();
}

bool isOwnWindow(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

} // namespace

bool isInvisibleOverlayWindow(HWND hwnd)
{
    const LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED))
        return false;
    // 整体透明度为 0（如 NVIDIA GeForce 叠加层）
    BYTE alpha = 255;
    DWORD flags = 0;
    COLORREF key = 0;
    if (GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0)
        return true;
    // 逐像素透明的工具窗口铺满整块屏幕：几乎都是看不见也点不到的覆盖层（各种托盘程序的消息层等）。
    // 正常的全屏程序不会是分层的工具窗口
    if (!(ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))
        return false;
    RECT r{};
    if (!GetWindowRect(hwnd, &r))
        return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    const RECT& m = mi.rcMonitor;
    return r.left <= m.left && r.top <= m.top && r.right >= m.right && r.bottom >= m.bottom;
}

QList<DetectedWindow> WinWindowFinder::windowsOnScreen(QScreen* screen)
{
    QList<RawWindow> raw;
    m_topLevels.clear();
    EnumContext ctx{&raw, &m_topLevels};
    EnumWindows(enumTopLevel, reinterpret_cast<LPARAM>(&ctx)); // 按 Z 序从上到下

    const QRect phys = physicalScreenRect(screen);
    const qreal dpr = screen->devicePixelRatio();

    QList<DetectedWindow> result;
    for (const RawWindow& w : raw) {
        const QRect local = toScreenLocal(w.rect, phys, dpr);
        if (!local.isEmpty())
            result.append({local, w.group, w.isChild});
    }
    return result;
}

void WinWindowFinder::requestElements(QObject* context, ElementsCallback onReady)
{
    if (!m_elementDetection)
        return;

    // 只细化最可能被截的窗口：光标下的窗口和前台窗口
    POINT pt{};
    GetCursorPos(&pt);
    QList<QPair<int, HWND>> jobs;
    for (HWND h : {GetAncestor(WindowFromPoint(pt), GA_ROOT), GetForegroundWindow()}) {
        const int group = int(m_topLevels.indexOf(h));
        if (h && group >= 0 && !isOwnWindow(h) && !std::any_of(jobs.cbegin(), jobs.cend(), [h](const auto& j) { return j.second == h; }))
            jobs.append({group, h});
    }
    if (jobs.isEmpty())
        return;

    // UI Automation 是跨进程调用，复杂应用可能要几百毫秒，放到后台线程，结果到达后再合并
    QPointer<QObject> guard(context);
    std::thread([jobs, guard, onReady] {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        QList<QPair<int, QList<QRect>>> found;
        for (const auto& job : jobs)
            found.append({job.first, collectUiaElements(job.second, kMaxElementsPerWindow)});
        if (SUCCEEDED(co))
            CoUninitialize();

        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, onReady, found] {
            if (!guard)
                return;
            for (QScreen* screen : QGuiApplication::screens()) {
                const QRect phys = physicalScreenRect(screen);
                const qreal dpr = screen->devicePixelRatio();
                QList<DetectedWindow> list;
                for (const auto& [group, rects] : found) {
                    for (const QRect& r : rects) {
                        const QRect local = toScreenLocal(r, phys, dpr);
                        if (!local.isEmpty())
                            list.append({local, group, true});
                    }
                }
                if (!list.isEmpty())
                    onReady(screen, list);
            }
        }, Qt::QueuedConnection);
    }).detach();
}
