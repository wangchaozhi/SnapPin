#include "scrollstitcher.h"

#include <QHashFunctions>
#include <QPainter>
#include <climits>
#include <cstring>

namespace {
constexpr double kMinScore = 0.85;     // 重叠区内可比较行的最低吻合比例
constexpr int kMinInformativeRows = 8; // 重叠区里至少要有这么多非空白行才可信
constexpr int kMinChangedRows = 4;     // 变化行少于此数视为光标闪烁等噪声
constexpr int kStrictBelowRows = 20;   // 可比较行少于此数时要求完全吻合
} // namespace

ScrollStitcher::ScrollStitcher(int ignoreRight, int maxHeight)
    : m_ignoreRight(qMax(0, ignoreRight))
    , m_maxHeight(maxHeight)
{
}

ScrollStitcher::Rows ScrollStitcher::analyze(const QImage& img) const
{
    const int h = img.height();
    const int w = qMax(1, img.width() - qMin(m_ignoreRight, img.width() / 4));
    Rows rows;
    rows.hash.resize(size_t(h));
    rows.uniform.resize(size_t(h));
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const quint32*>(img.constScanLine(y));
        rows.hash[size_t(y)] = qHashBits(line, size_t(w) * sizeof(quint32));
        bool uniform = true;
        for (int x = 1; x < w && uniform; ++x)
            uniform = line[x] == line[0];
        rows.uniform[size_t(y)] = uniform;
    }
    return rows;
}

ScrollStitcher::Rows ScrollStitcher::regionRows(const Rows& all) const
{
    const auto n = all.hash.size() - size_t(m_top) - size_t(m_bottom);
    Rows r;
    r.hash.assign(all.hash.begin() + m_top, all.hash.begin() + m_top + qsizetype(n));
    r.uniform.assign(all.uniform.begin() + m_top, all.uniform.begin() + m_top + qsizetype(n));
    return r;
}

bool ScrollStitcher::detectBands(const Rows& a, const Rows& b, int& top, int& bottom) const
{
    const int h = int(a.hash.size());
    top = 0;
    while (top < h && a.hash[size_t(top)] == b.hash[size_t(top)])
        ++top;
    if (top == h)
        return false;
    bottom = 0;
    while (bottom < h - top && a.hash[size_t(h - 1 - bottom)] == b.hash[size_t(h - 1 - bottom)])
        ++bottom;

    // 滚动区域边缘恰好是空白行时，前后两帧这些行碰巧相同，会被误算进固定区域。
    // 把紧贴滚动区域、同一颜色的连续空白行还给滚动区域；
    // 整个固定区域都是同一颜色时视为纯色标题栏/工具栏，保持不动
    auto trailingBlank = [&](int from, int step, int limit) {
        int n = 0;
        while (n < limit && b.uniform[size_t(from + n * step)] && b.hash[size_t(from + n * step)] == b.hash[size_t(from)])
            ++n;
        return n;
    };
    if (top > 0) {
        const int n = trailingBlank(top - 1, -1, top);
        if (n < top)
            top -= n;
    }
    if (bottom > 0) {
        const int n = trailingBlank(h - bottom, 1, bottom);
        if (n < bottom)
            bottom -= n;
    }
    return h - top - bottom >= kMinChangedRows;
}

bool ScrollStitcher::locate(const Rows& region, int from, int to, int& pos) const
{
    const int m = int(region.hash.size());
    const int bodyH = int(m_bodyRows.hash.size());
    const int minOverlap = qMax(kMinInformativeRows, m / 10);
    from = qMax(from, minOverlap - m);
    to = qMin(to, bodyH - minOverlap);

    // 画面第 y 行对应 body 第 p+y 行。同分时取离当前位置最近的
    double bestScore = 0;
    int bestDist = INT_MAX;
    bool found = false;
    for (int p = from; p <= to; ++p) {
        const int y0 = qMax(0, -p);
        const int y1 = qMin(m, bodyH - p);
        int informative = 0;
        int matched = 0;
        for (int y = y0; y < y1; ++y) {
            // 两边都是空白才跳过；一边空白一边有内容算作不吻合
            if (region.uniform[size_t(y)] && m_bodyRows.uniform[size_t(p + y)])
                continue;
            ++informative;
            if (region.hash[size_t(y)] == m_bodyRows.hash[size_t(p + y)])
                ++matched;
        }
        if (informative < kMinInformativeRows)
            continue;
        // 证据少时（重叠区大多是空白）必须完全吻合，否则容易错位几个像素
        if (informative < kStrictBelowRows && matched != informative)
            continue;
        const double score = double(matched) / informative;
        const int dist = qAbs(p - m_viewTop);
        if (score > bestScore + 1e-9 || (score > bestScore - 1e-9 && dist < bestDist)) {
            bestScore = score;
            bestDist = dist;
            pos = p;
            found = true;
        }
    }
    return found && bestScore >= kMinScore;
}

void ScrollStitcher::place(const QImage& frame, const Rows& region, int pos)
{
    const int m = int(region.hash.size());
    const int bodyH = m_body.height();
    const int above = qMax(0, -pos);               // 需要接到上面的行数
    const int below = qMax(0, pos + m - bodyH);    // 需要接到下面的行数
    if (above == 0 && below == 0) {
        m_viewTop = pos;
        return;
    }

    const int newH = bodyH + above + below;
    QImage out(m_body.width(), newH, QImage::Format_RGB32);
    const auto bpl = size_t(out.bytesPerLine());
    // 上方新内容：画面滚动区域的前 above 行
    if (above)
        std::memcpy(out.bits(), frame.constScanLine(m_top), bpl * size_t(above));
    std::memcpy(out.scanLine(above), m_body.constBits(), bpl * size_t(bodyH));
    // 下方新内容：画面滚动区域的后 below 行
    if (below)
        std::memcpy(out.scanLine(above + bodyH), frame.constScanLine(m_top + m - below), bpl * size_t(below));
    m_body = std::move(out);

    std::vector<size_t> hash(region.hash.begin(), region.hash.begin() + above);
    hash.insert(hash.end(), m_bodyRows.hash.begin(), m_bodyRows.hash.end());
    hash.insert(hash.end(), region.hash.end() - below, region.hash.end());
    m_bodyRows.hash = std::move(hash);
    std::vector<bool> uniform(region.uniform.begin(), region.uniform.begin() + above);
    uniform.insert(uniform.end(), m_bodyRows.uniform.begin(), m_bodyRows.uniform.end());
    uniform.insert(uniform.end(), region.uniform.end() - below, region.uniform.end());
    m_bodyRows.uniform = std::move(uniform);

    m_viewTop = pos + above;
}

ScrollStitcher::Result ScrollStitcher::add(const QImage& frameIn)
{
    const QImage frame = frameIn.convertToFormat(QImage::Format_RGB32);

    if (m_first.isNull()) {
        m_first = frame;
        m_firstRows = analyze(frame);
        return Result::First;
    }
    if (frame.size() != m_first.size())
        return Result::NoMatch;
    const Rows rows = analyze(frame);

    if (!m_started) {
        // 第一次滚动：先识别固定区域，确认两帧确实能对上再建立 body
        int top = 0, bottom = 0;
        if (!detectBands(m_firstRows, rows, top, bottom))
            return Result::Unchanged;
        m_top = top;
        m_bottom = bottom;
        m_body = m_first.copy(0, top, m_first.width(), m_first.height() - top - bottom);
        m_bodyRows = regionRows(m_firstRows);
        m_viewTop = 0;
        const Rows region = regionRows(rows);
        const int m = int(region.hash.size());
        int pos = 0;
        if (!locate(region, -m, m, pos)) {
            m_top = m_bottom = 0; // 不是滚动（比如悬停高亮），下次重新识别
            return Result::NoMatch;
        }
        m_header = m_first.copy(0, 0, m_first.width(), top);
        m_footer = m_first.copy(0, m_first.height() - bottom, m_first.width(), bottom);
        m_started = true;
        m_lastScroll = pos;
        place(frame, region, pos);
        return pos == 0 ? Result::Moved : Result::Appended;
    }

    const Rows region = regionRows(rows);
    const int m = int(region.hash.size());
    // 与当前位置相同：没有滚动
    bool same = true;
    for (int y = 0; y < m && same; ++y)
        same = region.hash[size_t(y)] == m_bodyRows.hash[size_t(m_viewTop + y)];
    if (same)
        return Result::Unchanged;

    // 先在当前位置附近找，找不到再搜整个已拼接内容（滚得太快、又滚回已截过的地方）
    int pos = 0;
    if (!locate(region, m_viewTop - m, m_viewTop + m, pos)
        && !locate(region, -m, int(m_bodyRows.hash.size()), pos))
        return Result::NoMatch;

    const int bodyH = m_body.height();
    const int grow = qMax(0, -pos) + qMax(0, pos + m - bodyH);
    if (m_top + bodyH + grow + m_bottom > m_maxHeight)
        return Result::Full;

    m_lastScroll = pos - m_viewTop;
    place(frame, region, pos);
    return grow > 0 ? Result::Appended : Result::Moved;
}

int ScrollStitcher::resultHeight() const
{
    return m_started ? m_header.height() + m_body.height() + m_footer.height() : m_first.height();
}

QImage ScrollStitcher::result() const
{
    return resultPart(0, resultHeight());
}

QImage ScrollStitcher::resultPart(int y, int height) const
{
    if (!m_started)
        return m_first.copy(0, y, m_first.width(), height);
    y = qBound(0, y, resultHeight());
    height = qBound(0, height, resultHeight() - y);
    QImage out(m_body.width(), height, QImage::Format_RGB32);
    QPainter p(&out);
    p.translate(0, -y);
    p.drawImage(0, 0, m_header);
    p.drawImage(0, m_header.height(), m_body);
    p.drawImage(0, m_header.height() + m_body.height(), m_footer);
    return out;
}

QRect ScrollStitcher::currentView() const
{
    if (!m_started)
        return m_first.rect();
    return QRect(0, m_header.height() + m_viewTop, m_body.width(), m_first.height() - m_top - m_bottom);
}
