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

ImageRegionDetector::ImageRegionDetector(const QImage& image, int minLineLength)
    : m_minLen(minLineLength)
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
