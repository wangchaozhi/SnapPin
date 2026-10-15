#include "macplatform.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <algorithm>
#include <thread>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace {

constexpr int kMaxElementsPerWindow = 4000;
constexpr int kMaxDepth = 40;
constexpr float kAxTimeoutSec = 0.3f; // 单次跨进程调用的超时，卡住的程序不会拖住识别线程

QRect toQRect(CGRect r)
{
    return QRectF(r.origin.x, r.origin.y, r.size.width, r.size.height).toAlignedRect();
}

QRect toScreenLocal(const QRect& globalRect, const QRect& screenRect)
{
    return (globalRect & screenRect).translated(-screenRect.topLeft());
}

// 菜单栏、状态栏图标、Dock 也识别；更高层的（通知、屏保、各种全屏覆盖层）不识别
bool acceptedLayer(int layer)
{
    return layer >= 0 && layer <= int(kCGStatusWindowLevel);
}

QList<MacWindowFinder::TopLevel> enumerateWindows()
{
    QList<MacWindowFinder::TopLevel> result;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                 kCGNullWindowID);
    if (!list)
        return result;
    const int self = int(getpid());
    auto* windows = (__bridge NSArray<NSDictionary*>*)list;
    for (NSDictionary* info in windows) { // 按 Z 序从上到下
        const int pid = [info[(__bridge NSString*)kCGWindowOwnerPID] intValue];
        if (pid == self)
            continue;
        if (!acceptedLayer([info[(__bridge NSString*)kCGWindowLayer] intValue]))
            continue;
        NSNumber* alpha = info[(__bridge NSString*)kCGWindowAlpha];
        if (alpha && alpha.doubleValue <= 0.01)
            continue;
        CGRect bounds{};
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds], &bounds))
            continue;
        const QRect r = toQRect(bounds);
        if (r.width() < 4 || r.height() < 4)
            continue;
        result.append({[info[(__bridge NSString*)kCGWindowNumber] unsignedIntValue], pid, r});
    }
    CFRelease(list);
    return result;
}

bool axRect(AXUIElementRef el, QRect* out)
{
    CFTypeRef posValue = nullptr, sizeValue = nullptr;
    bool ok = false;
    if (AXUIElementCopyAttributeValue(el, kAXPositionAttribute, &posValue) == kAXErrorSuccess
        && AXUIElementCopyAttributeValue(el, kAXSizeAttribute, &sizeValue) == kAXErrorSuccess) {
        CGPoint pos{};
        CGSize size{};
        ok = AXValueGetValue(AXValueRef(posValue), AXValueType(kAXValueCGPointType), &pos)
             && AXValueGetValue(AXValueRef(sizeValue), AXValueType(kAXValueCGSizeType), &size);
        if (ok)
            *out = toQRect(CGRectMake(pos.x, pos.y, size.width, size.height));
    }
    if (posValue)
        CFRelease(posValue);
    if (sizeValue)
        CFRelease(sizeValue);
    return ok;
}

// 遍历窗口的辅助功能树，收集可见控件的全局逻辑坐标矩形
QList<QRect> collectAxElements(int pid, const QRect& windowRect, int maxCount)
{
    QList<QRect> rects;
    AXUIElementRef app = AXUIElementCreateApplication(pid_t(pid));
    if (!app)
        return rects;
    AXUIElementSetMessagingTimeout(app, kAxTimeoutSec);

    // CGWindowID 与 AX 窗口没有公开的对应关系，按位置大小匹配
    AXUIElementRef window = nullptr;
    CFArrayRef windows = nullptr;
    if (AXUIElementCopyAttributeValue(app, kAXWindowsAttribute, (CFTypeRef*)&windows) == kAXErrorSuccess && windows) {
        for (CFIndex i = 0; i < CFArrayGetCount(windows) && !window; ++i) {
            auto el = AXUIElementRef(CFArrayGetValueAtIndex(windows, i));
            QRect r;
            if (axRect(el, &r) && qAbs(r.x() - windowRect.x()) <= 2 && qAbs(r.y() - windowRect.y()) <= 2
                && qAbs(r.width() - windowRect.width()) <= 2 && qAbs(r.height() - windowRect.height()) <= 2)
                window = AXUIElementRef(CFRetain(el));
        }
        CFRelease(windows);
    }
    CFRelease(app);
    if (!window)
        return rects;

    QList<QPair<AXUIElementRef, int>> queue{{window, 0}}; // 广度优先，元素太多时优先保留外层控件
    for (qsizetype head = 0; head < queue.size(); ++head) {
        const auto [el, depth] = queue[head];
        if (rects.size() < maxCount && depth < kMaxDepth) {
            CFArrayRef children = nullptr;
            if (AXUIElementCopyAttributeValue(el, kAXChildrenAttribute, (CFTypeRef*)&children) == kAXErrorSuccess && children) {
                for (CFIndex i = 0; i < CFArrayGetCount(children); ++i) {
                    auto child = AXUIElementRef(CFArrayGetValueAtIndex(children, i));
                    QRect r;
                    // 只保留在窗口内的可见控件；滚出视野的列表项坐标在窗口外
                    if (!axRect(child, &r) || r.width() < 4 || r.height() < 4 || !windowRect.intersects(r))
                        continue;
                    rects.append(r & windowRect);
                    queue.append({AXUIElementRef(CFRetain(child)), depth + 1});
                }
                CFRelease(children);
            }
        }
        CFRelease(el);
    }
    return rects;
}

} // namespace

QList<DetectedWindow> MacWindowFinder::windowsOnScreen(QScreen* screen)
{
    m_topLevels = enumerateWindows();
    const QRect screenRect = screen->geometry();
    QList<DetectedWindow> result;
    for (int i = 0; i < m_topLevels.size(); ++i) {
        const QRect local = toScreenLocal(m_topLevels[i].rect, screenRect);
        if (!local.isEmpty())
            result.append({local, i, false});
    }
    return result;
}

void MacWindowFinder::requestElements(QObject* context, ElementsCallback onReady)
{
    if (!m_elementDetection || !macAccessibilityTrusted(true))
        return;

    // 只细化最可能被截的窗口：光标下的窗口和前台程序最上层的窗口
    const QPoint cursor = QCursor::pos();
    const int frontPid = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
    const auto underCursor = std::find_if(m_topLevels.cbegin(), m_topLevels.cend(),
                                          [&](const TopLevel& t) { return t.rect.contains(cursor); });
    const auto front = std::find_if(m_topLevels.cbegin(), m_topLevels.cend(),
                                    [&](const TopLevel& t) { return t.pid == frontPid; });
    QList<QPair<int, TopLevel>> jobs;
    for (auto it : {underCursor, front}) {
        const int group = int(it - m_topLevels.cbegin());
        if (it != m_topLevels.cend() && !std::any_of(jobs.cbegin(), jobs.cend(), [group](const auto& j) { return j.first == group; }))
            jobs.append({group, *it});
    }
    if (jobs.isEmpty())
        return;

    // 辅助功能 API 是跨进程调用，复杂程序可能要几百毫秒，放到后台线程，结果到达后再合并
    QPointer<QObject> guard(context);
    std::thread([jobs, guard, onReady] {
        QList<QPair<int, QList<QRect>>> found;
        for (const auto& [group, top] : jobs)
            found.append({group, collectAxElements(top.pid, top.rect, kMaxElementsPerWindow)});

        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, onReady, found] {
            if (!guard)
                return;
            for (QScreen* screen : QGuiApplication::screens()) {
                const QRect screenRect = screen->geometry();
                QList<DetectedWindow> list;
                for (const auto& [group, rects] : found) {
                    for (const QRect& r : rects) {
                        const QRect local = toScreenLocal(r, screenRect);
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
