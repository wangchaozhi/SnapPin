#include "openaiengine.h"

#include "secretstore.h"
#include "translatetext.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

namespace {
constexpr int kTimeoutMs = 60 * 1000;

bool isLocalHost(const QString& host)
{
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1")
           || host.endsWith(QLatin1String(".localhost"));
}
} // namespace

OpenAiEngine::OpenAiEngine(const QString& baseUrl, const QString& model, QObject* parent)
    : TranslationEngine(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_baseUrl(baseUrl.trimmed().isEmpty() ? defaultBaseUrl() : baseUrl.trimmed())
    , m_model(model.trimmed().isEmpty() ? defaultModel() : model.trimmed())
{
}

QString OpenAiEngine::name() const
{
    return tr("大模型 %1").arg(m_model);
}

QString OpenAiEngine::remoteHost() const
{
    const QString host = QUrl(m_baseUrl).host();
    return isLocalHost(host) ? QString() : host;
}

QString OpenAiEngine::setupError() const
{
    // 本机服务（Ollama）可以不要 Key
    if (!remoteHost().isEmpty() && readSecret(apiKeySecret()).trimmed().isEmpty())
        return tr("还没有填写翻译服务的 API Key，请在“设置 → 翻译”里填写");
    return QString();
}

void OpenAiEngine::translate(const QStringList& texts, const QString& target, Callback onDone)
{
    const QString apiKey = readSecret(apiKeySecret()).trimmed();
    // 地址可以填到 /v1 为止，也可以填完整的 /chat/completions
    QString endpoint = m_baseUrl;
    while (endpoint.endsWith(QLatin1Char('/')))
        endpoint.chop(1);
    if (!endpoint.endsWith(QLatin1String("/chat/completions")))
        endpoint += QLatin1String("/chat/completions");
    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    request.setTransferTimeout(kTimeoutMs);

    const QString system = QStringLiteral(
        "You are a translation engine. The user message is a JSON array of strings recognized from a screenshot. "
        "Translate each string into %1. Reply with only a JSON array of the translated strings, with the same "
        "number of elements in the same order. Keep line breaks, numbers, URLs, code and product names as they "
        "are. If a string is already in %1, return it unchanged. Do not add any explanation.")
                               .arg(englishLanguageName(target));
    QJsonObject body{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("stream"), false},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
                    QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"),
                                 QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(texts))
                                                       .toJson(QJsonDocument::Compact))}}}},
    };
    QNetworkReply* reply = m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const int count = int(texts.size());
    connect(reply, &QNetworkReply::finished, this, [reply, count, onDone] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonObject json = QJsonDocument::fromJson(data).object();
        if (reply->error() != QNetworkReply::NoError) {
            // 服务返回的错误说明比 Qt 的网络错误更有用（Key 不对、余额不足…）
            QString message = json.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            if (message.isEmpty())
                message = json.value(QStringLiteral("error")).toString();
            if (message.isEmpty())
                message = reply->errorString();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            onDone({}, status ? QStringLiteral("HTTP %1：%2").arg(status).arg(message) : message);
            return;
        }
        const QString content = json.value(QStringLiteral("choices"))
                                    .toArray()
                                    .at(0)
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toObject()
                                    .value(QStringLiteral("content"))
                                    .toString();
        QStringList out = parseTranslationArray(content, count);
        if (out.isEmpty() && count == 1 && !content.trimmed().isEmpty())
            out << content.trimmed(); // 只有一段时，模型没按数组回复也能用
        if (out.isEmpty())
            onDone({}, tr("翻译服务返回的内容无法解析"));
        else
            onDone(out, QString());
    });
}
