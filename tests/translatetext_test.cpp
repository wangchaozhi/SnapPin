#include "translate/translatetext.h"

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
    // 目标语言：中文为主译成英文，其他译成中文；指定了就不变
    expect("auto zh->en", resolveTargetLanguage(QStringLiteral("auto"), QStringLiteral("打开文件 File")), QStringLiteral("en"));
    expect("auto en->zh", resolveTargetLanguage(QStringLiteral("auto"), QStringLiteral("Open the file 文")), QStringLiteral("zh-Hans"));
    expect("fixed target", resolveTargetLanguage(QStringLiteral("ja"), QStringLiteral("hello")), QStringLiteral("ja"));

    expect("join latin", joinLines({QStringLiteral("This is a"), QStringLiteral("sentence.")}), QStringLiteral("This is a sentence."));
    expect("join cjk", joinLines({QStringLiteral("这是一个"), QStringLiteral("句子。")}), QStringLiteral("这是一个句子。"));
    expect("join hyphen", joinLines({QStringLiteral("trans-"), QStringLiteral("lation")}), QStringLiteral("translation"));

    // 段落：左对齐、行距小的两行合成一段；远处的按钮单独一段；大标题不和正文合并
    const QList<OcrFragment> frags = {
        {QRectF(10, 10, 300, 40), QStringLiteral("Big Title")},
        {QRectF(10, 70, 280, 20), QStringLiteral("The quick brown fox jumps over")},
        {QRectF(10, 94, 200, 20), QStringLiteral("the lazy dog.")},
        {QRectF(400, 300, 60, 20), QStringLiteral("OK")},
    };
    const QList<TextBlock> blocks = groupTextBlocks(frags);
    expect("block count", QString::number(blocks.size()), QStringLiteral("3"));
    if (blocks.size() == 3) {
        expect("block title", blocks[0].text, QStringLiteral("Big Title"));
        expect("block paragraph", blocks[1].text, QStringLiteral("The quick brown fox jumps over the lazy dog."));
        expect("block button", blocks[2].text, QStringLiteral("OK"));
        expect("paragraph lines", QString::number(blocks[1].lines.size()), QStringLiteral("2"));
    }

    // 一行一项的列表不合并：短行、大写开头、句末标点
    const QList<TextBlock> list = groupTextBlocks({{QRectF(10, 10, 120, 20), QStringLiteral("feat/translate")},
                                                   {QRectF(10, 32, 130, 20), QStringLiteral("Remote Control")},
                                                   {QRectF(10, 54, 300, 20), QStringLiteral("This line is long enough to wrap.")},
                                                   {QRectF(10, 76, 300, 20), QStringLiteral("but this one starts a new item")}});
    expect("list kept apart", QString::number(list.size()), QStringLiteral("4"));

    // 同一行被检测切开的两段合成一句
    const QList<TextBlock> row = groupTextBlocks({{QRectF(10, 10, 60, 20), QStringLiteral("你好")},
                                                  {QRectF(80, 11, 60, 20), QStringLiteral("世界")}});
    expect("same row merged", row.value(0).text, QStringLiteral("你好世界"));

    expect("parse plain", parseTranslationArray(QStringLiteral("[\"a\",\"b\"]"), 2).join(u'|'), QStringLiteral("a|b"));
    expect("parse fenced", parseTranslationArray(QStringLiteral("```json\n[\"你好\"]\n```"), 1).join(u'|'), QStringLiteral("你好"));
    expect("parse wrong count", parseTranslationArray(QStringLiteral("[\"a\"]"), 2).join(u'|'), QString());
    expect("parse garbage", parseTranslationArray(QStringLiteral("sorry"), 1).join(u'|'), QString());

    return failures ? 1 : 0;
}
