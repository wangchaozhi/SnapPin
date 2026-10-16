#pragma once

#include "translationengine.h"

// macOS 系统翻译（Apple Translation 框架，本机离线）。语言包没下载时先询问，再弹出系统的下载确认
class SystemEngine : public TranslationEngine {
    Q_OBJECT
public:
    using TranslationEngine::TranslationEngine;

    static bool isSupported(); // macOS 26 及以后

    QString name() const override;
    QString remoteHost() const override { return QString(); }
    void translate(const QStringList& texts, const QString& target, Callback onDone) override;

private:
    void run(const QStringList& texts, const QString& target, Callback onDone, bool mayPrepare);
};
