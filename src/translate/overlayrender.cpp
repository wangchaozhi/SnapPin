#include "overlayrender.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QTextLayout>
#include <algorithm>

namespace {

int colorDistance(QRgb a, QRgb b)
{
    return qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b));
}

QRgb medianColor(QList<QRgb> pixels)
{
    if (pixels.isEmpty())
        return qRgb(255, 255, 255);
    auto channel = [&pixels](int (*get)(QRgb)) {
        QList<int> v;
        v.reserve(pixels.size());
        for (QRgb p : pixels)
            v << get(p);
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
    };
    return qRgb(channel(qRed), channel(qGreen), channel(qBlue));
}

// 背景色：段落外框外面一圈像素的中位数（文字以外的大多数像素）
QRgb backgroundColor(const QImage& img, const QRect& box)
{
    const QRect ring = box.adjusted(-2, -2, 2, 2).intersected(img.rect());
    QList<QRgb> pixels;
    for (int x = ring.left(); x <= ring.right(); ++x) {
        pixels << img.pixel(x, ring.top()) << img.pixel(x, ring.bottom());
    }
    for (int y = ring.top() + 1; y < ring.bottom(); ++y) {
        pixels << img.pixel(ring.left(), y) << img.pixel(ring.right(), y);
    }
    return medianColor(pixels);
}

// 文字颜色：框内和背景差别明显的像素里，取差别最大的那一批的中位数；对比不够时用黑或白
QRgb textColor(const QImage& img, const QList<QRectF>& lines, QRgb bg)
{
    QList<QPair<int, QRgb>> far;
    for (const QRectF& lf : lines) {
        const QRect r = lf.toAlignedRect().intersected(img.rect());
        const int step = qMax(1, qMin(r.width(), r.height()) / 16);
        for (int y = r.top(); y <= r.bottom(); y += step) {
            for (int x = r.left(); x <= r.right(); x += step) {
                const QRgb p = img.pixel(x, y);
                const int d = colorDistance(p, bg);
                if (d > 120)
                    far.append({d, p});
            }
        }
    }
    const int bgLuma = qGray(bg);
    const QRgb fallback = bgLuma > 140 ? qRgb(20, 20, 20) : qRgb(245, 245, 245);
    if (far.size() < 4)
        return fallback;
    std::sort(far.begin(), far.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QList<QRgb> top;
    for (qsizetype i = 0; i < qMax<qsizetype>(4, far.size() / 3); ++i)
        top << far[i].second;
    const QRgb c = medianColor(top);
    return qAbs(qGray(c) - bgLuma) < 80 ? fallback : c;
}

// 擦除范围：文字框里去掉“大部分像素都不是背景色”的整行、整列——那是框边上露出来的另一块背景
// （比如 OCR 框比按钮略高，框的上沿已经在按钮外面），不能涂成按钮的颜色
QRect eraseRect(const QImage& img, const QRect& box, QRgb bg)
{
    const QRect r = box.intersected(img.rect());
    if (r.isEmpty())
        return r;
    auto foreignRow = [&](int y) {
        int far = 0;
        for (int x = r.left(); x <= r.right(); ++x)
            far += colorDistance(img.pixel(x, y), bg) > 60;
        return far > r.width() * 6 / 10;
    };
    auto foreignCol = [&](int x) {
        int far = 0;
        for (int y = r.top(); y <= r.bottom(); ++y)
            far += colorDistance(img.pixel(x, y), bg) > 60;
        return far > r.height() * 6 / 10;
    };
    int top = r.top(), bottom = r.bottom(), left = r.left(), right = r.right();
    while (top < bottom && foreignRow(top))
        ++top;
    while (bottom > top && foreignRow(bottom))
        --bottom;
    while (left < right && foreignCol(left))
        ++left;
    while (right > left && foreignCol(right))
        --right;
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

// 在 rect 里按 font 排版，返回排完的行；放不下（超出高度）时 fits 为 false
struct Layout {
    QList<QTextLine> lines;
    qreal height = 0;
    bool fits = false;
};

void layoutText(QTextLayout& layout, const QRectF& rect, Layout* out)
{
    out->lines.clear();
    out->height = 0;
    layout.clearLayout();
    layout.beginLayout();
    bool overflowWidth = false;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(rect.width());
        line.setPosition(QPointF(0, out->height));
        out->height += line.height();
        if (line.naturalTextWidth() > rect.width() + 0.5)
            overflowWidth = true; // 一个长单词就比框宽
        out->lines.append(line);
    }
    layout.endLayout();
    out->fits = !overflowWidth && out->height <= rect.height() + 0.5;
}

// 文字是不是居中放在按钮、标签这类容器里：从框的中线向左右找背景色的边界，两边都不远且距离相近
bool centeredInContainer(const QImage& img, const QRectF& box, QRgb bg)
{
    const int y = qBound(0, qRound(box.center().y()), img.height() - 1);
    const int limit = qRound(box.height() * 4);
    auto distanceToEdge = [&](int from, int step) {
        for (int d = 1; d <= limit; ++d) {
            const int x = from + d * step;
            if (x < 0 || x >= img.width())
                return -1; // 到图片边缘还没碰到容器的边
            if (colorDistance(img.pixel(x, y), bg) > 60)
                return d;
        }
        return -1;
    };
    const int left = distanceToEdge(qFloor(box.left()), -1);
    const int right = distanceToEdge(qCeil(box.right()), 1);
    return left > 0 && right > 0 && qAbs(left - right) <= box.height();
}

bool isMultiLine(const TextBlock& b)
{
    return b.box.height() > b.lineHeight * 1.5;
}

// 排译文的范围：译文往往比原文长，单行文字（标题、标签）往右延伸到图片边缘或右边的下一段，
// 多行段落往下延伸到下面的下一段之前
QRectF layoutRect(const TextBlock& b, const QList<TextBlock>& all, const QSize& imageSize)
{
    QRectF r = b.box;
    const qreal gap = qMax<qreal>(4, b.lineHeight * 0.3);
    if (!isMultiLine(b)) {
        qreal right = imageSize.width() - gap;
        for (const TextBlock& o : all) {
            if (&o == &b || o.box.left() < b.box.right())
                continue;
            if (qMin(o.box.bottom(), b.box.bottom()) > qMax(o.box.top(), b.box.top())) // 同一行
                right = qMin(right, o.box.left() - gap);
        }
        r.setRight(qMax(r.right(), right));
    } else {
        qreal bottom = imageSize.height() - gap;
        for (const TextBlock& o : all) {
            if (&o == &b || o.box.top() < b.box.bottom())
                continue;
            if (qMin(o.box.right(), b.box.right()) > qMax(o.box.left(), b.box.left())) // 横向有重叠
                bottom = qMin(bottom, o.box.top() - gap);
        }
        r.setBottom(qMax(r.bottom(), bottom));
    }
    return r;
}

void drawBlockText(QPainter& p, const TextBlock& block, const QRectF& rect, const QString& text, const QColor& color,
                   bool centered)
{
    QFont font = QGuiApplication::font();
    const bool multiLine = isMultiLine(block);
    // 原文字号约为文字框高度的 0.75；放不下就逐步缩小，最小到原来的一半（再小看不清了，宁可超出框）
    const qreal start = qMax<qreal>(6, block.lineHeight * 0.75);
    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    Layout layout;
    QTextLayout textLayout(text);
    textLayout.setTextOption(option);
    qreal size = start;
    for (;; size *= 0.92) {
        font.setPixelSize(qMax(1, qRound(size)));
        textLayout.setFont(font);
        layoutText(textLayout, rect, &layout);
        if (layout.fits || size < start * 0.5)
            break;
    }
    if (!layout.fits) {
        // 还是放不下：允许在任意字符处换行
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        textLayout.setTextOption(option);
        layoutText(textLayout, rect, &layout);
    }
    // 单行的标签、按钮文字在原来的框里纵向居中；多行段落从顶上开始
    const qreal top = multiLine ? rect.top() : block.box.top() + (block.box.height() - layout.height) / 2;
    // 按钮里的文字按原来的中心对齐，其他的左对齐（译文更长时往右延伸）
    qreal left = rect.left();
    if (centered && layout.lines.size() == 1)
        left = block.box.center().x() - layout.lines.first().naturalTextWidth() / 2;
    p.setPen(color);
    textLayout.draw(&p, QPointF(left, top));
}

} // namespace

QImage renderTranslatedImage(const QImage& image, const QList<TextBlock>& blocks, const QStringList& translations)
{
    QImage out = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal dpr = image.devicePixelRatio();
    out.setDevicePixelRatio(1); // 文字框是像素坐标，直接按像素画
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    // 颜色都从原图取；先把所有原文擦掉，再统一画译文，相邻段落的擦除不会盖掉已经画好的译文
    struct Item {
        const TextBlock* block;
        QString text;
        QColor fg;
        bool centered;
    };
    QList<Item> items;
    for (qsizetype i = 0; i < blocks.size() && i < translations.size(); ++i) {
        const TextBlock& b = blocks[i];
        const QString text = translations[i].trimmed();
        if (text.isEmpty() || text == b.text.trimmed())
            continue;
        const QRgb bg = backgroundColor(image, b.box.toAlignedRect());
        p.fillRect(eraseRect(image, b.box.toAlignedRect().adjusted(-1, -1, 1, 1), bg), QColor(bg));
        items.append({&b, text, QColor(textColor(image, b.lines, bg)),
                      !isMultiLine(b) && centeredInContainer(image, b.box, bg)});
    }
    for (const Item& item : std::as_const(items))
        drawBlockText(p, *item.block, layoutRect(*item.block, blocks, image.size()), item.text, item.fg, item.centered);
    p.end();
    out.setDevicePixelRatio(dpr);
    return out;
}
