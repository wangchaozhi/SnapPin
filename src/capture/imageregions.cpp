#include "imageregions.h"

#include <cstdlib>

namespace {

constexpr int kEdgeThreshold = 40; // 相邻像素 RGB 差值之和超过它算边缘（浅灰边框 #E0E0E0 对白底约 93）
constexpr int kGap = 2;            // 边缘线允许的断点
constexpr int kMaxExpand = 24;     // 某条边不闭合时向外找下一条线的次数上限

inline int colorDiff(QRgb a, QRgb b)
{
    return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
}

} // namespace

ImageRegionDetector::ImageRegionDetector(const QImage& image, int minLineLength, int blockGap)
    : m_minLen(minLineLength), m_gap(blockGap)
{
    const QImage img = image.convertToFormat(QImage::Format_RGB32);
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
        for (int x = 0; x < m_w; ++x) {
            if (next && colorDiff(row[x], next[x]) > kEdgeThreshold)
                h[x] = 1;
            if (x + 1 < m_w && colorDiff(row[x], row[x + 1]) > kEdgeThreshold)
                v[x] = 1;
        }
    }

    const size_t stride = size_t(m_w) + 1;
    m_sat.assign(stride * (size_t(m_h0) + 1), 0);
    for (int y = 0; y < m_h0; ++y) {
        uint32_t rowSum = 0;
        for (int x = 0; x < m_w; ++x) {
            rowSum += (hEdge(x, y) || vEdge(x, y)) ? 1 : 0;
            m_sat[(size_t(y) + 1) * stride + size_t(x) + 1] = m_sat[size_t(y) * stride + size_t(x) + 1] + rowSum;
        }
    }
}

int ImageRegionDetector::edgeCount(int x0, int y0, int x1, int y1) const
{
    const size_t stride = size_t(m_w) + 1;
    auto at = [&](int x, int y) { return m_sat[size_t(y) * stride + size_t(x)]; };
    return int(at(x1 + 1, y1 + 1) - at(x0, y1 + 1) - at(x1 + 1, y0) + at(x0, y0));
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
    return QRect(QPoint(x0, y0), QPoint(x1, y1));
}

QRect ImageRegionDetector::contentBlock(const QPoint& p, const QRect& container) const
{
    // 递归 XY 切分：交替按空白行、空白列切开，每次留下光标所在的那一段，直到切不动为止
    QRect r = container.adjusted(2, 2, -2, -2) & QRect(0, 0, m_w, m_h0); // 去掉容器自己的边框线
    if (!r.contains(p))
        return {};
    bool split = false;
    for (int round = 0; round < 16; ++round) {
        const QRect before = r;
        for (const bool rows : {true, false}) {
            const int from = rows ? r.top() : r.left();
            const int to = rows ? r.bottom() : r.right();
            const int at = rows ? p.y() : p.x();
            auto filled = [&](int i) {
                return rows ? edgeCount(r.left(), i, r.right(), i) > 0 : edgeCount(i, r.top(), i, r.bottom()) > 0;
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
            // 光标落在空白里：不是某个内容块
            if (segStart < 0 || at > segEnd + 1)
                return {};
            // 这一段之外还有别的内容，才说明真的把它和旁边的东西分开了（只是去掉空白边距不算）
            const int inside = rows ? edgeCount(r.left(), segStart, r.right(), segEnd)
                                    : edgeCount(segStart, r.top(), segEnd, r.bottom());
            if (edgeCount(r.left(), r.top(), r.right(), r.bottom()) > inside)
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

QRect ImageRegionDetector::elementAt(const QPoint& p, const QRect& bounds) const
{
    const QRect area = bounds & QRect(0, 0, m_w, m_h0);
    if (m_h.empty() || !area.contains(p))
        return {};
    QRect container = regionAt(p) & area;
    if (!container.contains(p))
        container = area;
    const QRect block = contentBlock(p, container);
    if (!block.isEmpty())
        return block;
    return container == area ? QRect() : container;
}
