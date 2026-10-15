#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVersionNumber>

class QNetworkReply;

struct ReleaseInfo {
    QVersionNumber version;
    QString tag;
    QString notes;
    QUrl pageUrl;
    QUrl assetUrl; // 当前平台的安装包，没有时为空
    QString assetName;
    qint64 assetSize = 0;
    QByteArray sha256; // GitHub 提供的资产摘要（十六进制），可能为空
};

// 自动更新：从 GitHub Releases 获取最新版本，下载安装包并替换当前目录后重启。
// 只有发布包（目录中有 snappin.portable 标记）才会自动安装，开发构建只打开发布页
class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);

    // interactive：用户手动检查，已是最新或出错时也要提示
    void check(bool interactive);

signals:
    void notify(const QString& title, const QString& text);

private:
    void handleRelease(QNetworkReply* reply, bool interactive);
    void prompt(const ReleaseInfo& info);
    void download(const ReleaseInfo& info);
    void install(const QString& zipPath);
    static bool canSelfInstall();

    QNetworkAccessManager m_net;
    bool m_busy = false;
};
