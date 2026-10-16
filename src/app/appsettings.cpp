#include "appsettings.h"

#include <QSettings>

const QList<AppSettings::HotkeyDefault>& AppSettings::hotkeyDefaults()
{
    static const QList<HotkeyDefault> defaults = {
        {QStringLiteral("capture"), QStringLiteral("F1")},
        {QStringLiteral("pin_clipboard"), QStringLiteral("F3")},
        {QStringLiteral("toggle_pins"), QStringLiteral("Shift+F3")},
        {QStringLiteral("history"), QString()},
        {QStringLiteral("restore_pin"), QString()},
    };
    return defaults;
}

AppSettings AppSettings::load()
{
    QSettings s;
    AppSettings a;
    for (const HotkeyDefault& d : hotkeyDefaults())
        a.hotkeys[d.action] = s.value(QStringLiteral("hotkeys/") + d.action, d.keys).toString();
    a.captureBackend = s.value(QStringLiteral("capture/backend"), a.captureBackend).toString();
    a.uiAutomation = s.value(QStringLiteral("capture/uiAutomation"), a.uiAutomation).toBool();
    a.historyEnabled = s.value(QStringLiteral("history/enabled"), a.historyEnabled).toBool();
    a.historyMax = s.value(QStringLiteral("history/max"), a.historyMax).toInt();
    a.autoUpdate = s.value(QStringLiteral("update/auto"), a.autoUpdate).toBool();
    return a;
}

void AppSettings::save() const
{
    QSettings s;
    for (auto it = hotkeys.cbegin(); it != hotkeys.cend(); ++it)
        s.setValue(QStringLiteral("hotkeys/") + it.key(), it.value());
    s.setValue(QStringLiteral("capture/backend"), captureBackend);
    s.setValue(QStringLiteral("capture/uiAutomation"), uiAutomation);
    s.setValue(QStringLiteral("history/enabled"), historyEnabled);
    s.setValue(QStringLiteral("history/max"), historyMax);
    s.setValue(QStringLiteral("update/auto"), autoUpdate);
}
