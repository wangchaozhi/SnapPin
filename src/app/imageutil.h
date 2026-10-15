#pragma once

#include <QImage>

class QWidget;

// 弹出保存对话框并保存图片；用户取消或保存失败返回 false
bool saveImageInteractive(const QImage& image, QWidget* parent = nullptr);

// 把纯文本渲染成图片（贴文字用），按指定缩放比生成清晰图像
QImage renderTextImage(const QString& text, qreal devicePixelRatio);
