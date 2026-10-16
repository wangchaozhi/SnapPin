#pragma once

#include <QDateTime>
#include <QImage>

class QWidget;

// 弹出保存对话框并保存图片；用户取消或保存失败返回 false
bool saveImageInteractive(const QImage& image, QWidget* parent = nullptr);

// 不弹对话框，按时间命名存到快速保存目录（默认“图片/SnapPin”）；失败返回空字符串
QString quickSaveImage(const QImage& image);
QString quickSaveDir();
// 文件名格式：$...$ 之间按日期时间格式替换（如 "SnapPin_$yyyyMMdd_HHmmss$"）
QString expandFileNamePattern(const QString& pattern, const QDateTime& when);

// 把纯文本渲染成图片（贴文字用），按指定缩放比生成清晰图像
QImage renderTextImage(const QString& text, qreal devicePixelRatio);
