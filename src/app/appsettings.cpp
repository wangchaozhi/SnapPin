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
    a.historyEnabled = s.value(QStringLiteral("history/enabled"), a.historyEnabled).toBool();
    a.historyMax = s.value(QStringLiteral("history/max"), a.historyMax).toInt();
    a.autoUpdate = s.value(QStringLiteral("update/auto"), a.autoUpdate).toBool();
    a.captureCursor = s.value(QStringLiteral("capture/cursor"), a.captureCursor).toBool();
    a.iconPalette = s.value(QStringLiteral("appearance/iconPalette"), a.iconPalette).toString();
    // 保存相关的键由 imageutil 直接读取
    a.fileNamePattern = s.value(QStringLiteral("save/pattern"), a.fileNamePattern).toString();
    a.saveFormat = s.value(QStringLiteral("save/format"), a.saveFormat).toString();
    a.jpgQuality = s.value(QStringLiteral("save/quality"), a.jpgQuality).toInt();
    a.quickSaveDir = s.value(QStringLiteral("save/quickDir")).toString();
    return a;
}

void AppSettings::save() const
{
    QSettings s;
    for (auto it = hotkeys.cbegin(); it != hotkeys.cend(); ++it)
        s.setValue(QStringLiteral("hotkeys/") + it.key(), it.value());
    s.setValue(QStringLiteral("capture/backend"), captureBackend);
    s.setValue(QStringLiteral("history/enabled"), historyEnabled);
    s.setValue(QStringLiteral("history/max"), historyMax);
    s.setValue(QStringLiteral("update/auto"), autoUpdate);
    s.setValue(QStringLiteral("capture/cursor"), captureCursor);
    s.setValue(QStringLiteral("appearance/iconPalette"), iconPalette);
    s.setValue(QStringLiteral("save/pattern"), fileNamePattern);
    s.setValue(QStringLiteral("save/format"), saveFormat);
    s.setValue(QStringLiteral("save/quality"), jpgQuality);
    if (quickSaveDir.isEmpty())
        s.remove(QStringLiteral("save/quickDir"));
    else
        s.setValue(QStringLiteral("save/quickDir"), quickSaveDir);
}
