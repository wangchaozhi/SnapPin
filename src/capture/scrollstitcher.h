#pragma once

#include <QImage>
#include <QRect>
#include <vector>

// 长截图拼接：依次喂入同一区域、内容上下滚动后的截图帧，
// 通过逐行哈希找出当前画面在已拼接内容中的位置：
// 超出顶部的部分接到上面，超出底部的部分接到下面，滚回已截过的位置则只更新位置。
// 首尾不随滚动变化的行（吸顶标题栏、底部工具栏）在第一次滚动时识别为固定区域，只保留一份。
class ScrollStitcher {
public:
    enum class Result {
        First,     // 第一帧
        Appended,  // 拼接了新内容（上方或下方）
        Moved,     // 滚动到已截过的位置，没有新内容
        Unchanged, // 画面没有变化
        NoMatch,   // 找不到可信的重叠（滚动太快或内容变化太大）
        Full,      // 已达到最大高度
    };

    // ignoreRight：右侧不参与比较的像素宽度（滚动条滑块会随滚动移动）
    explicit ScrollStitcher(int ignoreRight = 0, int maxHeight = 30000);

    Result add(const QImage& frame);
    QImage result() const;
    QImage resultPart(int y, int height) const; // 只合成结果的一段（预览用，避免每次合成整张长图）
    int resultHeight() const;
    QRect currentView() const; // 当前画面的滚动区域在 result() 中的位置
    int lastScroll() const { return m_lastScroll; } // 上一次滚动位移，向上为负

private:
    struct Rows {
        std::vector<size_t> hash;
        std::vector<bool> uniform; // 单色行（空白）不参与匹配评分
    };
    Rows analyze(const QImage& img) const;
    bool detectBands(const Rows& a, const Rows& b, int& top, int& bottom) const;
    // 在 body 中查找滚动区域 rows 的位置 p（可为负或超出底部，表示露出了新内容）
    bool locate(const Rows& region, int from, int to, int& pos) const;
    void place(const QImage& frame, const Rows& region, int pos);
    Rows regionRows(const Rows& all) const;

    int m_ignoreRight;
    int m_maxHeight;

    QImage m_first;      // 识别出固定区域之前的第一帧
    Rows m_firstRows;
    bool m_started = false;

    int m_top = 0, m_bottom = 0; // 固定区域高度
    QImage m_header, m_footer;
    QImage m_body;               // 只含滚动区域内容
    Rows m_bodyRows;
    int m_viewTop = 0;           // 当前画面滚动区域在 body 中的位置
    int m_lastScroll = 0;
};
