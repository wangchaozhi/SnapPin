#include "ocr/ocrtext.h"

#include <cstdio>

static int failures = 0;

static void expect(const char* name, const QString& got, const QString& want)
{
    if (got != want) {
        std::printf("FAIL %s:\n  got  \"%s\"\n  want \"%s\"\n", name, qPrintable(got), qPrintable(want));
        ++failures;
    } else {
        std::printf("ok   %s\n", name);
    }
}

int main()
{
    expect("cjk spaces removed", tidyCjkSpacing(QStringLiteral("你 好 ， 世界")), QStringLiteral("你好，世界"));
    expect("latin spaces kept", tidyCjkSpacing(QStringLiteral("Hello  world")), QStringLiteral("Hello  world"));
    expect("mixed boundary kept", tidyCjkSpacing(QStringLiteral("使用 Qt 6 开发")), QStringLiteral("使用 Qt 6 开发"));
    expect("korean kept", tidyCjkSpacing(QStringLiteral("안녕 하세요")), QStringLiteral("안녕 하세요"));

    // 同一行的两段（第二段稍微偏下）先左后右；第二行在下面
    const QList<OcrFragment> frags = {
        {QRectF(200, 12, 80, 20), QStringLiteral("world")},
        {QRectF(10, 60, 100, 20), QStringLiteral("第 二 行")},
        {QRectF(10, 10, 80, 20), QStringLiteral("Hello")},
        {QRectF(10, 100, 50, 20), QStringLiteral("中文")},
        {QRectF(70, 102, 50, 20), QStringLiteral("相连")},
        {QRectF(10, 140, 50, 20), QStringLiteral("  ")},
    };
    expect("assemble", assembleOcrText(frags), QStringLiteral("Hello world\n第二行\n中文相连"));
    expect("assemble empty", assembleOcrText({}), QString());

    return failures ? 1 : 0;
}
