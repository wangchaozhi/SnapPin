#include "winplatform.h"

#include <QScreen>
#include <atomic>
#include <future>

namespace {

constexpr ULONG_PTR kReplayMarker = 0x534E5050; // 'SNPP'：自己注入的滚轮事件
constexpr int kMaxQueuedDelta = WHEEL_DELTA * 60; // 最多排队 60 格，防止回放太久

std::atomic<int> g_pending{0};
std::atomic<LONG> g_left{0}, g_top{0}, g_right{0}, g_bottom{0};

bool inRegion(POINT pt)
{
    return pt.x >= g_left && pt.x < g_right && pt.y >= g_top && pt.y < g_bottom;
}

LRESULT CALLBACK wheelHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == WM_MOUSEWHEEL) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        if (info->dwExtraInfo != kReplayMarker && inRegion(info->pt)) {
            const int delta = short(HIWORD(info->mouseData));
            int cur = g_pending.load();
            while (!g_pending.compare_exchange_weak(cur, qBound(-kMaxQueuedDelta, cur + delta, kMaxQueuedDelta))) {
            }
            return 1; // 吞掉，由截图会话按节奏重放
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

} // namespace

WinWheelThrottle::~WinWheelThrottle()
{
    stop();
}

bool WinWheelThrottle::start(QScreen* screen, const QRect& localPhysRect)
{
    stop();
    const QRect r = localPhysRect.translated(physicalScreenRect(screen).topLeft());
    g_left = r.left();
    g_top = r.top();
    g_right = r.left() + r.width();
    g_bottom = r.top() + r.height();
    g_pending = 0;

    std::promise<DWORD> ready;
    auto readyFuture = ready.get_future();
    m_thread = std::thread([&ready] {
        MSG msg;
        PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE); // 先建好消息队列，PostThreadMessage 才能送达
        HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, wheelHook, GetModuleHandleW(nullptr), 0);
        ready.set_value(hook ? GetCurrentThreadId() : 0);
        if (!hook)
            return;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        }
        UnhookWindowsHookEx(hook);
    });
    m_threadId = readyFuture.get();
    if (!m_threadId) {
        m_thread.join();
        return false;
    }
    return true;
}

void WinWheelThrottle::stop()
{
    if (!m_thread.joinable())
        return;
    if (m_threadId)
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
    m_thread.join();
    m_threadId = 0;
    g_pending = 0;
}

int WinWheelThrottle::pendingDelta() const
{
    return g_pending.load();
}

bool WinWheelThrottle::replay(int maxDelta)
{
    POINT pt{};
    GetCursorPos(&pt);
    if (!inRegion(pt)) { // 用户把鼠标移走了，剩下的滚动不再补发
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

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = DWORD(chunk);
    in.mi.dwExtraInfo = kReplayMarker;
    return SendInput(1, &in, sizeof(in)) == 1;
}
