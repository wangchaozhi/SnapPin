#include "updater.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

const char* const kPortableMarker = "snappin.portable";

QString assetSuffix()
{
#ifdef Q_OS_WIN
    return QStringLiteral("-windows-x64.zip");
#else
    return QString(); // 其他平台暂不支持自动安装，只打开发布页
#endif
}

QNetworkRequest apiRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SnapPin/" APP_VERSION));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(30000);
    return req;
}

QString updateDir()
{
    return QDir::temp().filePath(QStringLiteral("SnapPin-update"));
}

// 等待旧进程退出 → 解压 → 覆盖安装目录 → 启动新版本；失败时记录日志并启动原程序
const char* const kInstallScript = R"PS(param([int]$ProcessId, [string]$Zip, [string]$Target, [string]$Exe)
$log = Join-Path ([IO.Path]::GetDirectoryName($Zip)) 'update.log'
try {
    Wait-Process -Id $ProcessId -Timeout 30 -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    $staging = Join-Path ([IO.Path]::GetDirectoryName($Zip)) 'staging'
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
    Expand-Archive -Path $Zip -DestinationPath $staging -Force
    $items = @(Get-ChildItem $staging)
    $src = if ($items.Count -eq 1 -and $items[0].PSIsContainer) { $items[0].FullName } else { $staging }
    Copy-Item -Path (Join-Path $src '*') -Destination $Target -Recurse -Force -ErrorAction Stop
    Remove-Item $staging -Recurse -Force
    Remove-Item $Zip -Force
    "updated $(Get-Date)" | Out-File $log
} catch {
    "failed $(Get-Date): $_" | Out-File $log
}
Start-Process -FilePath $Exe
)PS";

} // namespace

Updater::Updater(QObject* parent)
    : QObject(parent)
{
}

bool Updater::canSelfInstall()
{
#ifdef Q_OS_WIN
    const QDir appDir(QCoreApplication::applicationDirPath());
    if (!appDir.exists(QString::fromLatin1(kPortableMarker)))
        return false;
    // 装在 Program Files 等目录时没有写权限
    QFile probe(appDir.filePath(QStringLiteral(".write-test")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
#else
    return false;
#endif
}

void Updater::check(bool interactive)
{
    if (m_busy)
        return;
    m_busy = true;
    const QUrl url(QStringLiteral("https://api.github.com/repos/" SNAPPIN_GITHUB_REPO "/releases/latest"));
    QNetworkReply* reply = m_net.get(apiRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        reply->deleteLater();
        m_busy = false;
        handleRelease(reply, interactive);
    });
}

void Updater::handleRelease(QNetworkReply* reply, bool interactive)
{
    if (reply->error() != QNetworkReply::NoError) {
        if (interactive)
            emit notify(tr("检查更新失败"), reply->errorString());
        return;
    }

    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    ReleaseInfo info;
    info.tag = obj.value(QStringLiteral("tag_name")).toString();
    info.version = QVersionNumber::fromString(info.tag.startsWith(QLatin1Char('v')) ? info.tag.mid(1) : info.tag);
    info.notes = obj.value(QStringLiteral("body")).toString();
    info.pageUrl = QUrl(obj.value(QStringLiteral("html_url")).toString());
    const QString suffix = assetSuffix();
    for (const QJsonValue& v : obj.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(QStringLiteral("name")).toString();
        if (suffix.isEmpty() || !name.endsWith(suffix))
            continue;
        info.assetName = name;
        info.assetUrl = QUrl(a.value(QStringLiteral("browser_download_url")).toString());
        info.assetSize = a.value(QStringLiteral("size")).toInteger();
        const QString digest = a.value(QStringLiteral("digest")).toString();
        if (digest.startsWith(QLatin1String("sha256:")))
            info.sha256 = digest.mid(7).toLatin1().toLower();
    }

    const QVersionNumber current = QVersionNumber::fromString(QStringLiteral(APP_VERSION));
    if (info.version.isNull() || info.version <= current) {
        if (interactive)
            emit notify(tr("检查更新"), tr("当前已是最新版本（%1）").arg(current.toString()));
        return;
    }
    if (!interactive && QSettings().value(QStringLiteral("update/skipVersion")).toString() == info.version.toString())
        return;
    prompt(info);
}

void Updater::prompt(const ReleaseInfo& info)
{
    QMessageBox box;
    box.setWindowTitle(tr("SnapPin 更新"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("发现新版本 %1（当前 %2）").arg(info.version.toString(), QStringLiteral(APP_VERSION)));
    if (!info.notes.isEmpty())
        box.setDetailedText(info.notes);
    const bool selfInstall = canSelfInstall() && info.assetUrl.isValid();
    box.setInformativeText(selfInstall ? tr("下载后将自动安装并重启 SnapPin。")
                                       : tr("将打开发布页面，请手动下载安装。"));
    QPushButton* update = box.addButton(selfInstall ? tr("立即更新") : tr("打开发布页"), QMessageBox::AcceptRole);
    QPushButton* skip = box.addButton(tr("跳过此版本"), QMessageBox::RejectRole);
    box.addButton(tr("稍后"), QMessageBox::RejectRole);
    box.setDefaultButton(update);
    box.setWindowFlag(Qt::WindowStaysOnTopHint);
    box.exec();

    if (box.clickedButton() == skip) {
        QSettings().setValue(QStringLiteral("update/skipVersion"), info.version.toString());
    } else if (box.clickedButton() == update) {
        if (selfInstall)
            download(info);
        else
            QDesktopServices::openUrl(info.pageUrl);
    }
}

void Updater::download(const ReleaseInfo& info)
{
    QDir().mkpath(updateDir());
    const QString path = QDir(updateDir()).filePath(info.assetName);
    auto* file = new QSaveFile(path, this);
    if (!file->open(QIODevice::WriteOnly)) {
        emit notify(tr("更新失败"), tr("无法写入临时文件：%1").arg(path));
        delete file;
        return;
    }

    m_busy = true;
    QNetworkRequest req = apiRequest(info.assetUrl);
    req.setRawHeader("Accept", "application/octet-stream");
    req.setTransferTimeout(0); // 大文件下载不设总超时
    QNetworkReply* reply = m_net.get(req);

    auto* progress = new QProgressDialog(tr("正在下载 %1 ...").arg(info.assetName), tr("取消"), 0, 100);
    progress->setWindowTitle(tr("SnapPin 更新"));
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

    auto* hash = new QCryptographicHash(QCryptographicHash::Sha256);
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, hash] {
        const QByteArray data = reply->readAll();
        hash->addData(data);
        file->write(data);
    });
    connect(reply, &QNetworkReply::downloadProgress, progress, [progress, info](qint64 got, qint64 total) {
        if (total <= 0)
            total = info.assetSize;
        if (total > 0)
            progress->setValue(int(got * 100 / total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, progress, info, path] {
        m_busy = false;
        reply->deleteLater();
        const QByteArray digest = hash->result().toHex();
        delete hash;
        progress->close();

        if (reply->error() != QNetworkReply::NoError) {
            file->cancelWriting();
            file->deleteLater();
            if (reply->error() != QNetworkReply::OperationCanceledError)
                emit notify(tr("更新失败"), reply->errorString());
            return;
        }
        if (!info.sha256.isEmpty() && digest != info.sha256) {
            file->cancelWriting();
            file->deleteLater();
            emit notify(tr("更新失败"), tr("安装包校验失败（SHA-256 不一致），已放弃安装"));
            return;
        }
        const bool ok = file->commit();
        file->deleteLater();
        if (!ok) {
            emit notify(tr("更新失败"), tr("无法保存安装包"));
            return;
        }
        install(path);
    });
}

void Updater::install(const QString& zipPath)
{
    const QString script = QDir(updateDir()).filePath(QStringLiteral("install.ps1"));
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit notify(tr("更新失败"), tr("无法写入安装脚本"));
        return;
    }
    f.write("\xEF\xBB\xBF"); // UTF-8 BOM：Windows PowerShell 5 才能正确识别路径中的中文
    f.write(kInstallScript);
    f.close();

    const QStringList args = {
        QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"), QDir::toNativeSeparators(script),
        QStringLiteral("-ProcessId"), QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("-Zip"), QDir::toNativeSeparators(zipPath),
        QStringLiteral("-Target"), QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
        QStringLiteral("-Exe"), QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
    };
    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), args)) {
        emit notify(tr("更新失败"), tr("无法启动安装程序"));
        return;
    }
    QApplication::quit();
}
