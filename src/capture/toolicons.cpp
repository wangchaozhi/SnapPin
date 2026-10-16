#include "toolicons.h"

#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyleHints>
#include <functional>

namespace {

// 所有图标画在 20×20 的网格里，统一笔画粗细与圆角端点
constexpr qreal kGrid = 20.0;
constexpr qreal kStroke = 1.6;

// 马卡龙配色（和应用图标同一套）：浅色用来填充 / 深色主题下的线条，深色用来描边
struct Hue {
    QColor light;
    QColor deep;
};
const Hue kRed{QColor(0xFF, 0xAD, 0xAD), QColor(0xE0, 0x6C, 0x6C)};
const Hue kPeach{QColor(0xFF, 0xD6, 0xA5), QColor(0xE0, 0x97, 0x4A)};
const Hue kYellow{QColor(0xFD, 0xFF, 0xB6), QColor(0xB8, 0xB3, 0x47)};
const Hue kMint{QColor(0xB9, 0xFB, 0xC0), QColor(0x3F, 0xAF, 0x5A)};
const Hue kCyan{QColor(0x9B, 0xF6, 0xFF), QColor(0x2B, 0xAF, 0xC0)};
const Hue kBlue{QColor(0xA0, 0xC4, 0xFF), QColor(0x5B, 0x8D, 0xEF)};
const Hue kLavender{QColor(0xBD, 0xB2, 0xFF), QColor(0x8A, 0x7B, 0xEA)};
const Hue kPink{QColor(0xFF, 0xC6, 0xFF), QColor(0xC7, 0x66, 0xC7)};
const Hue kGray{QColor(0xE4, 0xE4, 0xE7), QColor(0x6B, 0x6B, 0x70)};

const QColor kDisabledLight(0xbd, 0xbd, 0xbd);
const QColor kDisabledDark(0x5a, 0x5d, 0x63);

// 线条颜色 + 图形主体的填充（浅色主题下是浅色马卡龙，深色主题下不填充）
struct Ink {
    QColor line;
    QBrush body;
};
using Drawer = std::function<void(QPainter&, const Ink&)>;

class VectorIconEngine : public QIconEngine {
public:
    // fixedColors：自带颜色的图标（色块、复制按钮），不按主题上色，禁用时整体变淡
    VectorIconEngine(Drawer draw, Hue hue, bool fixedColors)
        : m_draw(std::move(draw)), m_hue(std::move(hue)), m_fixedColors(fixedColors) {}

    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        // 每次绘制时读系统主题，切换深浅色后工具栏重绘即可，不用重建图标
        const bool dark = toolbarDarkMode();
        Ink ink;
        if (mode == QIcon::Disabled) {
            ink.line = dark ? kDisabledDark : kDisabledLight;
        } else {
            ink.line = dark ? m_hue.light : m_hue.deep;
        }

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const qreal side = qMin(rect.width(), rect.height());
        p->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        p->scale(side / kGrid, side / kGrid);
        p->setPen(QPen(ink.line, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        if (m_fixedColors && mode == QIcon::Disabled)
            p->setOpacity(0.4);
        m_draw(*p, ink);
        p->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine* clone() const override { return new VectorIconEngine(*this); }

private:
    Drawer m_draw;
    Hue m_hue;
    bool m_fixedColors;
};

QIcon makeIcon(Drawer draw, Hue hue = kGray, bool fixedColors = false)
{
    return QIcon(new VectorIconEngine(std::move(draw), std::move(hue), fixedColors));
}

// 图形主体：浅色主题下用马卡龙浅色填充
template <typename F>
void body(QPainter& p, const Ink& ink, F draw)
{
    p.setBrush(ink.body);
    draw();
    p.setBrush(Qt::NoBrush);
}

QPainterPath polyline(std::initializer_list<QPointF> pts)
{
    QPainterPath path;
    bool first = true;
    for (const QPointF& pt : pts) {
        if (first)
            path.moveTo(pt);
        else
            path.lineTo(pt);
        first = false;
    }
    return path;
}

// 撤销箭头：向左的箭头 + 右侧半圆回弯；重做为其水平镜像
void drawUndo(QPainter& p)
{
    QPainterPath path;
    path.moveTo(6.0, 8.0);
    path.lineTo(12.0, 8.0);
    path.arcTo(QRectF(8.5, 8.0, 7.0, 7.0), 90, -180);
    path.lineTo(7.5, 15.0);
    p.drawPath(path);
    p.drawPath(polyline({{8.8, 5.2}, {6.0, 8.0}, {8.8, 10.8}}));
}

Drawer drawerFor(ToolIcon id)
{
    switch (id) {
    case ToolIcon::Rect:
        return [](QPainter& p, const Ink& ink) {
            body(p, ink, [&] { p.drawRoundedRect(QRectF(3.5, 5.0, 13.0, 10.0), 1.5, 1.5); });
        };

    case ToolIcon::Ellipse:
        return [](QPainter& p, const Ink& ink) { body(p, ink, [&] { p.drawEllipse(QPointF(10.0, 10.0), 7.0, 5.5); }); };

    case ToolIcon::Line:
        return [](QPainter& p, const Ink&) { p.drawLine(QPointF(4.5, 15.5), QPointF(15.5, 4.5)); };

    case ToolIcon::Number:
        return [](QPainter& p, const Ink& ink) {
            // 圆圈里的 1
            body(p, ink, [&] { p.drawEllipse(QPointF(10.0, 10.0), 7.0, 7.0); });
            p.drawPath(polyline({{8.4, 7.6}, {10.4, 6.2}, {10.4, 13.8}}));
        };

    case ToolIcon::Arrow:
        return [](QPainter& p, const Ink&) {
            p.drawLine(QPointF(4.5, 15.5), QPointF(15.0, 5.0));
            p.drawPath(polyline({{9.0, 5.0}, {15.0, 5.0}, {15.0, 11.0}}));
        };

    case ToolIcon::Pen:
        return [](QPainter& p, const Ink& ink) {
            // 斜放的铅笔：笔身 + 笔尖分界线
            body(p, ink, [&] {
                p.drawPath(polyline({{4.0, 16.0}, {4.8, 12.6}, {12.6, 4.8}, {15.2, 7.4}, {7.4, 15.2}, {4.0, 16.0}}));
            });
            p.drawLine(QPointF(11.0, 6.4), QPointF(13.6, 9.0));
        };

    case ToolIcon::Marker:
        return [](QPainter& p, const Ink& ink) {
            // 荧光笔：斜头笔身 + 下方一道半透明的粗笔迹
            body(p, ink, [&] { p.drawPath(polyline({{6.6, 12.6}, {12.4, 4.6}, {15.4, 6.8}, {9.6, 14.8}, {6.6, 12.6}})); });
            p.drawPath(polyline({{6.6, 12.6}, {5.6, 15.2}, {9.6, 14.8}}));
            QColor trail = ink.line;
            trail.setAlphaF(0.35);
            p.save();
            p.setPen(QPen(trail, 2.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(4.0, 17.6), QPointF(16.0, 17.6));
            p.restore();
        };

    case ToolIcon::Mosaic:
        return [](QPainter& p, const Ink& ink) {
            // 4×4 棋盘格：像素化的效果（3×3 隔格填充看起来像骰子）
            const QRectF box(4.0, 4.0, 12.0, 12.0);
            body(p, ink, [&] { p.drawRoundedRect(box, 1.2, 1.2); });
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(ink.line);
            const qreal cell = box.width() / 4;
            for (int r = 0; r < 4; ++r) {
                for (int col = 0; col < 4; ++col) {
                    if ((r + col) % 2 == 0)
                        p.drawRect(QRectF(box.x() + col * cell, box.y() + r * cell, cell, cell));
                }
            }
            p.restore();
        };

    case ToolIcon::Eraser:
        return [](QPainter& p, const Ink& ink) {
            // 斜放的橡皮：外框 + 擦头分界线 + 底部一条擦过的痕迹
            body(p, ink, [&] {
                p.drawPath(polyline({{8.2, 15.5}, {3.6, 10.9}, {10.6, 3.9}, {16.4, 9.7}, {10.6, 15.5}, {8.2, 15.5}}));
            });
            p.drawLine(QPointF(6.9, 7.6), QPointF(12.7, 13.4));
            p.drawLine(QPointF(10.6, 15.5), QPointF(16.5, 15.5));
        };

    case ToolIcon::Fill:
        return [](QPainter& p, const Ink& ink) {
            // 圆角方块，右下半边填充
            const QRectF box(4.0, 4.0, 12.0, 12.0);
            p.drawRoundedRect(box, 1.5, 1.5);
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(ink.line);
            QPainterPath half;
            half.addRoundedRect(box, 1.5, 1.5);
            QPainterPath tri;
            tri.addPolygon(QPolygonF{box.topRight(), box.bottomRight(), box.bottomLeft()});
            p.drawPath(half.intersected(tri));
            p.restore();
        };

    case ToolIcon::Text:
        return [](QPainter& p, const Ink&) {
            p.drawPath(polyline({{5.0, 6.5}, {5.0, 4.5}, {15.0, 4.5}, {15.0, 6.5}}));
            p.drawLine(QPointF(10.0, 4.5), QPointF(10.0, 15.5));
            p.drawLine(QPointF(7.8, 15.5), QPointF(12.2, 15.5));
        };

    case ToolIcon::Undo:
        return [](QPainter& p, const Ink&) { drawUndo(p); };

    case ToolIcon::Redo:
        return [](QPainter& p, const Ink&) {
            p.translate(kGrid, 0);
            p.scale(-1, 1);
            drawUndo(p);
        };

    case ToolIcon::LongCapture:
        return [](QPainter& p, const Ink& ink) {
            // 竖长的页面 + 上下双向箭头
            body(p, ink, [&] { p.drawRoundedRect(QRectF(5.5, 2.5, 9.0, 15.0), 1.5, 1.5); });
            p.drawLine(QPointF(10.0, 6.5), QPointF(10.0, 13.5));
            p.drawPath(polyline({{8.0, 8.3}, {10.0, 6.3}, {12.0, 8.3}}));
            p.drawPath(polyline({{8.0, 11.7}, {10.0, 13.7}, {12.0, 11.7}}));
        };

    case ToolIcon::Pin:
        return [](QPainter& p, const Ink& ink) {
            // 图钉：帽 + 收腰的钉身 + 针
            p.drawLine(QPointF(6.5, 3.5), QPointF(13.5, 3.5));
            body(p, ink, [&] {
                p.drawPath(polyline({{8.2, 3.5}, {8.2, 8.0}, {5.5, 11.0}, {14.5, 11.0}, {11.8, 8.0}, {11.8, 3.5}}));
            });
            p.drawLine(QPointF(10.0, 11.0), QPointF(10.0, 17.0));
        };

    case ToolIcon::Save:
        return [](QPainter& p, const Ink& ink) {
            // 软盘：切角外框 + 顶部标签 + 底部贴纸
            body(p, ink, [&] {
                p.drawPath(polyline({{4.5, 4.5}, {13.0, 4.5}, {15.5, 7.0}, {15.5, 15.5}, {4.5, 15.5}, {4.5, 4.5}}));
            });
            p.drawPath(polyline({{7.0, 4.5}, {7.0, 7.8}, {12.0, 7.8}, {12.0, 4.5}}));
            p.drawRoundedRect(QRectF(7.0, 11.0, 6.0, 4.5), 0.8, 0.8);
        };

    case ToolIcon::Copy:
        return [](QPainter& p, const Ink&) {
            // 两张叠放的纸：后面一张薰衣草紫，前面“贴”起来的一张薄荷绿（应用图标的配色）。
            // 填充色太浅，各自加一圈深一点的同色描边，深浅主题下都看得清
            const qreal w = p.pen().widthF();
            p.setPen(QPen(kLavender.deep, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(kLavender.light);
            p.drawRoundedRect(QRectF(3.5, 3.5, 9.0, 9.0), 1.8, 1.8);
            p.setPen(QPen(kMint.deep, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(kMint.light);
            p.drawRoundedRect(QRectF(7.5, 7.5, 9.0, 9.0), 1.8, 1.8);
        };
    }
    return [](QPainter&, const Ink&) {};
}

// 工具栏从左到右依次用彩虹马卡龙色（填充按钮用和复制按钮底纸一样的紫色）；撤销 / 重做用中性灰
Hue hueFor(ToolIcon id)
{
    switch (id) {
    case ToolIcon::Rect:
    case ToolIcon::Text:
        return kRed;
    case ToolIcon::Ellipse:
    case ToolIcon::Number:
        return kPeach;
    case ToolIcon::Line:
    case ToolIcon::LongCapture:
        return kYellow;
    case ToolIcon::Arrow:
    case ToolIcon::Pin:
        return kMint;
    case ToolIcon::Pen:
    case ToolIcon::Save:
        return kCyan;
    case ToolIcon::Marker:
        return kBlue;
    case ToolIcon::Mosaic:
    case ToolIcon::Fill:
        return kLavender;
    case ToolIcon::Eraser:
        return kPink;
    default:
        return kGray;
    }
}

} // namespace

bool toolbarDarkMode()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QIcon toolIcon(ToolIcon id)
{
    // 复制按钮自带两种颜色，不按主题上色
    return makeIcon(drawerFor(id), hueFor(id), id == ToolIcon::Copy);
}

QIcon colorIcon(const QColor& color)
{
    return makeIcon(
        [color](QPainter& p, const Ink&) {
            p.setPen(QPen(toolbarDarkMode() ? QColor(255, 255, 255, 90) : QColor(0, 0, 0, 60), 1.0));
            p.setBrush(color);
            p.drawEllipse(QPointF(10.0, 10.0), 6.5, 6.5);
        },
        kGray, true);
}
