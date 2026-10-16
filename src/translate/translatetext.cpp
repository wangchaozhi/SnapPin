#include "translatetext.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QObject>
#include <algorithm>
#include <numeric>

namespace {

bool isHan(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0xF900 && u <= 0xFAFF);
}

bool isCjk(QChar c)
{
    const ushort u = c.unicode();
    return isHan(c) || (u >= 0x3000 && u <= 0x30FF) || (u >= 0xFF00 && u <= 0xFFEF);
}

int findRoot(QList<int>& parent, int i)
{
    while (parent[i] != i)
        i = parent[i] = parent[parent[i]];
    return i;
}

} // namespace

const QList<TranslateLanguage>& translateLanguages()
{
    static const QList<TranslateLanguage> langs = {
        {QStringLiteral("auto"), QObject::tr("中文 ↔ 英文"), QString()},
        {QStringLiteral("zh-Hans"), QStringLiteral("简体中文"), QStringLiteral("Simplified Chinese")},
        {QStringLiteral("zh-Hant"), QStringLiteral("繁體中文"), QStringLiteral("Traditional Chinese")},
        {QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("English")},
        {QStringLiteral("ja"), QStringLiteral("日本語"), QStringLiteral("Japanese")},
        {QStringLiteral("ko"), QStringLiteral("한국어"), QStringLiteral("Korean")},
        {QStringLiteral("fr"), QStringLiteral("Français"), QStringLiteral("French")},
        {QStringLiteral("de"), QStringLiteral("Deutsch"), QStringLiteral("German")},
        {QStringLiteral("es"), QStringLiteral("Español"), QStringLiteral("Spanish")},
        {QStringLiteral("ru"), QStringLiteral("Русский"), QStringLiteral("Russian")},
    };
    return langs;
}

QString englishLanguageName(const QString& code)
{
    for (const TranslateLanguage& l : translateLanguages()) {
        if (l.code == code)
            return l.english;
    }
    return code;
}

QString resolveTargetLanguage(const QString& target, const QString& text)
{
    if (target != QLatin1String("auto"))
        return target;
    int han = 0;
    int letters = 0;
    for (QChar c : text) {
        if (isHan(c))
            ++han;
        if (c.isLetter())
            ++letters;
    }
    // 中文一个字的信息量抵得上好几个字母，汉字占到三成就算中文为主
    return letters > 0 && han * 10 >= letters * 3 ? QStringLiteral("en") : QStringLiteral("zh-Hans");
}

QString joinLines(const QStringList& lines)
{
    QString out;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (out.isEmpty()) {
            out = line;
        } else if (out.endsWith(QLatin1Char('-')) && out.size() > 1 && out.at(out.size() - 2).isLetter()
                   && line.front().isLower()) {
            out.chop(1); // 英文单词跨行时的连字符
            out += line;
        } else if (isCjk(out.back()) || isCjk(line.front())) {
            out += line;
        } else {
            out += QLatin1Char(' ') + line;
        }
    }
    return out;
}

QList<TextBlock> groupTextBlocks(const QList<OcrFragment>& input)
{
    QList<OcrFragment> frags;
    for (const OcrFragment& f : input) {
        if (!f.text.trimmed().isEmpty() && f.box.height() > 0)
            frags.append(f);
    }
    const int n = int(frags.size());
    QList<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    auto unite = [&](int a, int b) { parent[findRoot(parent, a)] = findRoot(parent, b); };

    for (int i = 0; i < n; ++i) {
        const QRectF a = frags[i].box;
        for (int j = i + 1; j < n; ++j) {
            const QRectF b = frags[j].box;
            const qreal h = qMin(a.height(), b.height());
            const qreal hMax = qMax(a.height(), b.height());
            if (hMax > h * 1.5) // 字号差得多（标题和正文）不合并
                continue;
            const qreal vOverlap = qMin(a.bottom(), b.bottom()) - qMax(a.top(), b.top());
            const qreal hGap = qMax(a.left(), b.left()) - qMin(a.right(), b.right());
            const qreal hOverlap = -hGap;
            if (vOverlap > 0.5 * h) {
                // 同一行：靠得近的片段是一句话被检测切开了
                if (hGap < 1.2 * h)
                    unite(i, j);
            } else {
                // 上下相邻的两行，只有像是同一段话折行时才合并：行距小、左边对齐、上一行排满了
                // 且没有在句末结束。界面截图里更常见的是一行一项的列表，宁可不合并
                const OcrFragment& upper = a.top() < b.top() ? frags[i] : frags[j];
                const OcrFragment& lower = a.top() < b.top() ? frags[j] : frags[i];
                const qreal vGap = lower.box.top() - upper.box.bottom();
                const QString tail = upper.text.trimmed();
                const bool sentenceEnd = !tail.isEmpty() && QStringLiteral(".!?:;。！？：；").contains(tail.back());
                // 折行的上一行足够长：中文至少 8 个字，其他文字至少 20 个字符；下一行大写字母开头多半是新的一项
                const qsizetype han = std::count_if(tail.cbegin(), tail.cend(), isCjk);
                const bool longLine = han >= 8 || tail.size() >= 20;
                const QString head = lower.text.trimmed();
                const bool newItem = !head.isEmpty() && head.front().isUpper() && !tail.endsWith(QLatin1Char(','))
                                     && !tail.endsWith(QLatin1Char('-'));
                if (vGap < 0.6 * h && vGap > -0.5 * h && qAbs(upper.box.left() - lower.box.left()) < 0.6 * h
                    && upper.box.width() >= 0.9 * qMax(upper.box.width(), lower.box.width()) && !sentenceEnd
                    && longLine && !newItem && hOverlap > 0)
                    unite(i, j);
            }
        }
    }

    QMap<int, QList<OcrFragment>> groups;
    for (int i = 0; i < n; ++i)
        groups[findRoot(parent, i)].append(frags[i]);

    QList<TextBlock> blocks;
    for (const QList<OcrFragment>& g : std::as_const(groups)) {
        TextBlock b;
        qreal heights = 0;
        for (const OcrFragment& f : g) {
            b.box = b.box.isNull() ? f.box : b.box.united(f.box);
            b.lines.append(f.box);
            heights += f.box.height();
        }
        b.lineHeight = heights / g.size();
        b.text = joinLines(assembleOcrText(g).split(QLatin1Char('\n')));
        blocks.append(b);
    }
    // 从上到下、从左到右，翻译请求和结果的顺序稳定
    std::sort(blocks.begin(), blocks.end(), [](const TextBlock& a, const TextBlock& b) {
        if (qAbs(a.box.top() - b.box.top()) > qMin(a.lineHeight, b.lineHeight) / 2)
            return a.box.top() < b.box.top();
        return a.box.left() < b.box.left();
    });
    return blocks;
}

QStringList parseTranslationArray(const QString& reply, int expected)
{
    const qsizetype begin = reply.indexOf(QLatin1Char('['));
    const qsizetype end = reply.lastIndexOf(QLatin1Char(']'));
    if (begin < 0 || end <= begin)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(reply.mid(begin, end - begin + 1).toUtf8());
    if (!doc.isArray())
        return {};
    const QJsonArray array = doc.array();
    if (array.size() != expected)
        return {};
    QStringList out;
    for (const QJsonValue& v : array) {
        if (!v.isString())
            return {};
        out << v.toString();
    }
    return out;
}
