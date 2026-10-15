#include "updater.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <QTimer>

namespace {

constexpr int kReportDelayMs = 3000;
const char* const kPortableMarker = "snappin.portable";

QString assetSuffix()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("-windows-x64.zip");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("-macos-universal.zip");
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("-linux-x86_64.AppImage");
#else
    return QString(); // 没有对应的安装包，只打开发布页
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

// 安装脚本写的结果，重启后读取并提示
QString installLogPath()
{
    return QDir(updateDir()).filePath(QStringLiteral("update.log"));
}

// 替换程序需要所在目录可写（装在 Program Files、/usr 等目录时没有写权限）
bool isDirWritable(const QDir& dir)
{
    QFile probe(dir.filePath(QStringLiteral(".snappin-write-test")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

#ifdef Q_OS_MACOS
// 当前程序所在的 .app（…/SnapPin.app/Contents/MacOS/SnapPin），不在 .app 里时为空
QString bundlePath()
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName() != QLatin1String("MacOS") || !dir.cdUp() || dir.dirName() != QLatin1String("Contents") || !dir.cdUp())
        return QString();
    return dir.absolutePath().endsWith(QLatin1String(".app")) ? dir.absolutePath() : QString();
}

// 等待旧进程退出 → 解压 → 校验新包的签名与当前程序一致 → 替换 .app（失败回滚）→ 启动新版本；失败时启动原程序
const char* const kInstallScript = R"SH(#!/bin/sh
pid=$1; zip=$2; app=$3
dir=$(dirname "$zip")
staging="$dir/staging"
backup="${app%.app}.old-update.app"
exec >"$dir/update.log" 2>&1
fail() { echo "failed $(date): $*"; rm -rf "$staging" "$zip"; open "$app"; exit 1; }

i=0
while kill -0 "$pid" 2>/dev/null; do
    # 旧进程还在运行，不再启动（否则单实例会把启动转成一次截图）
    i=$((i + 1)); [ $i -gt 150 ] && { echo "failed $(date): old process did not exit"; exit 1; }
    sleep 0.2
done

rm -rf "$staging"
ditto -x -k "$zip" "$staging" || fail "cannot extract $zip"
new=$(find "$staging" -maxdepth 1 -name '*.app' | head -n 1)
[ -n "$new" ] || fail "no .app in $zip"
# 程序自己下载的文件不带隔离属性，保险起见仍清除，避免 Gatekeeper 拦截
xattr -dr com.apple.quarantine "$new" 2>/dev/null
# 新包必须满足当前程序的签名要求（同一证书、同一 bundle ID），系统权限才能延续，也防止被替换成别人签名的程序
req=$(codesign -d -r- "$app" 2>/dev/null | sed -n 's/^designated => //p')
[ -n "$req" ] || fail "cannot read designated requirement of $app"
codesign --verify --deep --strict -R="$req" "$new" || fail "signature of new version does not match"

rm -rf "$backup"
mv "$app" "$backup" || fail "cannot move $app"
if ! mv "$new" "$app"; then
    rm -rf "$app"
    mv "$backup" "$app"
    fail "cannot install new version, rolled back"
fi
rm -rf "$backup" "$staging" "$zip"
echo "updated $(date)"
open "$app"
)SH";
#elif defined(Q_OS_WIN)
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
    "updated $(Get-Date)" | Out-File $log -Encoding utf8
} catch {
    "failed $(Get-Date): $_" | Out-File $log -Encoding utf8
}
Start-Process -FilePath $Exe
)PS";
#else
// 当前运行的 AppImage 文件（由 AppImage 运行时设置），不是 AppImage 时为空
QString appImagePath()
{
    return qEnvironmentVariable("APPIMAGE");
}

// 等待旧进程退出 → 复制到 AppImage 同目录 → 改名覆盖（原子替换，文件名不变，快捷方式继续有效）→ 启动新版本；
// 失败时启动原程序
const char* const kInstallScript = R"SH(#!/bin/sh
pid=$1; new=$2; target=$3
dir=$(dirname "$new")
tmp="$(dirname "$target")/.$(basename "$target").update"
exec >"$dir/update.log" 2>&1
fail() { echo "failed $(date): $*"; rm -f "$tmp" "$new"; nohup "$target" >/dev/null 2>&1 & exit 1; }

i=0
while kill -0 "$pid" 2>/dev/null; do
    # 旧进程还在运行，不再启动（否则单实例会把启动转成一次截图）
    i=$((i + 1)); [ $i -gt 150 ] && { echo "failed $(date): old process did not exit"; exit 1; }
    sleep 0.2
done

[ "$(head -c 4 "$new" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || fail "$new is not an AppImage"
cp "$new" "$tmp" && chmod 755 "$tmp" || fail "cannot copy new version next to $target"
mv -f "$tmp" "$target" || fail "cannot replace $target"
rm -f "$new"
echo "updated $(date)"
nohup "$target" >/dev/null 2>&1 &
)SH";
#endif

} // namespace

Updater::Updater(QObject* parent)
    : QObject(parent)
{
    // 等事件循环和托盘就绪后再提示
    QTimer::singleShot(kReportDelayMs, this, &Updater::reportLastInstall);
}

void Updater::reportLastInstall()
{
    QFile log(installLogPath());
    if (!log.open(QIODevice::ReadOnly))
        return;
    QString text = QString::fromUtf8(log.readAll());
    log.close();
    log.remove();
    text.remove(QChar(0xFEFF)); // PowerShell 写的 UTF-8 带 BOM
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString last = lines.isEmpty() ? QString() : lines.last().trimmed();
    if (last.startsWith(QLatin1String("updated")))
        emit notify(tr("SnapPin 已更新"), tr("当前版本 %1").arg(QStringLiteral(APP_VERSION)));
    else if (last.startsWith(QLatin1String("failed")))
        emit notify(tr("更新失败"), tr("自动安装没有完成，请到发布页手动下载。\n%1").arg(last));
}

bool Updater::canSelfInstall()
{
#ifdef Q_OS_WIN
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.exists(QString::fromLatin1(kPortableMarker)) && isDirWritable(appDir);
#elif defined(Q_OS_MACOS)
    // 只有 CI 打的发布包（Contents/Resources 里有标记）才覆盖，开发构建不动
    const QString bundle = bundlePath();
    if (bundle.isEmpty() || !QFile::exists(bundle + QStringLiteral("/Contents/Resources/") + QString::fromLatin1(kPortableMarker)))
        return false;
    // 从“下载”直接打开时系统会把程序转移到只读位置，这里也会失败
    if (!isDirWritable(QFileInfo(bundle).dir()) || !QFileInfo(bundle).isWritable())
        return false;
    // ad-hoc 签名（没配证书的构建）的签名要求是 cdhash，任何新版本都通不过校验
    QProcess codesign;
    codesign.start(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("-d"), QStringLiteral("-r-"), bundle});
    if (!codesign.waitForFinished(5000) || codesign.exitCode() != 0)
        return false;
    return codesign.readAllStandardOutput().contains("certificate leaf");
#elif defined(Q_OS_LINUX)
    // 只替换 AppImage（发布包）；从源码编译或装进系统目录的不动
    const QString appImage = appImagePath();
    return !appImage.isEmpty() && QFileInfo(appImage).isFile() && isDirWritable(QFileInfo(appImage).dir());
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

void Updater::install(const QString& packagePath)
{
#ifdef Q_OS_WIN
    const QString script = QDir(updateDir()).filePath(QStringLiteral("install.ps1"));
#else
    const QString script = QDir(updateDir()).filePath(QStringLiteral("install.sh"));
#endif
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit notify(tr("更新失败"), tr("无法写入安装脚本"));
        return;
    }
#ifdef Q_OS_WIN
    f.write("\xEF\xBB\xBF"); // UTF-8 BOM：Windows PowerShell 5 才能正确识别路径中的中文
#endif
    f.write(kInstallScript);
    f.close();
    QFile::remove(installLogPath());

    QProcess proc;
    const QString pid = QString::number(QCoreApplication::applicationPid());
#if defined(Q_OS_WIN)
    proc.setProgram(QStringLiteral("powershell.exe"));
    proc.setArguments({
        QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"), QDir::toNativeSeparators(script),
        QStringLiteral("-ProcessId"), pid,
        QStringLiteral("-Zip"), QDir::toNativeSeparators(packagePath),
        QStringLiteral("-Target"), QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
        QStringLiteral("-Exe"), QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
    });
#elif defined(Q_OS_MACOS)
    proc.setProgram(QStringLiteral("/bin/sh"));
    proc.setArguments({script, pid, packagePath, bundlePath()});
#else
    proc.setProgram(QStringLiteral("/bin/sh"));
    proc.setArguments({script, pid, packagePath, appImagePath()});
    // 去掉 AppImage 运行时为当前进程设置的环境（指向即将卸载的挂载目录），新版本启动时由它自己重新设置
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = env.value(QStringLiteral("APPDIR"));
    for (const QString& key : env.keys()) {
        if (key == QLatin1String("APPIMAGE") || key == QLatin1String("APPDIR") || key == QLatin1String("ARGV0")
            || key == QLatin1String("OWD") || (!appDir.isEmpty() && env.value(key).contains(appDir)))
            env.remove(key);
    }
    proc.setProcessEnvironment(env);
#endif
    if (!proc.startDetached()) {
        emit notify(tr("更新失败"), tr("无法启动安装程序"));
        return;
    }
    QApplication::quit();
}
