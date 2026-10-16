#include "ocrtext.h"

#include <algorithm>

namespace {

// 中日文字和全角标点。韩文词之间本来就有空格，不算在内
bool isCjk(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x3000 && u <= 0x30FF)     // 中日标点、假名
           || (u >= 0x3400 && u <= 0x4DBF)  // 扩展 A
           || (u >= 0x4E00 && u <= 0x9FFF)  // 基本汉字
           || (u >= 0xF900 && u <= 0xFAFF)  // 兼容汉字
           || (u >= 0xFF00 && u <= 0xFFEF)  // 全角字符
           || c.isHighSurrogate() || c.isLowSurrogate(); // 扩展 B 及以后
}

struct Line {
    qreal top = 0;
    qreal bottom = 0;
    QList<OcrFragment> items;
};

} // namespace

QString tidyCjkSpacing(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c != QLatin1Char(' ') && c != QLatin1Char('\t')) {
            out += c;
            continue;
        }
        qsizetype j = i;
        while (j < text.size() && (text.at(j) == QLatin1Char(' ') || text.at(j) == QLatin1Char('\t')))
            ++j;
        const bool cjkLeft = !out.isEmpty() && isCjk(out.back());
        const bool cjkRight = j < text.size() && isCjk(text.at(j));
        if (!(cjkLeft && cjkRight))
            out += text.mid(i, j - i);
        i = j - 1;
    }
    return out;
}

QString assembleOcrText(QList<OcrFragment> fragments)
{
    fragments.erase(std::remove_if(fragments.begin(), fragments.end(),
                                   [](const OcrFragment& f) { return f.text.trimmed().isEmpty(); }),
                    fragments.end());
    std::sort(fragments.begin(), fragments.end(),
              [](const OcrFragment& a, const OcrFragment& b) { return a.box.center().y() < b.box.center().y(); });

    QList<Line> lines;
    for (const OcrFragment& f : fragments) {
        Line* target = nullptr;
        for (Line& l : lines) {
            const qreal overlap = qMin(l.bottom, f.box.bottom()) - qMax(l.top, f.box.top());
            if (overlap > 0.5 * qMin(l.bottom - l.top, f.box.height())) {
                target = &l;
                break;
            }
        }
        if (!target) {
            lines.append({f.box.top(), f.box.bottom(), {}});
            target = &lines.last();
        }
        target->items.append(f);
    }
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.top < b.top; });

    QStringList rows;
    for (Line& l : lines) {
        std::sort(l.items.begin(), l.items.end(),
                  [](const OcrFragment& a, const OcrFragment& b) { return a.box.left() < b.box.left(); });
        QString row;
        for (const OcrFragment& f : l.items) {
            const QString t = f.text.trimmed();
            if (!row.isEmpty() && !(isCjk(row.back()) && isCjk(t.front())))
                row += QLatin1Char(' ');
            row += t;
        }
        rows << tidyCjkSpacing(row);
    }
    return rows.join(QLatin1Char('\n'));
}
