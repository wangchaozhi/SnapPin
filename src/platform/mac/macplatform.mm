#include "macplatform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QWidget>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace {

const QString kAgentLabel = QStringLiteral("com.github.wangchaozhi.SnapPin");

QString agentPath()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + kAgentLabel + QStringLiteral(".plist");
}

QString agentPlist()
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n<dict>\n"
               "    <key>Label</key><string>%1</string>\n"
               "    <key>ProgramArguments</key><array><string>%2</string></array>\n"
               "    <key>RunAtLoad</key><true/>\n"
               "    <key>ProcessType</key><string>Interactive</string>\n"
               "</dict>\n</plist>\n")
        .arg(kAgentLabel, QCoreApplication::applicationFilePath().toHtmlEscaped());
}

NSWindow* nativeWindow(QWidget* w)
{
    auto* view = (__bridge NSView*)reinterpret_cast<void*>(w->winId());
    return view.window;
}

void applyLayer(QWidget* w, FloatingLayer layer)
{
    NSWindow* win = nativeWindow(w);
    if (!win)
        return;
    win.hidesOnDeactivate = NO;
    // 跟随所有桌面空间，也能出现在其他程序的全屏空间里
    // （Qt 给 Tool 窗口设的 MoveToActiveSpace 与 CanJoinAllSpaces 互斥，同时设置会抛异常）
    win.collectionBehavior = (win.collectionBehavior & ~NSWindowCollectionBehaviorMoveToActiveSpace)
                             | NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    if (layer == FloatingLayer::Pin) {
        win.level = NSFloatingWindowLevel;
        return;
    }
    // 高于菜单栏、Dock，低于弹出菜单（工具栏的下拉菜单仍能显示在遮罩之上）
    win.level = NSStatusWindowLevel + 1;
    win.collectionBehavior |= NSWindowCollectionBehaviorIgnoresCycle;
    const bool takesFocus = !w->testAttribute(Qt::WA_ShowWithoutActivating)
                            && !(w->windowFlags() & Qt::WindowDoesNotAcceptFocus);
    if (takesFocus) {
        // 快捷键触发时前台是别的程序，不切过来遮罩收不到键盘输入
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
        [win makeKeyAndOrderFront:nil];
    }
}

class FloatingFilter : public QObject {
public:
    FloatingFilter(QWidget* w, FloatingLayer layer)
        : QObject(w)
        , m_layer(layer)
    {
    }

    bool eventFilter(QObject* obj, QEvent* e) override
    {
        if (e->type() == QEvent::Show) {
            auto* w = static_cast<QWidget*>(obj);
            applyLayer(w, m_layer);
            // Qt 在原生窗口真正显示时可能重设层级，事件循环回来后再设一次
            QTimer::singleShot(0, w, [w, layer = m_layer] { applyLayer(w, layer); });
        }
        return false;
    }

private:
    FloatingLayer m_layer;
};

// 系统毛玻璃（behind-window 混合，透出模糊后的桌面）插到 Qt 内容视图下面，铺满整个窗口（含标题栏）。
// 不用 NSGlassEffectView：它只折射同一窗口里在它下面的内容，当窗口背景时是不透明的白色；
// 也不能把它设成 contentView 再把 Qt 的视图放进去，Qt 显示窗口时会重设内容视图
class GlassFilter : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* obj, QEvent* e) override
    {
        if (e->type() == QEvent::Show && !m_installed) {
            m_installed = true;
            auto* w = static_cast<QWidget*>(obj);
            QTimer::singleShot(0, w, [w] { install(w); }); // 等原生窗口建好
        }
        return false;
    }

private:
    static void install(QWidget* w)
    {
        auto* content = (__bridge NSView*)reinterpret_cast<void*>(w->winId());
        NSView* frame = content.superview;
        if (!frame)
            return;
        auto* effect = [[NSVisualEffectView alloc] initWithFrame:frame.bounds];
        effect.material = NSVisualEffectMaterialPopover;
        effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        effect.state = NSVisualEffectStateActive; // 窗口失去焦点时也保持模糊
        effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [frame addSubview:effect positioned:NSWindowBelow relativeTo:content];
    }

    bool m_installed = false;
};

} // namespace

bool setupGlassBackground(QWidget* window)
{
    window->setAttribute(Qt::WA_TranslucentBackground);
    // 内容延伸到透明标题栏下面，整扇窗是一块玻璃；控件仍按安全区域避开标题栏
    window->setWindowFlag(Qt::ExpandedClientAreaHint);
    window->installEventFilter(new GlassFilter(window));
    return true;
}

Platform createPlatform()
{
    // 首次运行时请求屏幕录制权限（系统只弹一次，之后需要到“系统设置 > 隐私与安全性”里打开）
    if (!CGPreflightScreenCaptureAccess())
        CGRequestScreenCaptureAccess();

    Platform p;
    p.capturer = std::make_unique<MacScreenCapturer>();
    p.windowFinder = std::make_unique<MacWindowFinder>();
    p.hotkeys = std::make_unique<MacHotkeyManager>();
    p.wheelThrottle = std::make_unique<MacWheelThrottle>();
    return p;
}

void setupFloatingWindow(QWidget* window, FloatingLayer layer)
{
    window->setAttribute(Qt::WA_MacAlwaysShowToolWindow); // Qt::Tool 窗口默认在程序失去焦点时隐藏
    window->installEventFilter(new FloatingFilter(window, layer));
}

bool macAccessibilityTrusted(bool prompt)
{
    if (AXIsProcessTrusted())
        return true;
    static bool prompted = false;
    if (prompt && !prompted) {
        prompted = true;
        NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
        AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
    }
    return false;
}

bool scrollCaptureSupported()
{
    return true;
}

// 开机自启：~/Library/LaunchAgents 下的 plist，登录时由 launchd 启动
bool autostartSupported()
{
    return true;
}

bool autostartEnabled()
{
    QFile f(agentPath());
    return f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()) == agentPlist();
}

bool setAutostartEnabled(bool enabled)
{
    const QString path = agentPath();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(agentPlist().toUtf8()) > 0;
}
