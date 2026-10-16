#pragma once

#include <QObject>
#include <QStringList>
#include <functional>

// 翻译引擎接口：一次翻译一组文字（一段一条），结果在主线程回调，顺序与输入一致
class TranslationEngine : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const QStringList& texts, const QString& error)>;

    using QObject::QObject;
    virtual QString name() const = 0;       // 界面上显示的名字
    virtual QString remoteHost() const = 0; // 文字会发到的服务器域名；本机翻译为空
    virtual QString setupError() const { return QString(); } // 缺少必要的设置（比如 API Key）时说明原因
    // target 是具体的语言代码（zh-Hans、en…），不会是 "auto"
    virtual void translate(const QStringList& texts, const QString& target, Callback onDone) = 0;
};
