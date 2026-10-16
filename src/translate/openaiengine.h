#pragma once

#include "translationengine.h"

class QNetworkAccessManager;

// OpenAI 兼容的 Chat Completions 接口：DeepSeek、通义千问、Kimi、OpenAI，以及本机的 Ollama 等
class OpenAiEngine : public TranslationEngine {
    Q_OBJECT
public:
    // API Key 在翻译时才从系统钥匙串读取（Linux 的钥匙环锁着时会弹出解锁，不能在启动时读）
    OpenAiEngine(const QString& baseUrl, const QString& model, QObject* parent = nullptr);

    QString name() const override;
    QString remoteHost() const override;
    QString setupError() const override;
    void translate(const QStringList& texts, const QString& target, Callback onDone) override;

    static QString defaultBaseUrl() { return QStringLiteral("https://api.deepseek.com"); }
    static QString defaultModel() { return QStringLiteral("deepseek-chat"); }
    static QString apiKeySecret() { return QStringLiteral("openai_api_key"); } // 存在系统钥匙串里的名字

private:
    QNetworkAccessManager* m_net;
    QString m_baseUrl;
    QString m_model;
};
