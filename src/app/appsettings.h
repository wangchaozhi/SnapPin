#pragma once

#include <QList>
#include <QMap>
#include <QString>

// 用户设置（持久化到 QSettings）。开机自启由平台层直接读写系统位置，不在这里保存
struct AppSettings {
    struct HotkeyDefault {
        QString action;
        QString keys;
    };
    static const QList<HotkeyDefault>& hotkeyDefaults();

    QMap<QString, QString> hotkeys; // 动作 id -> 按键（QKeySequence 文本），空表示不设
    QString captureBackend = QStringLiteral("auto"); // auto: DXGI 优先；gdi
    bool uiAutomation = true;
    bool historyEnabled = true;
    int historyMax = 50;
    bool autoUpdate = true; // 启动后及每天检查一次更新

    static AppSettings load();
    void save() const;
};
