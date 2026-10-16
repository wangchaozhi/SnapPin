#pragma once

#include <QImage>
#include <QRect>
#include <cstdint>
#include <vector>

// 基于图像内容的界面元素识别：在截图里找“由直线边框围成的矩形”（按钮、输入框、面板、色块）。
// 不依赖窗口或无障碍接口，远程桌面、虚拟机里的界面和图片里的界面都能识别。
// 所有坐标都是图像的物理像素。
class ImageRegionDetector {
public:
    explicit ImageRegionDetector(const QImage& image, int minLineLength = 16);

    // 包含 p 的、四边都由足够长的直线边缘围成的最小矩形（含边框线本身）。
    // 找不到时返回空矩形；一直扩到图像边缘的也视为没找到
    QRect regionAt(const QPoint& p) const;

private:
    // 横向边缘：第 y 行与 y+1 行之间颜色突变；纵向边缘：第 x 列与 x+1 列之间
    bool hEdge(int x, int y) const { return m_h[size_t(y) * size_t(m_w) + size_t(x)] != 0; }
    bool vEdge(int x, int y) const { return m_v[size_t(y) * size_t(m_w) + size_t(x)] != 0; }
    // 经过 (x, y) 的边缘线段的范围，允许 kGap 像素的断点（抗锯齿、虚线）
    void hExtent(int x, int y, int& x0, int& x1) const;
    void vExtent(int x, int y, int& y0, int& y1) const;
    bool hLine(int x, int y) const; // 经过 (x, y) 的横向边缘线足够长
    bool vLine(int x, int y) const;

    int m_w = 0, m_h0 = 0;
    int m_minLen;
    std::vector<uint8_t> m_h, m_v;
};
