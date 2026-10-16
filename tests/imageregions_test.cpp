#include "capture/imageregions.h"

#include <QGuiApplication>
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
    expect("paragraph 2", e.elementAt({60, 104}, all), QRect(QPoint(47, 97), QPoint(143, 111)));
    expect("icon", e.elementAt({310, 60}, all), QRect(QPoint(299, 49), QPoint(332, 82)));
    expect("button label keeps button", e.elementAt({100, 215}, all), QRect(QPoint(50, 200), QPoint(170, 230)));
    expect("blank in panel", e.elementAt({400, 250}, all), QRect(QPoint(20, 20), QPoint(520, 320)));
    expect("outside bounds", e.elementAt({60, 54}, QRect(200, 200, 50, 50)), QRect());
    return failures ? 1 : 0;
}
