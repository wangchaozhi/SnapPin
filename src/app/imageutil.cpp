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

QString expandFileNamePattern(const QString& pattern, const QDateTime& when)
{
    // $...$ 之间按日期时间格式替换，其余原样保留；去掉文件名里不允许的字符
    QString out;
    const QStringList parts = pattern.split(QLatin1Char('$'));
    for (int i = 0; i < parts.size(); ++i)
        out += (i % 2 == 1) ? when.toString(parts[i]) : parts[i];
    static const QString kInvalid = QStringLiteral("\\/:*?\"<>|");
    for (QChar& c : out) {
        if (kInvalid.contains(c))
            c = QLatin1Char('_');
    }
    return out.trimmed().isEmpty() ? QStringLiteral("SnapPin") : out.trimmed();
}

namespace {

QString saveFormat()
{
    return QSettings().value(QStringLiteral("save/format"), QStringLiteral("png")).toString() == QLatin1String("jpg")
               ? QStringLiteral("jpg")
               : QStringLiteral("png");
}

QString defaultBaseName()
{
    const QString pattern = QSettings().value(QStringLiteral("save/pattern"), QStringLiteral("SnapPin_$yyyyMMdd_HHmmss$")).toString();
    return expandFileNamePattern(pattern, QDateTime::currentDateTime());
}

// JPG 用设置里的质量；JPG 没有透明通道，先铺白底
bool writeImage(const QImage& image, const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.setDevicePixelRatio(image.devicePixelRatio()); // 否则高分屏截图会按逻辑尺寸缩小画上去
        flat.fill(Qt::white);
        QPainter(&flat).drawImage(0, 0, image);
        return flat.save(path, "JPG", qBound(1, QSettings().value(QStringLiteral("save/quality"), 90).toInt(), 100));
    }
    return image.save(path);
}

} // namespace

bool saveImageInteractive(const QImage& image, QWidget* parent)
{
    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("save/lastDir"),
                                           QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    const QString format = saveFormat();
    const QString name = defaultBaseName() + QLatin1Char('.') + format;
    const QString png = QObject::tr("PNG 图片 (*.png)");
    const QString jpg = QObject::tr("JPEG 图片 (*.jpg *.jpeg)");
    const QString bmp = QObject::tr("BMP 图片 (*.bmp)");
    QString selected = format == QLatin1String("jpg") ? jpg : png;
    const QString path = QFileDialog::getSaveFileName(parent, QObject::tr("保存图片"), QDir(lastDir).filePath(name),
                                                      QStringList{png, jpg, bmp}.join(QStringLiteral(";;")), &selected);
    if (path.isEmpty())
        return false;
    settings.setValue(QStringLiteral("save/lastDir"), QFileInfo(path).absolutePath());

    if (!writeImage(image, path)) {
        QMessageBox::warning(parent, QObject::tr("保存失败"), QObject::tr("无法写入文件：\n%1").arg(path));
        return false;
    }
    return true;
}

QString quickSaveDir()
{
    const QString fallback = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(QStringLiteral("SnapPin"));
    const QString dir = QSettings().value(QStringLiteral("save/quickDir")).toString();
    return dir.isEmpty() ? fallback : dir;
}

QString quickSaveImage(const QImage& image)
{
    const QDir dir(quickSaveDir());
    if (!QDir().mkpath(dir.path()))
        return {};
    // 文件名格式精确到秒时，同一秒内多次保存加序号，避免覆盖
    const QString base = defaultBaseName();
    const QString suffix = QLatin1Char('.') + saveFormat();
    QString path = dir.filePath(base + suffix);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = dir.filePath(QStringLiteral("%1_%2%3").arg(base).arg(n).arg(suffix));
    return writeImage(image, path) ? path : QString();
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
