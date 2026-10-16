#pragma once

#include <QList>
#include <QRectF>
#include <QString>

// 各平台 OCR 共用的文字整理

struct OcrFragment {
    QRectF box;   // 图像坐标，左上角为原点
    QString text;
};

// 把一段段识别结果按位置拼成文本：纵向大半重叠的归为同一行，行内从左到右，行间用 \n
QString assembleOcrText(QList<OcrFragment> fragments);

// 去掉中日文字之间多余的空格；英文单词间的空格保留
QString tidyCjkSpacing(const QString& text);

