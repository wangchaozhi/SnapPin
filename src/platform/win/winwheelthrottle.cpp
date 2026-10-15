#include "winplatform.h"

#include <QScreen>
#include <atomic>
#include <future>
#include <mutex>
#include <vector>

#include <dwmapi.h>

namespace {

constexpr ULONG_PTR kReplayMarker = 0x534E5050;   // 'SNPP'：自己注入的滚轮事件
constexpr int kMaxQueuedDelta = WHEEL_DELTA * 60; // 最多排队 60 格，防止回放太久

std::atomic<int> g_pending{0};

// 接管范围：选区 ∪ 目标窗口，减去 SnapPin 自己的窗口（面板、边框）。只在 start() 时写入
struct Area {
    RECT region{};
    RECT target{};
    std::vector<RECT> own;
};
Area g_area;
std::mutex g_areaMutex;

bool contains(const RECT& r, POINT pt)
{
    return pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom;
}

bool intercepts(POINT pt)
{
    std::lock_guard lock(g_areaMutex);
    for (const RECT& r : g_area.own) {
        if (contains(r, pt))
            return false;
    }
    return contains(g_area.region, pt) || contains(g_area.target, pt);
}

LRESULT CALLBACK wheelHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == WM_MOUSEWHEEL) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        if (info->dwExtraInfo != kReplayMarker && intercepts(info->pt)) {
            const int delta = short(HIWORD(info->mouseData));
            int cur = g_pending.load();
            while (!g_pending.compare_exchange_weak(cur, qBound(-kMaxQueuedDelta, cur + delta, kMaxQueuedDelta))) {
            }
            return 1; // 吞掉，由截图会话按节奏重放
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

bool isOwnProcess(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

// 目标进程以管理员身份运行而我们不是时，系统会静默丢弃注入的输入，此时不能接管
bool isElevatedOtherProcess(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return true;
    bool elevated = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(proc, TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION e{};
        DWORD size = 0;
        elevated = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size) && e.TokenIsElevated;
        CloseHandle(token);
    } else {
        elevated = true; // 连查询都被拒绝，按高权限处理
    }
    CloseHandle(proc);
    if (!elevated)
        return false;
    // 我们自己也是管理员时不受影响
    HANDLE self = nullptr;
    TOKEN_ELEVATION mine{};
    DWORD size = 0;
    const bool selfElevated = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &self)
                              && GetTokenInformation(self, TokenElevation, &mine, sizeof(mine), &size) && mine.TokenIsElevated;
    if (self)
        CloseHandle(self);
    return !selfElevated;
}

struct FindContext {
    POINT pt;
    HWND target = nullptr;
    std::vector<RECT> own;
};

BOOL CALLBACK collectWindows(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<FindContext*>(lp);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;
    RECT r{};
    const bool clickThrough = GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT;
    if (isOwnProcess(hwnd)) {
        // 只排除会接收鼠标的自有窗口（控制面板）；选区边框是鼠标穿透的，盖住整个选区
        if (!clickThrough) {
            GetWindowRect(hwnd, &r);
            ctx->own.push_back(r);
        }
        return TRUE;
    }
    DWORD cloaked = 0;
    if (ctx->target || clickThrough
        || (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked))
        return TRUE;
    GetWindowRect(hwnd, &r);
    if (contains(r, ctx->pt))
        ctx->target = hwnd; // 按 Z 序第一个包含选区中心的窗口
    return TRUE;
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
    FindContext ctx;
    ctx.pt = {r.center().x(), r.center().y()};
    EnumWindows(collectWindows, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.target && isElevatedOtherProcess(ctx.target))
        return false;

    {
        std::lock_guard lock(g_areaMutex);
        g_area.region = {r.left(), r.top(), r.left() + r.width(), r.top() + r.height()};
        g_area.target = {};
        if (ctx.target)
            GetWindowRect(ctx.target, &g_area.target);
        g_area.own = std::move(ctx.own);
    }
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

void WinWheelThrottle::clear()
{
    g_pending = 0;
}

void WinWheelThrottle::requeue(int delta)
{
    int cur = g_pending.load();
    while (!g_pending.compare_exchange_weak(cur, qBound(-kMaxQueuedDelta, cur + delta, kMaxQueuedDelta))) {
    }
}

bool WinWheelThrottle::inject(int delta)
{
    POINT pt{};
    GetCursorPos(&pt);
    if (delta == 0 || !intercepts(pt))
        return false;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = DWORD(delta);
    in.mi.dwExtraInfo = kReplayMarker; // 钩子据此放行
    return SendInput(1, &in, sizeof(in)) == 1;
}

bool WinWheelThrottle::replay(int maxDelta)
{
    POINT pt{};
    GetCursorPos(&pt);
    if (!intercepts(pt)) { // 用户把鼠标移走了，剩下的滚动不再补发
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
