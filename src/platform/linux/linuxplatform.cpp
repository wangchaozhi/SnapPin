#include "linuxplatform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QWidget>

namespace {

// X11 下 Qt 自己的实现就是读根窗口（xcb_get_image），已经够用
class X11ScreenCapturer : public IScreenCapturer {
public:
    QPixmap grab(QScreen* screen) override { return screen->grabWindow(0); }
};

class NullWindowFinder : public IWindowFinder {
public:
    QList<DetectedWindow> windowsOnScreen(QScreen*) override { return {}; }
};

bool isWayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

xcb_connection_t* x11Connection()
{
    auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/SnapPin.desktop");
}

QString autostartEntry()
{
    // AppImage 运行时可执行文件在临时挂载点里，要记录 AppImage 本身的路径
    QString exe = qEnvironmentVariable("APPIMAGE");
    if (exe.isEmpty())
        exe = QCoreApplication::applicationFilePath();
    exe.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QStringLiteral(
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=SnapPin\n"
               "Exec=\"%1\"\n"
               "Terminal=false\n"
               "X-GNOME-Autostart-enabled=true\n")
        .arg(exe);
}

} // namespace

QRect nativeScreenRect(QScreen* screen)
{
    const QRect g = screen->geometry();
    return QRect(g.topLeft(), (QSizeF(g.size()) * screen->devicePixelRatio()).toSize());
}

QString WaylandHotkeyManager::unavailableReason() const
{
    return tr("Wayland 桌面不允许程序注册全局快捷键。\n"
              "请在系统设置的键盘快捷键里添加自定义快捷键，命令填写：\n%1 capture")
        .arg(qEnvironmentVariable("APPIMAGE", QCoreApplication::applicationFilePath()));
}

Platform createPlatform()
{
    Platform p;
    if (xcb_connection_t* conn = isWayland() ? nullptr : x11Connection()) {
        p.capturer = std::make_unique<X11ScreenCapturer>();
        p.windowFinder = std::make_unique<X11WindowFinder>(conn);
        p.hotkeys = std::make_unique<X11HotkeyManager>(conn);
    } else {
        p.capturer = std::make_unique<PortalScreenCapturer>();
        p.windowFinder = std::make_unique<NullWindowFinder>(); // Wayland 不允许读取其他窗口的位置
        p.hotkeys = std::make_unique<WaylandHotkeyManager>();
    }
    // 滚动接管需要全局吞掉滚轮事件，X11 只能抢占整个指针，副作用太大，暂不接管
    p.wheelThrottle = std::make_unique<IWheelThrottle>();
    return p;
}

void setupFloatingWindow(QWidget* window, FloatingLayer layer)
{
    if (layer != FloatingLayer::Overlay)
        return; // 贴图：WindowStaysOnTopHint 足够
    const bool takesFocus = !window->testAttribute(Qt::WA_ShowWithoutActivating)
                            && !(window->windowFlags() & Qt::WindowDoesNotAcceptFocus);
    if (takesFocus) {
        // 截图遮罩：全屏状态的窗口在 EWMH 窗口管理器和 Wayland 合成器里都会盖住面板
        window->setWindowState(window->windowState() | Qt::WindowFullScreen);
    } else if (!isWayland()) {
        // 长截图边框和面板不需要焦点，绕过窗口管理器，可以精确定位并盖住面板
        window->setWindowFlag(Qt::X11BypassWindowManagerHint);
    }
}

bool scrollCaptureSupported()
{
    return !isWayland(); // portal 截一次屏要几十毫秒（GNOME 还会存文件、闪屏），窗口也无法自己定位
}

// 开机自启：XDG autostart 规范，GNOME、KDE、Xfce 等都支持
bool autostartSupported()
{
    return true;
}

bool autostartEnabled()
{
    QFile f(autostartPath());
    return f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()) == autostartEntry();
}

bool setAutostartEnabled(bool enabled)
{
    const QString path = autostartPath();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(autostartEntry().toUtf8()) > 0;
}
