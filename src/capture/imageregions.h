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
    // blockGap：内容块之间至少要隔开多少像素的空白才算两块（字间距、行距比它小，会并成一块）
    explicit ImageRegionDetector(const QImage& image, int minLineLength = 16, int blockGap = 10);

    // 包含 p 的、四边都由足够长的直线边缘围成的最小矩形（含边框线本身）。
    // 找不到时返回空矩形；一直扩到图像边缘的也视为没找到
    QRect regionAt(const QPoint& p) const;

    // 在 bounds（通常是光标下的窗口 / 控件）里找光标下最细的元素：
    // 先找边框围成的矩形，再在里面按空白把文字段落、图标、图片这类没有边框的内容切成块。
    // 只有真的和旁边的内容分开了才算一块，单个按钮里的文字不会代替按钮本身。找不到时返回空矩形
    QRect elementAt(const QPoint& p, const QRect& bounds) const;

private:
    // 横向边缘：第 y 行与 y+1 行之间颜色突变；纵向边缘：第 x 列与 x+1 列之间
    bool hEdge(int x, int y) const { return m_h[size_t(y) * size_t(m_w) + size_t(x)] != 0; }
    bool vEdge(int x, int y) const { return m_v[size_t(y) * size_t(m_w) + size_t(x)] != 0; }
    // 经过 (x, y) 的边缘线段的范围，允许 kGap 像素的断点（抗锯齿、虚线）
    void hExtent(int x, int y, int& x0, int& x1) const;
    void vExtent(int x, int y, int& y0, int& y1) const;
    bool hLine(int x, int y) const; // 经过 (x, y) 的横向边缘线足够长
    bool vLine(int x, int y) const;
    QRect contentBlock(const QPoint& p, const QRect& container) const;
    // 边缘像素计数的积分图：任意矩形里有多少边缘像素 O(1) 可得
    int edgeCount(int x0, int y0, int x1, int y1) const;

    int m_w = 0, m_h0 = 0;
    int m_minLen;
    int m_gap;
    std::vector<uint8_t> m_h, m_v;
    std::vector<uint32_t> m_sat; // (w+1)×(h+1)
};
