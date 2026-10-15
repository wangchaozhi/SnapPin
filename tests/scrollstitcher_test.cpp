// 用合成数据验证长截图拼接：带吸顶标题栏、底部工具栏和移动的滚动条滑块，上下两个方向滚动
#include "capture/scrollstitcher.h"

#include <QPainter>
#include <QRandomGenerator>
#include <cstdio>

namespace {

constexpr int W = 400, H = 300, kHeader = 40, kFooter = 30, kScrollbar = 14;
constexpr int kContent = 2000; // 可滚动内容总高度
constexpr int kView = H - kHeader - kFooter;

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

QImage makeContent()
{
    // 随机色块 + 部分空白行，模拟真实页面
    QImage img(W, kContent, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    auto* rng = QRandomGenerator::global();
    for (int y = 0; y < kContent; y += 18) {
        if (rng->bounded(5) == 0)
            continue; // 段落间空白
        for (int x = 0; x < W; x += 9)
            p.fillRect(x, y, 7, 12, QColor::fromRgb(rng->generate() | 0xff000000));
    }
    return img;
}

QImage makeFrame(const QImage& content, int scroll)
{
    QImage f(W, H, QImage::Format_RGB32);
    QPainter p(&f);
    p.fillRect(0, 0, W, kHeader, QColor(30, 60, 200));                 // 吸顶标题栏
    p.drawImage(QPoint(0, kHeader), content.copy(0, scroll, W, kView)); // 滚动区域
    p.fillRect(0, H - kFooter, W, kFooter, QColor(80, 80, 80));         // 底部工具栏
    // 滚动条：滑块位置随滚动变化，所有行都会受影响
    p.fillRect(W - kScrollbar, kHeader, kScrollbar, kView, QColor(230, 230, 230));
    const int thumb = kHeader + scroll * (kView - 40) / (kContent - kView);
    p.fillRect(W - kScrollbar + 2, thumb, kScrollbar - 4, 40, QColor(120, 120, 120));
    return f;
}

// 理想结果：标题栏 + 内容 [minScroll, maxScroll + kView) + 工具栏（忽略滚动条列）
QImage expected(const QImage& content, int minScroll, int maxScroll)
{
    const int bodyH = maxScroll + kView - minScroll;
    QImage e(W, kHeader + bodyH + kFooter, QImage::Format_RGB32);
    QPainter p(&e);
    p.fillRect(0, 0, W, kHeader, QColor(30, 60, 200));
    p.drawImage(QPoint(0, kHeader), content.copy(0, minScroll, W, bodyH));
    p.fillRect(0, e.height() - kFooter, W, kFooter, QColor(80, 80, 80));
    return e;
}

bool sameIgnoringScrollbar(const QImage& a, const QImage& b)
{
    if (a.size() != b.size())
        return false;
    const QRect r(0, 0, W - kScrollbar, a.height());
    return a.copy(r) == b.copy(r);
}

void runCase(const char* name, int start, const QList<int>& steps)
{
    std::printf("-- %s\n", name);
    const QImage content = makeContent();
    ScrollStitcher s(kScrollbar + 4);
    int scroll = start, lo = start, hi = start;

    check(s.add(makeFrame(content, scroll)) == ScrollStitcher::Result::First, "first frame");
    check(s.add(makeFrame(content, scroll)) == ScrollStitcher::Result::Unchanged, "unchanged frame");
    for (int step : steps) {
        const int previous = scroll;
        scroll = qBound(0, scroll + step, kContent - kView);
        const auto r = s.add(makeFrame(content, scroll));
        if (r == ScrollStitcher::Result::NoMatch) {
            // 重叠区几乎全是空白时拒绝拼接是安全的；模拟用户滚回去
            std::printf("   step %+d -> refused (safe)\n", step);
            scroll = previous;
            continue;
        }
        const bool grows = scroll < lo || scroll > hi;
        lo = qMin(lo, scroll);
        hi = qMax(hi, scroll);
        const bool ok = grows ? r == ScrollStitcher::Result::Appended : r == ScrollStitcher::Result::Moved;
        std::printf("   step %+d -> %s (detected %+d)\n", step, ok ? "ok" : "WRONG", s.lastScroll());
        if (!ok)
            ++failures;
        check(s.currentView().top() == kHeader + scroll - lo, "view position tracked");
        if (s.currentView().top() != kHeader + scroll - lo)
            std::printf("   view top %d height %d, expected top %d height %d, result %d\n", s.currentView().top(),
                        s.currentView().height(), kHeader + scroll - lo, kView, int(r));
    }
    check(sameIgnoringScrollbar(s.result(), expected(content, lo, hi)), "stitched image matches ideal");
    std::printf("   result height %d, expected %d\n", s.result().height(), expected(content, lo, hi).height());
}

} // namespace

int main()
{
    // 单步滚动量不超过可视高度（230）的 60%，即重叠至少 40%
    runCase("scroll down", 0, {37, 120, 5, 90, 135, 64});
    runCase("scroll up from middle", 1200, {-40, -135, -72, -130, -9});
    runCase("down, back up, then further up", 800, {100, 130, -120, -130, -100, -135, 60});
    // 一次滚过大半屏后跳回已截过的区域：需要在整个已拼接内容中重新定位
    runCase("jump back into captured area", 500, {150, 150, 150, -400, 60, 300});
    return failures == 0 ? 0 : 1;
}
