#include "macplatform.h"

#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <atomic>
#include <future>
#include <mutex>
#include <vector>

#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

namespace {

constexpr int64_t kReplayMarker = 0x534E5050; // 'SNPP'：自己注入的滚动事件
constexpr int kUnitsPerPixel = 3;              // 接口按 Windows 滚轮单位（一格 120）计量，约等于 40 像素
constexpr int kMaxQueuedDelta = 120 * 60;      // 最多排队 60 格，防止回放太久

std::atomic<int> g_pending{0};
CFMachPortRef g_tap = nullptr; // 只在 tap 线程和 start/stop 之间交接

// 接管范围：选区 ∪ 目标窗口，减去 SnapPin 自己接收鼠标的窗口（面板）。全局逻辑坐标，只在 start() 时写入
struct Area {
    QRectF region;
    QRectF target;
    std::vector<QRectF> own;
};
Area g_area;
std::mutex g_areaMutex;

bool intercepts(CGPoint pt)
{
    const QPointF p(pt.x, pt.y);
    std::lock_guard lock(g_areaMutex);
    for (const QRectF& r : g_area.own) {
        if (r.contains(p))
            return false;
    }
    return g_area.region.contains(p) || g_area.target.contains(p);
}

CGPoint cursorPos()
{
    CGEventRef e = CGEventCreate(nullptr);
    const CGPoint pt = CGEventGetLocation(e);
    CFRelease(e);
    return pt;
}

void addPending(int delta)
{
    int cur = g_pending.load();
    while (!g_pending.compare_exchange_weak(cur, qBound(-kMaxQueuedDelta, cur + delta, kMaxQueuedDelta))) {
    }
}

CGEventRef onEvent(CGEventTapProxy, CGEventType type, CGEventRef event, void*)
{
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (g_tap)
            CGEventTapEnable(g_tap, true);
        return event;
    }
    if (type != kCGEventScrollWheel || CGEventGetIntegerValueField(event, kCGEventSourceUserData) == kReplayMarker
        || !intercepts(CGEventGetLocation(event)))
        return event;
    // 触控板和鼠标都有像素增量（已含系统加速和“自然滚动”方向），正数向上
    int px = int(CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1));
    if (px == 0)
        px = int(CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1)) * 10;
    addPending(px * kUnitsPerPixel);
    return nullptr; // 吞掉（包括惯性滚动），由截图会话按节奏重放
}

// 光标下第一个不属于本进程的普通窗口（按 Z 序）
QRectF targetWindowAt(const QPointF& pt)
{
    QRectF found;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                 kCGNullWindowID);
    if (!list)
        return found;
    const int self = int(getpid());
    for (NSDictionary* info in (__bridge NSArray<NSDictionary*>*)list) {
        if ([info[(__bridge NSString*)kCGWindowOwnerPID] intValue] == self
            || [info[(__bridge NSString*)kCGWindowLayer] intValue] != 0)
            continue;
        CGRect b{};
        if (CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds], &b)
            && CGRectContainsPoint(b, CGPointMake(pt.x(), pt.y()))) {
            found = QRectF(b.origin.x, b.origin.y, b.size.width, b.size.height);
            break;
        }
    }
    CFRelease(list);
    return found;
}

} // namespace

MacWheelThrottle::~MacWheelThrottle()
{
    stop();
}

bool MacWheelThrottle::start(QScreen* screen, const QRect& localPhysRect)
{
    stop();
    if (!macAccessibilityTrusted(true))
        return false;

    const qreal dpr = screen->devicePixelRatio();
    const QRectF region = QRectF(QPointF(localPhysRect.topLeft()) / dpr, QSizeF(localPhysRect.size()) / dpr)
                              .translated(screen->geometry().topLeft());
    {
        std::lock_guard lock(g_areaMutex);
        g_area.region = region;
        g_area.target = targetWindowAt(region.center());
        g_area.own.clear();
        // 只排除会接收鼠标的自有窗口（控制面板）；选区边框是鼠标穿透的，盖住整个选区
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w->isVisible() && !(w->windowFlags() & Qt::WindowTransparentForInput))
                g_area.own.push_back(w->frameGeometry());
        }
    }
    g_pending = 0;

    std::promise<CFRunLoopRef> ready;
    auto readyFuture = ready.get_future();
    m_thread = std::thread([&ready] {
        // 独立线程：主线程忙于截图拼接时，事件 tap 也不会因超时被系统禁用
        g_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                                 CGEventMaskBit(kCGEventScrollWheel), onEvent, nullptr);
        if (!g_tap) {
            ready.set_value(nullptr);
            return;
        }
        CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(nullptr, g_tap, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        CGEventTapEnable(g_tap, true);
        ready.set_value(CFRunLoopGetCurrent());
        CFRunLoopRun();
        CGEventTapEnable(g_tap, false);
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        CFRelease(source);
        CFRelease(g_tap);
        g_tap = nullptr;
    });
    m_runLoop = readyFuture.get();
    if (!m_runLoop) {
        m_thread.join();
        return false;
    }
    return true;
}

void MacWheelThrottle::stop()
{
    if (!m_thread.joinable())
        return;
    if (m_runLoop)
        CFRunLoopStop(CFRunLoopRef(m_runLoop));
    m_thread.join();
    m_runLoop = nullptr;
    g_pending = 0;
}

int MacWheelThrottle::pendingDelta() const
{
    return g_pending.load();
}

void MacWheelThrottle::clear()
{
    g_pending = 0;
}

void MacWheelThrottle::requeue(int delta)
{
    addPending(delta);
}

bool MacWheelThrottle::inject(int delta)
{
    const int px = delta / kUnitsPerPixel;
    if (px == 0 || !intercepts(cursorPos()))
        return false;
    CGEventRef e = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitPixel, 1, int32_t(px));
    if (!e)
        return false;
    CGEventSetIntegerValueField(e, kCGEventSourceUserData, kReplayMarker); // tap 据此放行
    CGEventPost(kCGSessionEventTap, e);
    CFRelease(e);
    return true;
}

bool MacWheelThrottle::replay(int maxDelta)
{
    if (!intercepts(cursorPos())) { // 用户把鼠标移走了，剩下的滚动不再补发
        g_pending = 0;
        return false;
    }
    int cur = g_pending.load();
    int chunk = 0;
    do {
        if (cur == 0)
            return false;
        chunk = cur > 0 ? qMin(cur, maxDelta) : qMax(cur, -maxDelta);
    } while (!g_pending.compare_exchange_weak(cur, cur - chunk));
    return inject(chunk);
}
