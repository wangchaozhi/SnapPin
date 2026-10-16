#pragma once

#include <QColor>
#include <QIcon>

// 截图工具栏的图标：统一的线性风格，代码绘制的矢量图，任意缩放比例下都清晰
enum class ToolIcon {
    Rect,
    Ellipse,
    Line,
    Arrow,
    Pen,
    Marker,
    Mosaic,
    Eraser,
    Fill,
    Text,
    Number,
    Undo,
    Redo,
    LongCapture,
    Pin,
    Save,
    Copy,
};

// accent：始终用主题色绘制（主操作按钮）
QIcon toolIcon(ToolIcon id, bool accent = false);
// 线宽按钮：一条按比例显示粗细的横线
QIcon widthIcon(qreal width);
// 颜色按钮：圆形色块
QIcon colorIcon(const QColor& color);
