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
    bool historyEnabled = true;
    int historyMax = 50;
    bool autoUpdate = true; // 启动后及每天检查一次更新
    bool captureCursor = false; // 截图里带上鼠标指针
    QString iconPalette = QStringLiteral("sea"); // 托盘 / 窗口图标配色，见 appicon.h

    // 保存：文件名格式里 $...$ 之间按日期时间格式替换（与 Snipaste 相同的写法）
    QString fileNamePattern = QStringLiteral("SnapPin_$yyyyMMdd_HHmmss$");
    QString saveFormat = QStringLiteral("png"); // png 或 jpg
    int jpgQuality = 90;
    QString quickSaveDir; // 空表示默认的“图片/SnapPin”

    // 翻译：system 是 macOS 系统翻译（本机离线），openai 是 OpenAI 兼容的在线接口。API Key 存在系统钥匙串里，不在这里
#ifdef Q_OS_MACOS
    QString translateEngine = QStringLiteral("system");
#else
    QString translateEngine = QStringLiteral("openai");
#endif
    QString openAiBaseUrl = QStringLiteral("https://api.deepseek.com");
    QString openAiModel = QStringLiteral("deepseek-chat");

    static AppSettings load();
    void save() const;
};
