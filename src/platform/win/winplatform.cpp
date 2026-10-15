#include "winplatform.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace {
const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kRunValue = QStringLiteral("SnapPin");

QString launchCommand()
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"');
}
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
