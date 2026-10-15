#include "scrollstitcher.h"

#include <QHash>
#include <QHashFunctions>
#include <QPainter>
#include <climits>
#include <cstdio>
#include <cstring>

namespace {
constexpr double kSureScore = 0.95;    // 吻合度达到这个值直接接受
constexpr double kMinScore = 0.35;     // 低于这个值一律不接受（视频、轮播等大块动画会拉低吻合度）
constexpr int kMinMatchedRows = 12;    // 至少要有这么多行吻合
constexpr double kMinMargin = 0.2;     // 介于两者之间时，需要比其他位置高出这么多
constexpr int kNeighborRows = 4;       // 比较“其他位置”时排除最佳位置附近的行
constexpr int kMinInformativeRows = 8; // 重叠区里至少要有这么多非空白行才可信
constexpr int kMinChangedRows = 4;     // 滚动区域至少要有这么多行
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

void ScrollStitcher::bandsFromShift(const Rows& a, const Rows& b, int dy, int& top, int& bottom)
{
    // b 第 y 行 == a 第 y+dy 行。向上滚动时交换两帧即可套用同样的判断
    if (dy < 0) {
        bandsFromShift(b, a, -dy, top, bottom);
        return;
    }
    const int h = int(a.hash.size());
    auto scrolledTop = [&](int y) { return b.hash[size_t(y)] == a.hash[size_t(y + dy)]; };    // b 第 y 行跟着滚动
    auto scrolledBottom = [&](int z) { return a.hash[size_t(z)] == b.hash[size_t(z - dy)]; }; // a 第 z 行跟着滚动

    // 顶部：没有跟着滚动的行（静止或有动画的标题栏），直到第一行跟着滚动的有内容的行。
    // 空白行跟哪里都可能碰巧相同，先跳过，再把紧贴内容、确实跟着滚动的空白行还给滚动区域
    int y = 0;
    while (y < h - dy && (b.uniform[size_t(y)] || !scrolledTop(y)))
        ++y;
    while (y > 0 && b.uniform[size_t(y - 1)] && scrolledTop(y - 1))
        --y;
    top = y;

    // 底部：同理，从下往上
    int z = h - 1;
    while (z >= dy && (a.uniform[size_t(z)] || !scrolledBottom(z)))
        --z;
    while (z + 1 < h && z + 1 >= dy && a.uniform[size_t(z + 1)] && scrolledBottom(z + 1))
        ++z;
    bottom = h - 1 - z;
}

bool ScrollStitcher::locate(const Rows& region, int from, int to, int& pos) const
{
    const int m = int(region.hash.size());
    const int bodyH = int(m_bodyRows.hash.size());
    const int minOverlap = qMax(kMinInformativeRows, m / 10);
    from = qMax(from, minOverlap - m);
    to = qMin(to, bodyH - minOverlap);

    // 行的权重 = 1 / 出现次数：重复出现的行（同样的句子、列表项的边框）几乎不能说明位置，
    // 独一无二的行才是有力证据。错位时只能对上重复行，得分会远低于正确位置
    QHash<size_t, int> occurrences;
    for (const size_t h : region.hash)
        ++occurrences[h];
    for (int y = qMax(0, from); y < qMin(bodyH, to + m); ++y)
        ++occurrences[m_bodyRows.hash[size_t(y)]];
    auto weight = [&](size_t h) { return 1.0 / occurrences.value(h, 1); };

    // 画面第 y 行对应 body 第 p+y 行。同分时取离当前位置最近的
    double bestScore = 0;
    int bestDist = INT_MAX;
    int bestMatched = 0;
    bool found = false;
    std::vector<std::pair<int, double>> scores; // 用于判断最佳位置是否明显胜出
    for (int p = from; p <= to; ++p) {
        const int y0 = qMax(0, -p);
        const int y1 = qMin(m, bodyH - p);
        int informative = 0;
        int matched = 0;
        double total = 0;
        double hit = 0;
        for (int y = y0; y < y1; ++y) {
            // 两边都是空白才跳过；一边空白一边有内容算作不吻合
            if (region.uniform[size_t(y)] && m_bodyRows.uniform[size_t(p + y)])
                continue;
            if ((!region.ignore.empty() && region.ignore[size_t(y)])
                || (!m_bodyRows.ignore.empty() && m_bodyRows.ignore[size_t(p + y)]))
                continue;
            const size_t a = region.hash[size_t(y)];
            const size_t b = m_bodyRows.hash[size_t(p + y)];
            const double w = (weight(a) + weight(b)) / 2;
            ++informative;
            total += w;
            if (a == b) {
                ++matched;
                hit += w;
            }
        }
        if (informative < kMinInformativeRows)
            continue;
        // 证据少时（重叠区大多是空白）必须完全吻合，否则容易错位几个像素
        if (informative < kStrictBelowRows && matched != informative)
            continue;
        if (matched < kMinMatchedRows && matched != informative)
            continue;
        const double score = total > 0 ? hit / total : 0;
        scores.emplace_back(p, score);
        // 同分时先看吻合的行数（证据更多），再看离当前位置的远近
        const int dist = qAbs(p - m_viewTop);
        const bool tie = score > bestScore - 1e-9 && score < bestScore + 1e-9;
        if (score > bestScore + 1e-9 || (tie && (matched > bestMatched || (matched == bestMatched && dist < bestDist)))) {
            bestScore = score;
            bestDist = dist;
            bestMatched = matched;
            pos = p;
            found = true;
        }
    }
    if (!found || bestScore < kMinScore)
        return false;
    if (bestScore >= kSureScore)
        return true;
    // 有动画等原地变化时吻合度达不到很高，只要明显好于其他位置就接受；
    // 内容重复（多个位置得分接近）时拒绝，宁可不拼也不拼错
    // 排除最佳位置所在的整座“山丘”：内容里有连续相同的行时，错开几行也能部分吻合，
    // 得分从峰顶向两侧逐渐下降，这些不算竞争者；只和别的峰比
    size_t peak = 0;
    while (scores[peak].first != pos)
        ++peak;
    size_t lo = peak, hi = peak;
    while (lo > 0 && scores[lo - 1].first == scores[lo].first - 1 && scores[lo - 1].second <= scores[lo].second + 1e-9)
        --lo;
    while (hi + 1 < scores.size() && scores[hi + 1].first == scores[hi].first + 1
           && scores[hi + 1].second <= scores[hi].second + 1e-9)
        ++hi;
    double second = 0;
    int secondPos = 0;
    for (size_t i = 0; i < scores.size(); ++i) {
        const auto& [p, s] = scores[i];
        if ((i < lo || i > hi) && qAbs(p - pos) > kNeighborRows && s > second) {
            second = s;
            secondPos = p;
        }
    }
    if (qEnvironmentVariableIsSet("SNAPPIN_STITCH_DEBUG"))
        std::fprintf(stderr, "    locate best %d (%.3f) second %d (%.3f) range [%d,%d]\n", pos, bestScore, secondPos,
                     second, from, to);
    return bestScore - second >= kMinMargin;
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
    // 全部按物理像素处理：带缩放比的图（如 125% 屏幕截图）在合成时会被 QPainter 按逻辑尺寸缩小
    QImage frame = frameIn.convertToFormat(QImage::Format_RGB32);
    frame.setDevicePixelRatio(1.0);

    if (m_first.isNull()) {
        m_first = frame;
        m_firstRows = analyze(frame);
        return Result::First;
    }
    if (frame.size() != m_first.size())
        return Result::NoMatch;
    const Rows rows = analyze(frame);

    if (!m_started) {
        // 第一次滚动：先在整帧上找出滚动位移，再逐行看哪些行没有跟着滚动——
        // 顶部 / 底部这样的行就是吸顶 / 吸底区域（静止的或带动画的都算）
        const int h = frame.height();
        if (rows.hash == m_firstRows.hash)
            return Result::Unchanged;
        m_top = m_bottom = 0;
        m_body = m_first;
        m_bodyRows = m_firstRows;
        m_viewTop = 0;
        // 两帧同位置完全相同的行（静止的标题栏等）只在位移 0 处吻合，不能说明滚动了多少，不参与评分
        Rows moving = rows;
        moving.ignore.resize(size_t(h));
        for (int y = 0; y < h; ++y)
            moving.ignore[size_t(y)] = rows.hash[size_t(y)] == m_firstRows.hash[size_t(y)];
        m_bodyRows.ignore = moving.ignore;
        int shift = 0;
        const bool located = locate(moving, -h + 1, h - 1, shift);
        m_bodyRows.ignore.clear();
        if (!located)
            return Result::NoMatch;
        if (shift == 0)
            return Result::Unchanged; // 只有原地变化（动画、悬停高亮），还没滚动
        // 整帧匹配含有标题栏里的动画行，可能偏差几行；划出固定区域后在滚动区域内重新匹配，
        // 结果不一致就用新位移重新划分，直到两者一致
        int top = 0, bottom = 0, pos = 0;
        Rows region;
        for (int round = 0;; ++round) {
            bandsFromShift(m_firstRows, rows, shift, top, bottom);
            if (h - top - bottom < kMinChangedRows || round == 3) {
                m_top = m_bottom = 0;
                return Result::NoMatch;
            }
            m_top = top;
            m_bottom = bottom;
            m_bodyRows = regionRows(m_firstRows);
            region = regionRows(rows);
            const int m = int(region.hash.size());
            if (!locate(region, -m, m, pos) || pos == 0) {
                m_top = m_bottom = 0;
                return Result::NoMatch;
            }
            if (pos == shift)
                break;
            shift = pos;
        }
        m_body = m_first.copy(0, top, m_first.width(), h - top - bottom);
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
