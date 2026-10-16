#include "imageregions.h"

#include <cstdlib>

namespace {

constexpr int kEdgeThreshold = 30; // 相邻像素 RGB 差值之和超过它算边缘（浅灰边框 #E0E0E0 对白底约 93）
constexpr int kStrongEdge = 60;    // 局部连成一团时只用比这更强的边缘
constexpr int kGap = 2;            // 边缘线允许的断点
constexpr int kMaxExpand = 24;     // 某条边不闭合时向外找下一条线的次数上限
constexpr int kLongLine = 64;      // 这么长的直边缘算长线（输入框、色块的边）
constexpr int kHugeLine = 256;     // 这么长的算结构线（窗口边框、贯穿的分界线）
constexpr int kMaxPillHeight = 96; // 胶囊形（两端是半圆，没有竖边）的输入框、按钮最高多少
constexpr int kMinPillLine = 48;   // 胶囊上下边至少这么长
constexpr int kBgSamples = 15;
constexpr int kMaxComponentGaps = 15; // 局部连成的一团最大多少个 blockGap 宽 / 高（一个桌面图标连文字约 12 个），再大多半是顺着壁纸的线条连到了旁边
constexpr int kMaxSeedReach = 40;  // 光标在图标内部纯色上时，四周多远内有边缘算被围住     // 判断一行 / 一列的底色时取多少个采样点

inline int colorDiff(QRgb a, QRgb b)
{
    return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
}

} // namespace

ImageRegionDetector::ImageRegionDetector(const QImage& image, int minLineLength, int blockGap)
    : m_minLen(minLineLength), m_gap(blockGap)
{
    m_img = image.convertToFormat(QImage::Format_RGB32);
    const QImage& img = m_img;
    m_w = img.width();
    m_h0 = img.height();
    if (m_w < 2 || m_h0 < 2)
        return;
    m_h.assign(size_t(m_w) * size_t(m_h0), 0);
    m_v.assign(size_t(m_w) * size_t(m_h0), 0);
    for (int y = 0; y < m_h0; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const auto* next = y + 1 < m_h0 ? reinterpret_cast<const QRgb*>(img.constScanLine(y + 1)) : nullptr;
        uint8_t* h = &m_h[size_t(y) * size_t(m_w)];
        uint8_t* v = &m_v[size_t(y) * size_t(m_w)];
        // 1 = 边缘，2 = 强边缘（componentAt 只用强边缘连接，壁纸里柔和的线条不会把图标连到一起）
        for (int x = 0; x < m_w; ++x) {
            if (next) {
                const int d = colorDiff(row[x], next[x]);
                h[x] = d > kStrongEdge ? 2 : d > kEdgeThreshold ? 1 : 0;
            }
            if (x + 1 < m_w) {
                const int d = colorDiff(row[x], row[x + 1]);
                v[x] = d > kStrongEdge ? 2 : d > kEdgeThreshold ? 1 : 0;
            }
        }
    }

    // 切分内容用的两套前缀和，直边缘按长度分三类：
    // 短的（文字、图标）总算内容；长的（输入框、实心色块的边）在同方向切分时当作空白——
    // 按行切时只有长横线的行算空白行，但按列切时它算内容，实心色块中间的列不会变成空白；
    // 特别长的（窗口边框、贯穿的分界线）哪个方向都不算内容
    std::vector<uint8_t> hClass(size_t(m_w) * size_t(m_h0), 0), vClass(size_t(m_w) * size_t(m_h0), 0); // 0 无 1 短 2 长 3 特别长
    auto classifyRuns = [&](bool horizontal) {
        const int lines = horizontal ? m_h0 : m_w;
        const int len = horizontal ? m_w : m_h0;
        for (int a = 0; a < lines; ++a) {
            auto edge = [&](int i) { return horizontal ? hEdge(i, a) : vEdge(a, i); };
            for (int i = 0; i < len;) {
                if (!edge(i)) {
                    ++i;
                    continue;
                }
                int end = i;
                for (int k = i + 1, gap = 0; k < len && gap <= kGap; ++k) {
                    if (edge(k)) {
                        end = k;
                        gap = 0;
                    } else {
                        ++gap;
                    }
                }
                const int runLen = end - i + 1;
                const uint8_t cls = runLen < kLongLine ? 1 : runLen < kHugeLine ? 2 : 3;
                for (int k = i; k <= end; ++k) {
                    if (edge(k))
                        (horizontal ? hClass[size_t(a) * size_t(m_w) + size_t(k)]
                                    : vClass[size_t(k) * size_t(m_w) + size_t(a)]) = cls;
                }
                i = end + 1;
            }
        }
    };
    classifyRuns(true);
    classifyRuns(false);

    m_rowPre.assign(size_t(m_w + 1) * size_t(m_h0), 0);
    m_colPre.assign(size_t(m_h0 + 1) * size_t(m_w), 0);
    for (int y = 0; y < m_h0; ++y) {
        uint16_t* pre = &m_rowPre[size_t(y) * size_t(m_w + 1)];
        for (int x = 0; x < m_w; ++x) {
            const size_t i = size_t(y) * size_t(m_w) + size_t(x);
            pre[x + 1] = uint16_t(pre[x] + ((hClass[i] == 1 || vClass[i] == 1 || vClass[i] == 2) ? 1 : 0));
        }
    }
    // 短边缘（不属于长直线）和其中强边缘的积分图，componentAt 用它 O(1) 判断某个小矩形里有没有边缘。
    // 长直线（选中高亮框、分界线）不参与连接，否则会把相邻的图标连在一起
    const size_t stride = size_t(m_w) + 1;
    m_edgeSat.assign(stride * (size_t(m_h0) + 1), 0);
    m_strongSat.assign(stride * (size_t(m_h0) + 1), 0);
    for (int y = 0; y < m_h0; ++y) {
        uint32_t rowSum = 0, strongSum = 0;
        for (int x = 0; x < m_w; ++x) {
            const size_t i = size_t(y) * size_t(m_w) + size_t(x);
            rowSum += (hClass[i] == 1 || vClass[i] == 1) ? 1 : 0;
            strongSum += ((hClass[i] == 1 && m_h[i] == 2) || (vClass[i] == 1 && m_v[i] == 2)) ? 1 : 0;
            const size_t o = (size_t(y) + 1) * stride + size_t(x) + 1;
            m_edgeSat[o] = m_edgeSat[o - stride] + rowSum;
            m_strongSat[o] = m_strongSat[o - stride] + strongSum;
        }
    }
    for (int x = 0; x < m_w; ++x) {
        uint16_t* pre = &m_colPre[size_t(x) * size_t(m_h0 + 1)];
        for (int y = 0; y < m_h0; ++y) {
            const size_t i = size_t(y) * size_t(m_w) + size_t(x);
            pre[y + 1] = uint16_t(pre[y] + ((vClass[i] == 1 || hClass[i] == 1 || hClass[i] == 2) ? 1 : 0));
        }
    }
}

QRect ImageRegionDetector::componentAt(const QPoint& p) const
{
    // 先用所有边缘、正常的连接距离；连进壁纸细节（太大）时改成只用强边缘（壁纸里柔和的线条不算），
    // 再不行就收紧连接距离
    const int rx = qMax(1, m_gap / 4), ry = qMax(2, m_gap / 2);
    QRect r = componentAt(p, rx, ry, m_edgeSat);
    if (r.isEmpty())
        r = componentAt(p, rx, ry, m_strongSat);
    if (r.isEmpty())
        r = componentAt(p, 1, qMax(2, m_gap / 4), m_strongSat);
    return r;
}

QRect ImageRegionDetector::componentAt(const QPoint& p, int rx, int ry, const std::vector<uint32_t>& sat) const
{
    // 从光标出发，把附近的边缘像素连成一团：上下连得远（图标和下面的文字、多行文字），左右连得近
    // （同一个词里的字，但不连到旁边图标的文字）。连成的这一团就是光标下的元素。
    // 在细节很多的壁纸上会越连越大，超过上限就放弃
    auto edgesIn = [&](int x0, int y0, int x1, int y1) {
        x0 = qMax(0, x0);
        y0 = qMax(0, y0);
        x1 = qMin(m_w - 1, x1);
        y1 = qMin(m_h0 - 1, y1);
        const size_t stride = size_t(m_w) + 1;
        auto at = [&](int x, int y) { return sat[size_t(y) * stride + size_t(x)]; };
        return at(x1 + 1, y1 + 1) - at(x0, y1 + 1) - at(x1 + 1, y0) + at(x0, y0);
    };
    // 只沿水平、竖直方向连（十字形），不斜着连：密排的桌面图标里，旁边居中的长文字会斜着挨到这边的文字
    auto on = [&](int x, int y) { return edgesIn(x - rx, y, x + rx, y) > 0 || edgesIn(x, y - ry, x, y + ry) > 0; };
    // 光标在图标内部一片纯色上时附近没有边缘：四个方向不远处都有边缘（被围住）就从最近的那个出发
    QPoint seed = p;
    if (!on(p.x(), p.y())) {
        int best = kMaxSeedReach + 1;
        for (const QPoint d : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
            int dist = 1;
            QPoint q = p + d;
            for (; dist <= kMaxSeedReach && q.x() >= 0 && q.y() >= 0 && q.x() < m_w && q.y() < m_h0; ++dist, q += d)
                if (on(q.x(), q.y()))
                    break;
            if (dist > kMaxSeedReach || q.x() < 0 || q.y() < 0 || q.x() >= m_w || q.y() >= m_h0)
                return {}; // 有一个方向是开着的：光标在空白处
            if (dist < best) {
                best = dist;
                seed = q;
            }
        }
    }

    const int maxW = kMaxComponentGaps * m_gap, maxH = kMaxComponentGaps * m_gap;
    const QRect window = QRect(p.x() - maxW, p.y() - maxH, 2 * maxW + 1, 2 * maxH + 1)
                         & QRect(0, 0, m_w, m_h0);
    std::vector<uint8_t> seen(size_t(window.width()) * size_t(window.height()), 0);
    auto mark = [&](int x, int y) -> bool {
        uint8_t& s = seen[size_t(y - window.top()) * size_t(window.width()) + size_t(x - window.left())];
        if (s)
            return false;
        s = 1;
        return true;
    };
    std::vector<QPoint> stack{seed};
    mark(seed.x(), seed.y());
    int x0 = m_w, y0 = m_h0, x1 = -1, y1 = -1; // 真正的边缘像素的范围
    while (!stack.empty()) {
        const QPoint q = stack.back();
        stack.pop_back();
        if (edgesIn(q.x(), q.y(), q.x(), q.y()) > 0) {
            x0 = qMin(x0, q.x());
            y0 = qMin(y0, q.y());
            x1 = qMax(x1, q.x());
            y1 = qMax(y1, q.y());
            if (x1 - x0 > maxW || y1 - y0 > maxH)
                return {};
        }
        for (const QPoint d : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
            const QPoint n = q + d;
            if (!window.contains(n))
                return {}; // 碰到搜索窗口边缘：太大了
            if (on(n.x(), n.y()) && mark(n.x(), n.y()))
                stack.push_back(n);
        }
    }
    if (x1 < 0)
        return {};
    // 边缘在像素和它右边 / 下边那个像素之间，再留两像素边距
    const QRect r = QRect(QPoint(x0 - 1, y0 - 1), QPoint(x1 + 3, y1 + 3)) & QRect(0, 0, m_w, m_h0);
    return r.width() >= m_minLen / 2 && r.height() >= m_minLen / 2 ? r : QRect();
}

bool ImageRegionDetector::rowHasContent(int y, int x0, int x1) const
{
    const uint16_t* pre = &m_rowPre[size_t(y) * size_t(m_w + 1)];
    return pre[x1 + 1] != pre[x0];
}

bool ImageRegionDetector::colHasContent(int x, int y0, int y1) const
{
    const uint16_t* pre = &m_colPre[size_t(x) * size_t(m_h0 + 1)];
    return pre[y1 + 1] != pre[y0];
}

void ImageRegionDetector::hExtent(int x, int y, int& x0, int& x1) const
{
    x0 = x1 = x;
    for (int gap = 0, i = x - 1; i >= 0 && gap <= kGap; --i) {
        if (hEdge(i, y)) {
            x0 = i;
            gap = 0;
        } else {
            ++gap;
        }
    }
    for (int gap = 0, i = x + 1; i < m_w && gap <= kGap; ++i) {
        if (hEdge(i, y)) {
            x1 = i;
            gap = 0;
        } else {
            ++gap;
        }
    }
}

void ImageRegionDetector::vExtent(int x, int y, int& y0, int& y1) const
{
    y0 = y1 = y;
    for (int gap = 0, i = y - 1; i >= 0 && gap <= kGap; --i) {
        if (vEdge(x, i)) {
            y0 = i;
            gap = 0;
        } else {
            ++gap;
        }
    }
    for (int gap = 0, i = y + 1; i < m_h0 && gap <= kGap; ++i) {
        if (vEdge(x, i)) {
            y1 = i;
            gap = 0;
        } else {
            ++gap;
        }
    }
}

bool ImageRegionDetector::hLine(int x, int y) const
{
    if (!hEdge(x, y))
        return false;
    int x0, x1;
    hExtent(x, y, x0, x1);
    return x1 - x0 + 1 >= m_minLen;
}

bool ImageRegionDetector::vLine(int x, int y) const
{
    if (!vEdge(x, y))
        return false;
    int y0, y1;
    vExtent(x, y, y0, y1);
    return y1 - y0 + 1 >= m_minLen;
}

QRect ImageRegionDetector::regionAt(const QPoint& p) const
{
    if (m_h.empty() || p.x() < 0 || p.y() < 0 || p.x() >= m_w || p.y() >= m_h0)
        return {};

    // 四条边界：top / bottom 是横向边缘所在的“行间”编号，left / right 是纵向边缘的“列间”编号；-1 或越界表示图像边缘
    auto findUp = [&](int from) { for (int y = from; y >= 0; --y) if (hLine(p.x(), y)) return y; return -1; };
    auto findDown = [&](int from) { for (int y = from; y < m_h0 - 1; ++y) if (hLine(p.x(), y)) return y; return m_h0 - 1; };
    auto findLeft = [&](int from) { for (int x = from; x >= 0; --x) if (vLine(x, p.y())) return x; return -1; };
    auto findRight = [&](int from) { for (int x = from; x < m_w - 1; ++x) if (vLine(x, p.y())) return x; return m_w - 1; };

    int top = findUp(p.y() - 1);
    int bottom = findDown(p.y());
    int left = findLeft(p.x() - 1);
    int right = findRight(p.x());

    // 检查每条边是否真的“横跨”这个矩形；不闭合的边（比如文字下划线、内容里的分隔短线）向外换下一条
    for (int round = 0; round < kMaxExpand; ++round) {
        const int width = right - left;
        const int height = bottom - top;
        const int tolX = qMax(6, width / 8);
        const int tolY = qMax(6, height / 8);
        bool ok = true;
        if (top >= 0) {
            int x0, x1;
            hExtent(p.x(), top, x0, x1);
            if (x0 > left + 1 + tolX || x1 < right - tolX) {
                top = findUp(top - 1);
                ok = false;
            }
        }
        if (bottom < m_h0 - 1) {
            int x0, x1;
            hExtent(p.x(), bottom, x0, x1);
            if (x0 > left + 1 + tolX || x1 < right - tolX) {
                bottom = findDown(bottom + 1);
                ok = false;
            }
        }
        if (left >= 0) {
            int y0, y1;
            vExtent(left, p.y(), y0, y1);
            if (y0 > top + 1 + tolY || y1 < bottom - tolY) {
                left = findLeft(left - 1);
                ok = false;
            }
        }
        if (right < m_w - 1) {
            int y0, y1;
            vExtent(right, p.y(), y0, y1);
            if (y0 > top + 1 + tolY || y1 < bottom - tolY) {
                right = findRight(right + 1);
                ok = false;
            }
        }
        if (ok)
            break;
        if (round == kMaxExpand - 1)
            return {};
    }

    // 四边都扩到了图像边缘：没有找到框
    if (top < 0 && left < 0 && bottom >= m_h0 - 1 && right >= m_w - 1)
        return {};
    // 包含边框线本身：上 / 左边界取边缘外侧那一行 / 列，下 / 右边界取边缘外侧那一行 / 列
    const int x0 = qMax(0, left);
    const int y0 = qMax(0, top);
    const int x1 = qMin(m_w - 1, right + 1);
    const int y1 = qMin(m_h0 - 1, bottom + 1);
    // 比最短边线还小的“框”是图标、文字里的笔画，不是控件
    if (x1 - x0 + 1 < m_minLen || y1 - y0 + 1 < m_minLen)
        return {};
    return QRect(QPoint(x0, y0), QPoint(x1, y1));
}

QRect ImageRegionDetector::pillAt(const QPoint& p) const
{
    // 上下各有一条横线、两条线左右端几乎对齐：两端是圆角（没有竖直边）的输入框、按钮。
    // 最近的横线可能是里面文字的笔画，所以向外逐对尝试，取最近的一对对齐的线
    struct Line { int y, x0, x1; };
    std::vector<Line> ups, downs;
    for (int y = p.y() - 1; y >= 0 && p.y() - y < kMaxPillHeight; --y)
        if (hLine(p.x(), y)) {
            int x0, x1;
            hExtent(p.x(), y, x0, x1);
            ups.push_back({y, x0, x1});
        }
    for (int y = p.y(); y < m_h0 - 1 && y - p.y() < kMaxPillHeight; ++y)
        if (hLine(p.x(), y)) {
            int x0, x1;
            hExtent(p.x(), y, x0, x1);
            downs.push_back({y, x0, x1});
        }
    // 所有对齐的上下线对；胶囊的上下边比高度长得多，文字笔画偶尔也会上下对齐，但没这么长
    struct Pair { int top, bottom, x0, x1; };
    std::vector<Pair> pairs;
    int minSpan = kMaxPillHeight + 1;
    for (const Line& a : ups) {
        for (const Line& b : downs) {
            const int span = b.y - a.y;
            const int minLen = qMax(kMinPillLine, 2 * span);
            if (span < m_minLen || span > kMaxPillHeight || qAbs(a.x0 - b.x0) > 3 || qAbs(a.x1 - b.x1) > 3
                || a.x1 - a.x0 + 1 < minLen || b.x1 - b.x0 + 1 < minLen)
                continue;
            pairs.push_back({a.y, b.y, qMin(a.x0, b.x0), qMax(a.x1, b.x1)});
            minSpan = qMin(minSpan, span);
        }
    }
    // 最近的一对是边框；粗边框（或抗锯齿）内外两条线都对齐，在只比它高几个像素的线对里取最外侧的，框住整条边框
    int top = -1, bottom = -1, lineX0 = 0, lineX1 = 0;
    for (const Pair& pr : pairs) {
        if (pr.bottom - pr.top <= minSpan + 4 && (top < 0 || pr.bottom - pr.top > bottom - top)) {
            top = pr.top;
            bottom = pr.bottom;
            lineX0 = pr.x0;
            lineX1 = pr.x1;
        }
    }
    if (top < 0)
        return {};
    // 圆角从直线两端往外再延伸最多半个高度；在中间那一行找最外侧的边缘作为左右边界
    const int h = bottom - top;
    const int mid = (top + bottom) / 2;
    int left = lineX0, right = lineX1 + 1;
    // 碰到边缘后，再隔开一段空白就停（后面是旁边的图标，不属于这个控件）；里面靠边的头像等图标和圆角之间也有空白，所以按高度放宽
    const int maxGap = h / 3;
    const int lineStart = left;
    for (int x = lineStart - 1, gap = -1; x >= qMax(0, lineStart - h / 2 - 2) && gap <= maxGap; --x) {
        if (vEdge(x, mid)) {
            left = x;
            gap = 0;
        } else if (gap >= 0) {
            ++gap;
        }
    }
    const int lineEnd = right;
    for (int x = lineEnd, gap = -1; x < qMin(m_w - 1, lineEnd + h / 2 + 2) && gap <= maxGap; ++x) {
        if (vEdge(x, mid)) {
            right = x + 1;
            gap = 0;
        } else if (gap >= 0) {
            ++gap;
        }
    }
    const QRect r(QPoint(left, top), QPoint(right, bottom + 1));
    return r.contains(p) && r.width() >= m_minLen ? r : QRect();
}

bool ImageRegionDetector::background(bool row, int i, const QRect& r, QRgb& color) const
{
    // 在这一行（列）上均匀取几个点，出现最多的颜色就是底色；文字只占少数点，不影响。
    // 最多的颜色也不到六成时（比如穿过一排图标的那几列）算“混杂”，没有明确的底色
    const int lo = row ? r.left() : r.top();
    const int hi = row ? r.right() : r.bottom();
    QRgb samples[kBgSamples];
    for (int k = 0; k < kBgSamples; ++k) {
        const int j = lo + int((qint64(hi - lo) * (2 * k + 1)) / (2 * kBgSamples));
        samples[k] = row ? m_img.pixel(j, i) : m_img.pixel(i, j);
    }
    int best = 0, bestVotes = 0;
    for (int k = 0; k < kBgSamples; ++k) {
        int votes = 0;
        for (const QRgb s : samples)
            votes += colorDiff(samples[k], s) <= kEdgeThreshold ? 1 : 0;
        if (votes > bestVotes) {
            bestVotes = votes;
            best = k;
        }
    }
    color = samples[best];
    return bestVotes * 10 >= kBgSamples * 6;
}

void ImageRegionDetector::backgroundRun(bool rows, const QRect& r, int at, int& start, int& end) const
{
    // 从光标所在行（列）向两边扩，和上一行（列）底色相近就连成一段：渐变的壁纸、背景不会被切开，
    // 只有底色突然变了（标签和标签栏、任务栏和壁纸）才断。夹在中间的分界线、零星的杂色（短于最短边线）不打断，
    // 底色混杂的行（列）也不打断
    const int from = rows ? r.top() : r.left();
    const int to = rows ? r.bottom() : r.right();
    start = from;
    end = to;
    QRgb ref;
    if (!background(rows, at, r, ref))
        return; // 光标所在处就没有明确的底色：不按底色切
    start = end = at;
    for (const int dir : {-1, 1}) {
        QRgb prev = ref;
        for (int i = at + dir, miss = 0; i >= from && i <= to && miss < m_minLen; i += dir) {
            QRgb bg;
            const bool clear = background(rows, i, r, bg);
            // 混杂的行（列）只在紧挨着这一段时并进来；已经断开了的话不能靠它们又连上
            if (clear ? colorDiff(bg, prev) <= kEdgeThreshold : miss == 0) {
                (dir < 0 ? start : end) = i;
                if (clear)
                    prev = bg;
                miss = 0;
            } else {
                ++miss;
            }
        }
    }
}

QRect ImageRegionDetector::contentBlock(const QPoint& p, const QRect& container, bool* isRegion) const
{
    if (isRegion)
        *isRegion = false;
    // 递归 XY 切分：交替按行、按列切，每次留下光标所在的那一段，直到切不动为止。两种切法：
    // 1. 底色变了：标签栏里白色的当前标签、桌面上的任务栏、侧边栏……底色不同的一段自成一个区域
    // 2. 空白：区域里按足够宽的空白把文字段落、图标切成内容块
    // 光标在某个区域的空白处时返回这个区域；在内容上时返回内容块
    QRect r = container.adjusted(2, 2, -2, -2) & QRect(0, 0, m_w, m_h0); // 去掉容器自己的边框线
    if (!r.contains(p))
        return {};
    QRect region;      // 最近一次按底色切出来的区域
    QRect area = r;    // 底色区域的范围：判断底色时在整个区域的宽（高）上取样，不受内容裁剪影响（否则按钮上的黑字会被当成底色）
    bool split = false;
    for (int round = 0; round < 16; ++round) {
        const QRect before = r;
        for (const bool rows : {true, false}) {
            const int at = rows ? p.y() : p.x();

            int runStart, runEnd;
            backgroundRun(rows, area, at, runStart, runEnd);
            const int from0 = rows ? area.top() : area.left();
            const int to0 = rows ? area.bottom() : area.right();
            // 切掉的部分至少有最短边线那么宽才算，边框内侧一两行抗锯齿的颜色不算换了底色
            if ((runStart - from0 >= m_minLen || to0 - runEnd >= m_minLen) && runEnd - runStart + 1 >= m_minLen) {
                if (rows)
                    area.setRect(area.x(), runStart, area.width(), runEnd - runStart + 1);
                else
                    area.setRect(runStart, area.y(), runEnd - runStart + 1, area.height());
                r &= area;
                region = area;
                split = true;
            }

            const int from = rows ? r.top() : r.left();
            const int to = rows ? r.bottom() : r.right();
            auto filled = [&](int i) {
                return rows ? rowHasContent(i, r.left(), r.right()) : colHasContent(i, r.top(), r.bottom());
            };
            // 找包含 at 的内容段：段内空白不超过 m_gap
            int segStart = -1, segEnd = -1, lastFilled = -1, start = -1;
            for (int i = from; i <= to; ++i) {
                if (!filled(i))
                    continue;
                if (lastFilled >= 0 && i - lastFilled - 1 > m_gap) {
                    if (at <= lastFilled)
                        break;
                    start = -1;
                }
                if (start < 0)
                    start = i;
                lastFilled = i;
                if (at >= start) {
                    segStart = start;
                    segEnd = i;
                }
            }
            // 光标落在空白里：是底色区域本身（有的话），不是某个内容块
            if (segStart < 0 || at > segEnd + 1)
            {
                if (isRegion)
                    *isRegion = !region.isEmpty();
                return region.isEmpty() ? QRect() : (region & container);
            }
            // 这一段之外还有别的内容，才说明真的把它和旁边的东西分开了（只是去掉空白边距不算）
            for (int i = from; i <= to && !split; ++i)
                if ((i < segStart || i > segEnd) && filled(i))
                    split = true;
            if (rows)
                r.setRect(r.x(), segStart, r.width(), segEnd - segStart + 2); // 横向边缘在 y 与 y+1 之间
            else
                r.setRect(segStart, r.y(), segEnd - segStart + 2, r.height());
            r &= container;
            if (!r.contains(p))
                return {};
        }
        if (r == before)
            break;
    }
    if (!split)
        return {};
    return r.adjusted(-2, -2, 2, 2) & container;
}

QRect ImageRegionDetector::withCaption(const QRect& block, const QRect& container) const
{
    // 图标和下面的文字（桌面图标、宫格里的应用）间距比字间距大，会被切成两块。
    // 正上方 / 正下方不远处有一块和它居中对齐的，就并成一个；上下都有时只并近的那个
    // （桌面图标排成一列时，图标上面是上一个图标的文字，下面才是自己的）
    const int reach = 3 * m_gap;
    auto neighbour = [&](int dir, int& gap) -> QRect {
        const int x0 = qMax(container.left(), block.left());
        const int x1 = qMin(container.right(), block.right());
        const int yStart = dir > 0 ? block.bottom() + 1 : block.top() - 1;
        for (int y = yStart, d = 0; d <= reach && y >= container.top() && y <= container.bottom(); y += dir, ++d) {
            if (!rowHasContent(y, x0, x1))
                continue;
            // 这一行里离中线最近的内容像素，从它找所在的块
            const int cx = (x0 + x1) / 2;
            for (int off = 0; off <= (x1 - x0) / 2 + 1; ++off) {
                for (const int x : {cx - off, cx + off}) {
                    if (x < x0 || x > x1 || !colHasContent(x, y, y))
                        continue;
                    bool region = false;
                    const QRect other = contentBlock(QPoint(x, y), container, &region);
                    if (other.isEmpty() || region || other.intersects(block))
                        return {};
                    // 居中对齐，而且不比自己宽太多（不是旁边一整段文字）
                    const int dc = qAbs((other.left() + other.right()) - (block.left() + block.right())) / 2;
                    if (dc > qMax(3, qMin(other.width(), block.width()) / 20) || other.width() > 3 * block.width()
                        || block.width() > 3 * other.width())
                        return {};
                    gap = d;
                    return other;
                }
            }
            return {};
        }
        return {};
    };
    int gapDown = 0, gapUp = 0;
    const QRect down = neighbour(1, gapDown);
    const QRect up = neighbour(-1, gapUp);
    if (!down.isEmpty() && (up.isEmpty() || gapDown <= gapUp))
        return block | down;
    if (!up.isEmpty())
        return block | up;
    return block;
}

QRect ImageRegionDetector::elementAt(const QPoint& p, const QRect& bounds) const
{
    const QRect area = bounds & QRect(0, 0, m_w, m_h0);
    if (m_h.empty() || !area.contains(p))
        return {};
    // 容器取边框矩形和胶囊形里更小的那个
    QRect container = area;
    for (const QRect& r : {regionAt(p), pillAt(p)}) {
        const QRect c = r & area;
        if (c.contains(p) && qint64(c.width()) * c.height() < qint64(container.width()) * container.height())
            container = c;
    }
    // 从光标处局部地把附近的边缘连成一团。带边框的小块（实心方形的图标）连同下面的文字连成了一团时取这一团：
    // 这一团要把整个边框块包在里面（按钮里的文字连成的团在按钮里面，不会代替按钮）
    const QRect comp = componentAt(p) & area;
    auto reachesBeyond = [&](const QRect& inner) {
        const int m = qMax(2, m_gap / 2);
        return comp.left() < inner.left() - m || comp.top() < inner.top() - m || comp.right() > inner.right() + m
               || comp.bottom() > inner.bottom() + m;
    };
    // 只多出几像素的边距不算（那就是这个边框块本身）
    if (container != area && comp.contains(p) && comp.contains(container.adjusted(2, 2, -2, -2)) && reachesBeyond(container))
        return comp;
    bool isRegion = false;
    const QRect block = contentBlock(p, container, &isRegion);
    if (!block.isEmpty())
        return isRegion ? block : withCaption(block, container);
    // 按整行整列切不开（密密排列的桌面图标、细节多的背景上，每一行每一列都有东西）：用连成的那一团
    if (container == area && comp.contains(p))
        return comp;
    // 边框围成的小块（比如实心的图标）也可能带说明文字
    return container == area ? QRect() : withCaption(container, area);
}
