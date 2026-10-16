#pragma once

#include <QColor>
#include <QIcon>

// 截图工具栏的图标：统一的线性风格，代码绘制的矢量图，任意缩放比例下都清晰
enum class ToolIcon {
    Rect,
    Ellipse,
    Arrow,
    Pen,
    Marker,
    Mosaic,
    Text,
    Undo,
    Redo,
    LongCapture,
    Pin,
    Save,
    Cancel,
    Done,
};

// accent：始终用主题色绘制（主操作按钮）
QIcon toolIcon(ToolIcon id, bool accent = false);
// 线宽按钮：一条对应粗细的横线；level 为 0、1、2（细、中、粗）
QIcon widthIcon(int level);
// 颜色按钮：圆形色块
QIcon colorIcon(const QColor& color);
