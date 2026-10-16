#include "imageutil.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>

bool saveImageInteractive(const QImage& image, QWidget* parent)
{
    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("save/lastDir"),
                                           QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    const QString name = QStringLiteral("SnapPin_%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));

    const QString path = QFileDialog::getSaveFileName(parent, QObject::tr("保存图片"), QDir(lastDir).filePath(name),
                                                      QObject::tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;BMP 图片 (*.bmp)"));
    if (path.isEmpty())
        return false;
    settings.setValue(QStringLiteral("save/lastDir"), QFileInfo(path).absolutePath());

    if (!image.save(path)) {
        QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("无法写入文件：\n%1").arg(path));
        return false;
    }
    return true;
}

QString quickSaveDir()
{
    const QString fallback = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(QStringLiteral("SnapPin"));
    return QSettings().value(QStringLiteral("save/quickDir"), fallback).toString();
}

QString quickSaveImage(const QImage& image)
{
    const QDir dir(quickSaveDir());
    if (!QDir().mkpath(dir.path()))
        return {};
    // 同一秒内多次保存时加毫秒，避免覆盖
    const QString path = dir.filePath(QStringLiteral("SnapPin_%1.png")
                                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));
    return image.save(path) ? path : QString();
}

QImage renderTextImage(const QString& text, qreal dpr)
{
    constexpr int kMaxWidth = 600;
    constexpr int kPadding = 12;

    QFont font = QApplication::font();
    font.setPixelSize(15);
    const QFontMetrics fm(font);
    const QRect bounds = fm.boundingRect(QRect(0, 0, kMaxWidth, 100000), Qt::TextWordWrap | Qt::TextExpandTabs, text);

    const QSize logical = bounds.size() + QSize(kPadding * 2, kPadding * 2);
    QImage img((QSizeF(logical) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(QColor(255, 255, 238));

    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);
    p.setPen(QColor(30, 30, 30));
    p.drawText(QRect(QPoint(kPadding, kPadding), bounds.size()), Qt::TextWordWrap | Qt::TextExpandTabs, text);
    return img;
}
