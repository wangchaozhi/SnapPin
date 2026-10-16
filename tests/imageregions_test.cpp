#include "capture/imageregions.h"

#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <cstdio>

static int failures = 0;

static void expect(const char* name, const QRect& got, const QRect& want)
{
    if (got != want) {
        std::printf("FAIL %s: got (%d,%d %dx%d) want (%d,%d %dx%d)\n", name, got.x(), got.y(), got.width(),
                    got.height(), want.x(), want.y(), want.width(), want.height());
        ++failures;
    } else {
        std::printf("ok   %s\n", name);
    }
}

// 结果只要求大致对（比如允许边框差一两个像素）时用
static void check(const char* name, bool ok, const QRect& got)
{
    if (ok) {
        std::printf("ok   %s\n", name);
    } else {
        std::printf("FAIL %s: got (%d,%d %dx%d)\n", name, got.x(), got.y(), got.width(), got.height());
        ++failures;
    }
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QImage img(400, 300, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(QColor(0xE0, 0xE0, 0xE0));
    p.drawRect(20, 20, 300, 200);          // 面板：外框 20..320
    p.fillRect(40, 40, 100, 30, QColor(0, 120, 215)); // 实心按钮 40..139 x 40..69
    p.setPen(QColor(0x80, 0x80, 0x80));
    p.drawRect(40, 100, 200, 24);          // 输入框 40..240 x 100..124
    p.setPen(Qt::black);
    p.drawLine(60, 160, 70, 160);          // 短下划线，不应被当成边
    p.end();

    ImageRegionDetector d(img);
    expect("button", d.regionAt({80, 55}), QRect(QPoint(39, 39), QPoint(140, 70)));
    expect("input", d.regionAt({100, 112}), QRect(QPoint(40, 100), QPoint(240, 124)));
    expect("panel", d.regionAt({65, 170}), QRect(QPoint(20, 20), QPoint(320, 220)));
    expect("outside", d.regionAt({380, 280}), QRect());

    // 没有边框的内容：段落（几行“字”）、图标；带边框的按钮里只有一个标签
    QImage page(600, 400, QImage::Format_RGB32);
    page.fill(Qt::white);
    QPainter q(&page);
    q.setPen(QColor(0xE0, 0xE0, 0xE0));
    q.drawRect(20, 20, 500, 300); // 面板 20..520 x 20..320
    for (const int y : {50, 64})  // 段落 1：两行，每行三个“词”，字间距、行距都小于 blockGap
        for (const int x : {50, 86, 126})
            q.fillRect(x, y, x == 126 ? 35 : 31, 9, Qt::black); // 50..160 x 50..72
    q.fillRect(50, 100, 91, 9, Qt::black);                     // 段落 2：50..140 x 100..108
    q.fillRect(300, 50, 32, 32, QColor(220, 60, 60));          // 图标 300..331 x 50..81
    q.setPen(QColor(0x80, 0x80, 0x80));
    q.drawRect(50, 200, 120, 30);                              // 按钮 50..170 x 200..230
    q.fillRect(90, 211, 41, 9, Qt::black);                     // 按钮文字
    q.end();

    ImageRegionDetector e(page);
    const QRect all = page.rect();
    expect("paragraph", e.elementAt({60, 54}, all), QRect(QPoint(47, 47), QPoint(163, 75)));
    expect("paragraph line gap", e.elementAt({60, 61}, all), QRect(QPoint(47, 47), QPoint(163, 75)));
    expect("paragraph 2", e.elementAt({60, 104}, all), QRect(QPoint(47, 98), QPoint(143, 111)));
    expect("icon", e.elementAt({310, 60}, all), QRect(QPoint(299, 49), QPoint(332, 82)));
    expect("button label keeps button", e.elementAt({100, 215}, all), QRect(QPoint(50, 200), QPoint(170, 230)));
    expect("blank in panel", e.elementAt({400, 250}, all), QRect(QPoint(20, 20), QPoint(520, 320)));
    expect("outside bounds", e.elementAt({60, 54}, QRect(200, 200, 50, 50)), QRect());

    // 浏览器顶栏：标签栏和工具栏之间是一条贯穿全宽的分界线，工具栏里有胶囊形地址栏和一排小图标
    QImage bar(800, 200, QImage::Format_RGB32);
    bar.fill(Qt::white);
    QPainter b(&bar);
    b.fillRect(0, 0, 800, 50, QColor(0xD3, 0xE3, 0xFD)); // 标签栏底色，下边界是全宽的分界线
    b.setRenderHint(QPainter::Antialiasing);
    b.setPen(QPen(QColor(0x1A, 0x73, 0xE8), 2));
    b.drawRoundedRect(QRectF(100, 60, 400, 40), 20, 20);  // 胶囊地址栏 99..501 x 59..101
    b.setRenderHint(QPainter::Antialiasing, false);
    for (const int x : {560, 600, 640})
        b.fillRect(x, 70, 20, 20, QColor(0x5F, 0x63, 0x68)); // 图标
    b.end();

    ImageRegionDetector f(bar);
    const QRect pill = f.elementAt({300, 80}, bar.rect());
    expect("pill height", QRect(0, pill.y(), 1, pill.height()), QRect(0, 58, 1, 44));
    if (qAbs(pill.left() - 99) > 2 || qAbs(pill.right() - 501) > 2) {
        std::printf("FAIL pill sides: %d..%d\n", pill.left(), pill.right());
        ++failures;
    }
    expect("toolbar icon", f.elementAt({610, 80}, bar.rect()), QRect(QPoint(599, 69), QPoint(620, 90)));

    // 标签栏：浅蓝底上一个白色的当前标签，下边和白色工具栏连在一起（没有边线）；标签里有图标、标题、关闭按钮
    QImage tabs(800, 120, QImage::Format_RGB32);
    tabs.fill(Qt::white);
    QPainter t(&tabs);
    t.fillRect(0, 0, 800, 50, QColor(0xD3, 0xE3, 0xFD));
    t.fillRect(50, 6, 290, 44, Qt::white);                  // 当前标签 50..339 x 6..49
    t.fillRect(62, 20, 16, 16, QColor(0x5F, 0x63, 0x68));    // 图标
    t.fillRect(86, 23, 60, 10, Qt::black);                   // 标题
    t.fillRect(316, 22, 12, 12, QColor(0x5F, 0x63, 0x68));   // 关闭按钮
    t.fillRect(400, 23, 60, 10, Qt::black);                  // 另一个（未选中）标签的标题
    t.end();
    ImageRegionDetector g(tabs);
    const QRect tab = g.elementAt({200, 28}, tabs.rect());
    check("tab", qAbs(tab.left() - 50) <= 2 && qAbs(tab.right() - 339) <= 2 && tab.top() <= 8 && tab.bottom() >= 44
                     && tab.bottom() <= 52, tab);
    const QRect title = g.elementAt({100, 28}, tabs.rect());
    check("tab title", title.contains(QRect(86, 23, 60, 10)) && title.right() < 300, title);

    // 桌面：上下渐变的壁纸，底部深色任务栏，里面一排图标
    QImage desk(1000, 600, QImage::Format_RGB32);
    QPainter k(&desk);
    QLinearGradient grad(0, 0, 0, 552);
    grad.setColorAt(0, QColor(40, 90, 160));
    grad.setColorAt(1, QColor(120, 170, 220));
    k.fillRect(0, 0, 1000, 552, grad);
    k.fillRect(0, 552, 1000, 48, QColor(32, 32, 32));        // 任务栏 0..999 x 552..599
    for (const int x : {400, 450, 500, 550})
        k.fillRect(x, 564, 24, 24, QColor(0xE8, 0xA0, 0x30)); // 任务栏图标
    k.end();
    ImageRegionDetector w(desk);
    const QRect bar0 = w.elementAt({200, 575}, desk.rect());
    check("taskbar", bar0.top() >= 548 && bar0.top() <= 553 && bar0.bottom() >= 598 && bar0.width() >= 990, bar0);
    expect("taskbar icon", w.elementAt({460, 575}, desk.rect()), QRect(QPoint(449, 563), QPoint(474, 588)));
    // 渐变壁纸不会被切成一条条，空白处框的是任务栏以上的整块桌面
    const QRect wall = w.elementAt({500, 300}, desk.rect());
    check("wallpaper not cut into bands", wall.top() <= 2 && wall.bottom() >= 548 && wall.bottom() <= 553 && wall.width() >= 990, wall);

    // 桌面图标：一列图标，每个下面隔 14 像素是居中的两行文字，上一个的文字到下一个图标隔 30 像素
    QImage icons(600, 500, QImage::Format_RGB32);
    QPainter n(&icons);
    QLinearGradient sky(0, 0, 0, 500);
    sky.setColorAt(0, QColor(30, 80, 150));
    sky.setColorAt(1, QColor(90, 150, 210));
    n.fillRect(icons.rect(), sky);
    for (const int top : {20, 160, 300}) {
        n.fillRect(50, top, 60, 60, QColor(0xF0, 0xC0, 0x40));     // 图标 50..109
        n.fillRect(40, top + 74, 80, 12, Qt::white);                // 第一行文字 40..119
        n.fillRect(55, top + 89, 50, 12, Qt::white);                // 第二行文字 55..104
    }
    n.end();
    ImageRegionDetector m(icons);
    for (const int top : {20, 160, 300}) {
        const QRect item(QPoint(40, top), QPoint(119, top + 100));
        const QRect onIcon = m.elementAt({80, top + 30}, icons.rect());
        const QRect onLabel = m.elementAt({80, top + 80}, icons.rect());
        check("desktop icon includes label", onIcon.contains(item) && onIcon.height() < 115, onIcon);
        check("desktop label includes icon", onLabel.contains(item) && onLabel.height() < 115, onLabel);
    }
    // 左对齐的两段文字（不是图标和说明）不合并
    QImage list(400, 200, QImage::Format_RGB32);
    list.fill(Qt::white);
    QPainter l(&list);
    l.fillRect(20, 20, 200, 10, Qt::black);
    l.fillRect(20, 50, 120, 10, Qt::black);
    l.end();
    ImageRegionDetector lt(list);
    const QRect line = lt.elementAt({60, 25}, list.rect());
    check("left-aligned lines not merged", line.bottom() < 40, line);
    return failures ? 1 : 0;
}
