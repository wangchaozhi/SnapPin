#include "translator.h"

#include "app/appsettings.h"
#include "openaiengine.h"
#include "translatetext.h"
#ifdef Q_OS_MACOS
#include "systemengine.h"
#endif

#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QSettings>

namespace {
const QString kTargetKey = QStringLiteral("translate/target");
const QString kConsentKey = QStringLiteral("translate/consentedHosts");
} // namespace

Translator::Translator(QObject* parent)
    : QObject(parent)
{
}

void Translator::configure(const AppSettings& s)
{
    delete m_engine;
#ifdef Q_OS_MACOS
    if (s.translateEngine == QLatin1String("system") && SystemEngine::isSupported()) {
        m_engine = new SystemEngine(this);
        return;
    }
#endif
    m_engine = new OpenAiEngine(s.openAiBaseUrl, s.openAiModel, this);
}

QString Translator::engineName() const
{
    return m_engine ? m_engine->name() : QString();
}

QString Translator::remoteHost() const
{
    return m_engine ? m_engine->remoteHost() : QString();
}

QString Translator::targetLanguage()
{
    return QSettings().value(kTargetKey, QStringLiteral("auto")).toString();
}

void Translator::setTargetLanguage(const QString& code)
{
    QSettings().setValue(kTargetKey, code);
}

bool Translator::hostConsented(const QString& host)
{
    return QSettings().value(kConsentKey).toStringList().contains(host);
}

void Translator::setHostConsented(const QString& host, bool consented)
{
    QSettings s;
    QStringList hosts = s.value(kConsentKey).toStringList();
    hosts.removeAll(host);
    if (consented)
        hosts << host;
    s.setValue(kConsentKey, hosts);
}

bool Translator::askConsent(QWidget* parent)
{
    const QString host = m_engine->remoteHost();
    if (host.isEmpty() || hostConsented(host))
        return true;
    QMessageBox box(QMessageBox::Information, tr("在线翻译 - SnapPin"),
                    tr("翻译时，从截图里识别出的文字会发送到 %1（%2）。").arg(host, m_engine->name()),
                    QMessageBox::NoButton, parent);
    box.setInformativeText(tr("截图本身不会上传，只发送文字。文字可能包含你屏幕上的隐私信息，"
                              "该服务会怎样保存和使用这些内容，取决于它的隐私政策。\n\n"
                              "不想让文字离开本机，可以在“设置 → 翻译”里改用本机的翻译引擎。"));
    QPushButton* ok = box.addButton(tr("同意并翻译"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(ok);
    box.setWindowFlag(Qt::WindowStaysOnTopHint);
    box.exec();
    if (box.clickedButton() != ok)
        return false;
    setHostConsented(host, true);
    return true;
}

void Translator::translate(const QStringList& texts, const QString& target, QWidget* dialogParent, QObject* context,
                           Callback onDone)
{
    TranslateResult result;
    if (!m_engine) {
        result.error = tr("没有可用的翻译引擎");
        onDone(result);
        return;
    }
    result.engine = m_engine->name();
    result.host = m_engine->remoteHost();
    result.error = m_engine->setupError();
    if (!result.error.isEmpty()) {
        onDone(result);
        return;
    }
    if (!askConsent(dialogParent)) {
        result.error = tr("已取消：没有同意把文字发送到 %1").arg(result.host);
        onDone(result);
        return;
    }
    const QString lang = resolveTargetLanguage(target, texts.join(QLatin1Char('\n')));
    QPointer<QObject> guard(context);
    m_engine->translate(texts, lang, [guard, onDone, result](const QStringList& out, const QString& error) mutable {
        if (!guard)
            return;
        result.texts = out;
        result.error = error;
        onDone(result);
    });
}
