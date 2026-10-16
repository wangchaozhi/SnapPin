#include "translate/systemengine.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPointer>

extern "C" {
typedef void (*SnapPinTranslateCallback)(void* ctx, const char* json, const char* error, int code);
int snappin_system_translate_supported(void);
void snappin_system_translate(const char* textsJson, const char* target, void* ctx, SnapPinTranslateCallback cb);
void snappin_system_prepare(const char* source, const char* target, void* ctx, SnapPinTranslateCallback cb);
}

namespace {

// Swift 端的回调可能在任意线程，这里把结果转回主线程
struct Pending {
    QPointer<SystemEngine> engine;
    std::function<void(const QStringList&, const QString&, int)> done;
};

void onReply(void* ctx, const char* json, const char* error, int code)
{
    auto* p = static_cast<Pending*>(ctx);
    QStringList texts;
    if (json) {
        for (const QJsonValue& v : QJsonDocument::fromJson(QByteArray(json)).array())
            texts << v.toString();
    }
    const QString message = error ? QString::fromUtf8(error) : QString();
    QMetaObject::invokeMethod(qApp, [p, texts, message, code] {
        if (p->engine)
            p->done(texts, message, code);
        delete p;
    }, Qt::QueuedConnection);
}

} // namespace

bool SystemEngine::isSupported()
{
    return snappin_system_translate_supported() != 0;
}

QString SystemEngine::name() const
{
    return tr("macOS 系统翻译");
}

void SystemEngine::translate(const QStringList& texts, const QString& target, Callback onDone)
{
    run(texts, target, onDone, true);
}

void SystemEngine::run(const QStringList& texts, const QString& target, Callback onDone, bool mayPrepare)
{
    auto* p = new Pending;
    p->engine = this;
    p->done = [this, texts, target, onDone, mayPrepare](const QStringList& out, const QString& error, int code) {
        if (code != 2) {
            onDone(out, error);
            return;
        }
        // 语言包还没下载：error 里是 "源语言 目标语言"
        const QStringList pair = error.split(QLatin1Char(' '));
        const QString source = pair.value(0);
        if (!mayPrepare) {
            onDone({}, tr("翻译语言包没有下载完成"));
            return;
        }
        const auto answer = QMessageBox::question(
            nullptr, tr("下载翻译语言包 - SnapPin"),
            tr("系统翻译需要先下载 %1 → %2 的语言包（由 Apple 提供，下载后完全在本机离线翻译）。\n\n"
               "现在下载吗？也可以在“系统设置 → 通用 → 语言与地区 → 翻译语言”里管理。")
                .arg(QLocale(source).nativeLanguageName(), QLocale(target).nativeLanguageName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            onDone({}, tr("没有下载翻译语言包"));
            return;
        }
        auto* prep = new Pending;
        prep->engine = this;
        prep->done = [this, texts, target, onDone](const QStringList&, const QString& prepError, int prepCode) {
            if (prepCode != 0)
                onDone({}, tr("下载翻译语言包失败：%1").arg(prepError));
            else
                run(texts, target, onDone, false);
        };
        snappin_system_prepare(source.toUtf8().constData(), target.toUtf8().constData(), prep, onReply);
    };
    const QByteArray json = QJsonDocument(QJsonArray::fromStringList(texts)).toJson(QJsonDocument::Compact);
    snappin_system_translate(json.constData(), target.toUtf8().constData(), p, onReply);
}
