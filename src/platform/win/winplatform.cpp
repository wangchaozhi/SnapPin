#include "winplatform.h"

#include <QCoreApplication>
#include <QDir>
#include <QOperatingSystemVersion>
#include <QSettings>
#include <QWidget>

#include <dwmapi.h>

namespace {
const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kRunValue = QStringLiteral("SnapPin");

QString launchCommand()
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"');
}

// 旧版 SDK 里没有的常量
constexpr DWORD kDwmSystemBackdropType = 38; // DWMWA_SYSTEMBACKDROP_TYPE
constexpr int kBackdropAcrylic = 3;          // DWMSBT_TRANSIENTWINDOW

// 原生窗口建好后打开系统的亚克力背景：DWM 把它画在整个窗口（含标题栏）后面，Qt 的背景是透明的
class BackdropFilter : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* obj, QEvent* e) override
    {
        if (e->type() == QEvent::Show && !m_installed) {
            m_installed = true;
            const HWND hwnd = HWND(static_cast<QWidget*>(obj)->winId());
            const MARGINS margins{-1, -1, -1, -1};
            DwmExtendFrameIntoClientArea(hwnd, &margins);
            const int backdrop = kBackdropAcrylic;
            DwmSetWindowAttribute(hwnd, kDwmSystemBackdropType, &backdrop, sizeof(backdrop));
        }
        return false;
    }

private:
    bool m_installed = false;
};
} // namespace

Platform createPlatform()
{
    Platform p;
    p.capturer = std::make_unique<WinScreenCapturer>();
    p.windowFinder = std::make_unique<WinWindowFinder>();
    p.hotkeys = std::make_unique<WinHotkeyManager>();
    p.wheelThrottle = std::make_unique<WinWheelThrottle>();
    return p;
}

QPixmap WinScreenCapturer::grab(QScreen* screen)
{
    if (m_backend != QLatin1String("gdi")) {
        QPixmap pm = m_dxgi.grab(screen);
        if (!pm.isNull())
            return pm;
    }
    return m_gdi.grab(screen);
}

void setupFloatingWindow(QWidget*, FloatingLayer)
{
    // WindowStaysOnTopHint 在 Windows 上已经够用
}

bool setupGlassBackground(QWidget* window)
{
    // 系统背景材质从 Windows 11 22H2 开始才有，更早的系统保持原样
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621))
        return false;
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->installEventFilter(new BackdropFilter(window));
    return true;
}

TitleBarMode setupMergedTitleBar(QWidget* window)
{
    // Qt 6.9 起在客户区里画标题、窗口按钮并处理拖动；毛玻璃下标题栏不画底色，透出亚克力
    window->setWindowFlag(Qt::ExpandedClientAreaHint);
    if (window->testAttribute(Qt::WA_TranslucentBackground))
        window->setWindowFlag(Qt::NoTitleBarBackgroundHint);
    return TitleBarMode::SystemDrawn;
}

QMargins titleBarInsets(QWidget*)
{
    return {};
}

bool scrollCaptureSupported()
{
    return true;
}

bool autostartSupported()
{
    return true;
}

bool autostartEnabled()
{
    const QSettings run(kRunKey, QSettings::NativeFormat);
    return run.value(kRunValue).toString().compare(launchCommand(), Qt::CaseInsensitive) == 0;
}

bool setAutostartEnabled(bool enabled)
{
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue(kRunValue, launchCommand());
    else
        run.remove(kRunValue);
    run.sync();
    return run.status() == QSettings::NoError;
}
