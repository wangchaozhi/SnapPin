#include "historystore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>

HistoryStore::HistoryStore(QObject* parent)
    : QObject(parent)
    , m_dir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/history"))
{
    QDir().mkpath(m_dir);
}

void HistoryStore::add(const QImage& image)
{
    if (!m_enabled || image.isNull())
        return;
    const QString path = QDir(m_dir).filePath(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")) + QStringLiteral(".png"));

    // 4K PNG 编码要上百毫秒，不阻塞界面
    QPointer<HistoryStore> self(this);
    QThreadPool::globalInstance()->start([self, image, path] {
        image.save(path, "PNG");
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self] {
            if (self) {
                self->prune();
                emit self->changed();
            }
        }, Qt::QueuedConnection);
    });
}

QFileInfoList HistoryStore::entries() const
{
    return QDir(m_dir).entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name | QDir::Reversed);
}

void HistoryStore::remove(const QString& path)
{
    if (QFileInfo(path).absolutePath() == QFileInfo(m_dir).absoluteFilePath()) // 只删历史目录里的文件
        QFile::remove(path);
    emit changed();
}

void HistoryStore::clear()
{
    for (const QFileInfo& fi : entries())
        QFile::remove(fi.absoluteFilePath());
    emit changed();
}

void HistoryStore::setMaxCount(int count)
{
    m_maxCount = qMax(1, count);
    prune();
}

void HistoryStore::prune()
{
    const QFileInfoList all = entries();
    for (qsizetype i = m_maxCount; i < all.size(); ++i)
        QFile::remove(all[i].absoluteFilePath());
}
