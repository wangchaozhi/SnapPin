#pragma once

#include "translatetext.h"

#include <QImage>

// 把译文贴回截图原处：用段落周围的背景色盖住原文，再在同一个框里排译文，字号尽量接近原文、放不下时缩小。
// translations 与 blocks 一一对应；和原文相同的段落保持不动
QImage renderTranslatedImage(const QImage& image, const QList<TextBlock>& blocks, const QStringList& translations);
