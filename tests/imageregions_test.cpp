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
    return failures ? 1 : 0;
}
