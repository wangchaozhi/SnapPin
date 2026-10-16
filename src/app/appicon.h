#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QString>

// 应用图标（托盘、窗口）：2×2 色块，右上一块“贴”起来浮在原位之上。配色可在设置里换。
// 程序文件本身的图标（exe / .app / AppImage）用默认配色，由 resources/icons/*.svg 生成
struct IconPalette {
    QString id;
    QString name;
    QColor topLeft, bottomLeft, bottomRight, floating, dash;
};

const QList<IconPalette>& iconPalettes(); // 第一个是默认配色
QIcon makeAppIcon(const QString& paletteId);
