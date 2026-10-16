#pragma once

#include "ocr/ocrtext.h"

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

// 翻译用到的文字处理，与界面和引擎无关（单元测试覆盖）

struct TranslateLanguage {
    QString code;    // BCP 47：zh-Hans、en…；"auto" 表示中英互译
    QString name;    // 界面上显示的名字（用该语言自己的写法）
    QString english; // 写进大模型提示词的英文名
};
const QList<TranslateLanguage>& translateLanguages();
QString englishLanguageName(const QString& code);

// "auto"：原文以中文为主时译成英文，否则译成简体中文；其他目标原样返回
QString resolveTargetLanguage(const QString& target, const QString& text);

// 截图上的一段文字：位置相邻、行高相近的几行合成一段，一起翻译、一起排版
struct TextBlock {
    QRectF box;            // 整段的外框（图像坐标）
    QList<QRectF> lines;   // 每个文字片段的框，覆盖译文前先擦掉
    qreal lineHeight = 0;  // 原文的平均行高，决定译文字号
    QString text;          // 合并后的原文（不再按行断开）
};
QList<TextBlock> groupTextBlocks(const QList<OcrFragment>& fragments);

// 把多行原文接成一段：中日文直接相连，其他语言用空格，行尾的连字符去掉
QString joinLines(const QStringList& lines);

// 解析大模型返回的 JSON 字符串数组（容忍外面包了 ``` 代码块或多余的说明文字）；数量不对时返回空
QStringList parseTranslationArray(const QString& reply, int expected);
