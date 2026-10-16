#pragma once

#include <QColor>
#include <QIcon>

// 截图工具栏的图标：统一的线性风格，每个工具一种马卡龙色，代码绘制的矢量图，任意缩放比例下都清晰
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
    Ocr,
    Pin,
    Save,
    Copy,
};

// 系统是否是深色主题。工具栏和图标都跟着它：浅色主题是白底 + 马卡龙浅色填充、同色深描边，
// 深色主题是深色底 + 马卡龙浅色线条。图标每次绘制时读取，主题切换后重绘即可
bool toolbarDarkMode();

QIcon toolIcon(ToolIcon id);
// 颜色按钮：圆形色块
QIcon colorIcon(const QColor& color);
