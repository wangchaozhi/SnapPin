#include "ocr/ppocr.h"

#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <cstdio>

static int failures = 0;

static void check(const char* name, bool ok, const QString& got)
{
    std::printf("%s %s: \"%s\"\n", ok ? "ok  " : "FAIL", name, qPrintable(got));
    if (!ok)
        ++failures;
}

// 截图里常见的尺寸：14 像素左右的字，白底黑字
static QImage render(const QStringList& lines, const QFont& font)
{
    const QFontMetrics fm(font);
    int width = 0;
    for (const QString& l : lines)
        width = qMax(width, fm.horizontalAdvance(l));
    QImage img(width + 40, fm.height() * 2 * int(lines.size()) + 20, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setFont(font);
    p.setPen(QColor(0x22, 0x22, 0x22));
    for (int i = 0; i < lines.size(); ++i)
        p.drawText(20, 10 + fm.ascent() + i * fm.height() * 2, lines[i]);
    return img;
}

static QString joined(const QList<OcrFragment>& frags)
{
    return assembleOcrText(frags);
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PpOcrEngine engine;
    QString error;
    QElapsedTimer timer;
    timer.start();
    if (!engine.load(&error)) {
        std::printf("FAIL load: %s\n", qPrintable(error));
        return 1;
    }
    std::printf("load %lld ms\n", timer.elapsed());

    // 调试用：ppocr_test 图片路径，打印识别结果
    if (argc > 1) {
        for (int round = 0; round < 2; ++round) {
            timer.restart();
            const QString text = joined(engine.recognize(QImage(QString::fromLocal8Bit(argv[1]))));
            std::printf("%lld ms\n%s\n", timer.elapsed(), qPrintable(text));
        }
        return 0;
    }

    // 没有任何字体的环境（如 Windows 上用 QT_QPA_PLATFORM=offscreen 跑）画不出文字，只有方框，识别结果无意义：跳过
    if (QFontDatabase::families().isEmpty()) {
        std::printf("skip text checks: no fonts available\n");
        check("blank image", joined(engine.recognize(QImage(200, 60, QImage::Format_RGB32))).isEmpty(), QString());
        return failures ? 1 : 0;
    }

    QFont font;
    font.setPixelSize(15);
    timer.restart();
    const QString en = joined(engine.recognize(render({QStringLiteral("The quick brown fox jumps over the lazy dog 1234567890")}, font)));
    std::printf("english %lld ms\n", timer.elapsed());
    // 只比较字母、数字和空格：不同系统的默认字体下，模型偶尔会把笔画边缘认成 | 之类的符号
    QString words;
    for (const QChar c : en)
        if (c.isLetterOrNumber() || c == QLatin1Char(' '))
            words += c;
    words = words.simplified();
    check("english", words.contains(QStringLiteral("quick brown fox")) && words.contains(QStringLiteral("1234567890")), en);

    // 没有中文字体的环境（如精简的 CI 容器）跳过中文
    const QString zhLine = QStringLiteral("截图后能识别选区里的文字");
    if (QFontMetrics(font).inFontUcs4(zhLine.at(0).unicode())) {
        timer.restart();
        QString zh = joined(engine.recognize(render({zhLine, QStringLiteral("使用 Qt 开发，价格 99 元")}, font)));
        // 模型偶尔输出同一个字的繁体 / 日文字形（如“別”），按简体比较
        zh.replace(QChar(0x5225), QChar(0x522B));
        std::printf("chinese %lld ms\n", timer.elapsed());
        check("chinese", zh.contains(QStringLiteral("识别选区")) && zh.contains(QStringLiteral("\n")), zh);
    } else {
        std::printf("skip chinese: no CJK font\n");
    }

    check("blank image", joined(engine.recognize(render({QString()}, font))).isEmpty(), QString());
    return failures ? 1 : 0;
}
