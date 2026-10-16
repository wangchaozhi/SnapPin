#pragma once

#include "translationengine.h"

#include <QObject>
#include <QPointer>

struct AppSettings;
class QWidget;

struct TranslateResult {
    QStringList texts; // 与请求一一对应
    QString error;     // 为空表示成功
    QString engine;    // 引擎名，界面上注明译文来源
    QString host;      // 文字发到了哪个服务器；本机翻译为空
};

// 按设置选择翻译引擎。在线引擎第一次往某个服务器发送文字前，先弹窗说明并征得同意
class Translator : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const TranslateResult&)>;

    explicit Translator(QObject* parent = nullptr);

    void configure(const AppSettings& settings); // 设置变化后重建引擎
    QString engineName() const;
    QString remoteHost() const;

    // 目标语言（"auto" 表示中英互译），在结果窗口里选过的会记住
    static QString targetLanguage();
    static void setTargetLanguage(const QString& code);

    // target 可以是 "auto"，按 texts 的内容决定；context 销毁后不再回调
    void translate(const QStringList& texts, const QString& target, QWidget* dialogParent, QObject* context,
                   Callback onDone);

    // 用户同意过往这个服务器发送文字
    static bool hostConsented(const QString& host);
    static void setHostConsented(const QString& host, bool consented);

private:
    bool askConsent(QWidget* parent);

    QPointer<TranslationEngine> m_engine;
};
