#include "winplatform.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <algorithm>
#include <atomic>
#include <memory>
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

    // 所有看得见的顶层窗口都细化到控件（按 Z 序，光标下的窗口最先）；被更上层窗口完全盖住的跳过
    POINT pt{};
    GetCursorPos(&pt);
    const HWND underCursor = GetAncestor(WindowFromPoint(pt), GA_ROOT);
    QList<QPair<int, HWND>> jobs;
    QList<QRect> above;
    for (int group = 0; group < m_topLevels.size(); ++group) {
        const HWND h = m_topLevels[group];
        const QRect r = windowRect(h, true);
        const bool covered = std::any_of(above.cbegin(), above.cend(), [&r](const QRect& a) { return a.contains(r); });
        above.append(r);
        if (covered || isOwnWindow(h))
            continue;
        if (h == underCursor)
            jobs.prepend({group, h});
        else
            jobs.append({group, h});
    }
    if (jobs.isEmpty())
        return;

    // 会话结束（遮罩关闭）后后台线程尽快停下，不再白白查询
    auto alive = std::make_shared<std::atomic_bool>(true);
    QObject::connect(context, &QObject::destroyed, [alive] { *alive = false; });

    // UI Automation 是跨进程调用，复杂应用可能要几百毫秒，放到后台线程；每查完一个窗口就合并进去，识别逐步变细
    QPointer<QObject> guard(context);
    std::thread([jobs, guard, onReady, alive] {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (const auto& [group, hwnd] : jobs) {
            if (!*alive)
                break;
            // 可滚动内容等控件可能比窗口还大，裁到窗口范围内，框选不会超出窗口
            const QRect bounds = windowRect(hwnd, true);
            QList<QRect> rects;
            for (const QRect& r : collectUiaElements(hwnd, kMaxElementsPerWindow)) {
                const QRect clipped = r & bounds;
                if (clipped.width() >= 4 && clipped.height() >= 4)
                    rects.append(clipped);
            }
            if (rects.isEmpty())
                continue;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, onReady, group, rects] {
                if (!guard)
                    return;
                for (QScreen* screen : QGuiApplication::screens()) {
                    const QRect phys = physicalScreenRect(screen);
                    const qreal dpr = screen->devicePixelRatio();
                    QList<DetectedWindow> list;
                    for (const QRect& r : rects) {
                        const QRect local = toScreenLocal(r, phys, dpr);
                        if (!local.isEmpty())
                            list.append({local, group, true});
                    }
                    if (!list.isEmpty())
                        onReady(screen, list);
                }
            }, Qt::QueuedConnection);
        }
        if (SUCCEEDED(co))
            CoUninitialize();
    }).detach();
}
