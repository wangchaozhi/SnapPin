#include "appicon.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPixmap>

namespace {

// 与 resources/icons/snappin.svg、snappin-small.svg 的布局一致（512×512 坐标）
constexpr int kSmallMax = 32; // 不大于这个尺寸用简化版：去掉虚线和阴影、色块加大

QPixmap render(const IconPalette& c, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 512.0, size / 512.0);
    p.setPen(Qt::NoPen);
    auto block = [&p](const QRectF& r, qreal radius, const QColor& color) {
        p.setBrush(color);
        p.drawRoundedRect(r, radius, radius);
    };
    if (size <= kSmallMax) {
        block({16, 112, 192, 192}, 28, c.topLeft);
        block({16, 320, 192, 192}, 28, c.bottomLeft);
        block({224, 320, 192, 192}, 28, c.bottomRight);
        block({304, 0, 208, 208}, 28, c.floating);
        return pm;
    }
    block({40, 96, 176, 176}, 20, c.topLeft);
    block({40, 296, 176, 176}, 20, c.bottomLeft);
    block({240, 296, 176, 176}, 20, c.bottomRight);
    QPen dash(c.dash, 10, Qt::CustomDashLine, Qt::FlatCap);
    dash.setDashPattern({2.6, 1.8}); // 以线宽为单位：26 实 18 空
    p.setPen(dash);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(240, 96, 176, 176), 20, 20);
    p.setPen(Qt::NoPen);
    block({314, 54, 176, 176}, 20, QColor(0x1E, 0x2A, 0x3A, 46)); // 阴影
    block({304, 40, 176, 176}, 20, c.floating);
    return pm;
}

} // namespace

const QList<IconPalette>& iconPalettes()
{
    auto tr = [](const char* s) { return QCoreApplication::translate("AppIcon", s); };
    // 左上、左下、右下、浮起的一块、虚线
    static const QList<IconPalette> palettes = {
        {QStringLiteral("sea"), tr("海盐蓝绿"), QColor(0x1F7A8C), QColor(0x2BB3A3), QColor(0x8FD3C3), QColor(0xFF7A59), QColor(0x9DB8B8)},
        {QStringLiteral("macaron"), tr("马卡龙"), QColor(0x5B8DEF), QColor(0x8A7BEA), QColor(0x3FAF5A), QColor(0xE06C6C), QColor(0xA8A3B8)},
        {QStringLiteral("pastel"), tr("马卡龙浅色"), QColor(0xA0C4FF), QColor(0xBDB2FF), QColor(0xB9FBC0), QColor(0xFFADAD), QColor(0xC9C6D6)},
        {QStringLiteral("vivid"), tr("鲜艳"), QColor(0x2F6BFF), QColor(0x7B4DFF), QColor(0x1FB04A), QColor(0xFF4D4D), QColor(0x9AA0B8)},
        {QStringLiteral("jewel"), tr("宝石"), QColor(0x1D4ED8), QColor(0x6D28D9), QColor(0x047857), QColor(0xBE123C), QColor(0x8B8FA3)},
        {QStringLiteral("sunset"), tr("日落橙紫"), QColor(0xFF8A3D), QColor(0xE0457B), QColor(0xFFC23D), QColor(0x6C4BD8), QColor(0xB9A9A0)},
        {QStringLiteral("forest"), tr("森林"), QColor(0x2F6F4F), QColor(0x7FA650), QColor(0xC9B458), QColor(0xE0703A), QColor(0xA3A894)},
        {QStringLiteral("grapefruit"), tr("葡萄柚"), QColor(0xF25F5C), QColor(0xFFE066), QColor(0x70C1B3), QColor(0x247BA0), QColor(0xB4B0A8)},
        {QStringLiteral("blue_orange"), tr("蓝调橙"), QColor(0x3A6EA5), QColor(0x5B8FD0), QColor(0x8EB8EB), QColor(0xFF6B35), QColor(0xA0AFC2)},
        {QStringLiteral("chinese"), tr("中国传统色"), QColor(0x1A3D6D), QColor(0xC8372D), QColor(0x3C8D6E), QColor(0xF2A900), QColor(0xA39C8E)},
        {QStringLiteral("neon"), tr("赛博霓虹"), QColor(0x3A0CA3), QColor(0x7209B7), QColor(0x4CC9F0), QColor(0xF72585), QColor(0x9C94B8)},
        {QStringLiteral("coffee"), tr("咖啡奶茶"), QColor(0x8B5E3C), QColor(0xC49A6C), QColor(0xE8D3B9), QColor(0xD9463E), QColor(0xB5A897)},
    };
    return palettes;
}

QIcon makeAppIcon(const QString& paletteId)
{
    const QList<IconPalette>& all = iconPalettes();
    const IconPalette* palette = &all.first();
    for (const IconPalette& c : all) {
        if (c.id == paletteId)
            palette = &c;
    }
    QIcon icon;
    for (int s : {16, 20, 24, 32, 40, 48, 64, 128, 256})
        icon.addPixmap(render(*palette, s));
    return icon;
}
