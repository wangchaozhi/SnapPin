#include "toolicons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <functional>

namespace {

// 所有图标画在 20×20 的网格里，统一笔画粗细与圆角端点
constexpr qreal kGrid = 20.0;
constexpr qreal kStroke = 1.6;

const QColor kNormal(0x3a, 0x3a, 0x3a);
const QColor kDisabled(0xbd, 0xbd, 0xbd);
const QColor kAccent(0x16, 0x77, 0xff);

using Drawer = std::function<void(QPainter&, const QColor&)>;

class VectorIconEngine : public QIconEngine {
public:
    VectorIconEngine(Drawer draw, bool accent, bool fixedColors)
        : m_draw(std::move(draw)), m_accent(accent), m_fixedColors(fixedColors) {}

    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        QColor color = kNormal;
        if (mode == QIcon::Disabled)
            color = kDisabled;
        else if (m_accent || state == QIcon::On)
            color = kAccent;

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const qreal side = qMin(rect.width(), rect.height());
        p->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        p->scale(side / kGrid, side / kGrid);
        p->setPen(QPen(color, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        if (m_fixedColors && mode == QIcon::Disabled)
            p->setOpacity(0.4);
        m_draw(*p, color);
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
    bool m_accent;
    bool m_fixedColors; // 自带颜色的图标（色块）：禁用时整体变淡
};

QIcon makeIcon(Drawer draw, bool accent = false, bool fixedColors = false)
{
    return QIcon(new VectorIconEngine(std::move(draw), accent, fixedColors));
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
        return [](QPainter& p, const QColor&) { p.drawRoundedRect(QRectF(3.5, 5.0, 13.0, 10.0), 1.5, 1.5); };

    case ToolIcon::Ellipse:
        return [](QPainter& p, const QColor&) { p.drawEllipse(QPointF(10.0, 10.0), 7.0, 5.5); };

    case ToolIcon::Line:
        return [](QPainter& p, const QColor&) { p.drawLine(QPointF(4.5, 15.5), QPointF(15.5, 4.5)); };

    case ToolIcon::Number:
        return [](QPainter& p, const QColor&) {
            // 圆圈里的 1
            p.drawEllipse(QPointF(10.0, 10.0), 7.0, 7.0);
            p.drawPath(polyline({{8.4, 7.6}, {10.4, 6.2}, {10.4, 13.8}}));
        };

    case ToolIcon::Arrow:
        return [](QPainter& p, const QColor&) {
            p.drawLine(QPointF(4.5, 15.5), QPointF(15.0, 5.0));
            p.drawPath(polyline({{9.0, 5.0}, {15.0, 5.0}, {15.0, 11.0}}));
        };

    case ToolIcon::Pen:
        return [](QPainter& p, const QColor&) {
            // 斜放的铅笔：笔身 + 笔尖分界线
            p.drawPath(polyline({{4.0, 16.0}, {4.8, 12.6}, {12.6, 4.8}, {15.2, 7.4}, {7.4, 15.2}, {4.0, 16.0}}));
            p.drawLine(QPointF(11.0, 6.4), QPointF(13.6, 9.0));
        };

    case ToolIcon::Marker:
        return [](QPainter& p, const QColor& c) {
            // 荧光笔：斜头笔身 + 下方一道半透明的粗笔迹
            p.drawPath(polyline({{6.6, 12.6}, {12.4, 4.6}, {15.4, 6.8}, {9.6, 14.8}, {6.6, 12.6}}));
            p.drawPath(polyline({{6.6, 12.6}, {5.6, 15.2}, {9.6, 14.8}}));
            QColor ink = c;
            ink.setAlphaF(0.35);
            p.save();
            p.setPen(QPen(ink, 2.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(4.0, 17.6), QPointF(16.0, 17.6));
            p.restore();
        };

    case ToolIcon::Mosaic:
        return [](QPainter& p, const QColor& c) {
            // 4×4 棋盘格：像素化的效果（3×3 隔格填充看起来像骰子）
            const QRectF box(4.0, 4.0, 12.0, 12.0);
            p.drawRoundedRect(box, 1.2, 1.2);
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(c);
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
        return [](QPainter& p, const QColor&) {
            // 斜放的橡皮：外框 + 擦头分界线 + 底部一条擦过的痕迹
            p.drawPath(polyline({{8.2, 15.5}, {3.6, 10.9}, {10.6, 3.9}, {16.4, 9.7}, {10.6, 15.5}, {8.2, 15.5}}));
            p.drawLine(QPointF(6.9, 7.6), QPointF(12.7, 13.4));
            p.drawLine(QPointF(10.6, 15.5), QPointF(16.5, 15.5));
        };

    case ToolIcon::Fill:
        return [](QPainter& p, const QColor& c) {
            // 圆角方块，右下半边填充
            const QRectF box(4.0, 4.0, 12.0, 12.0);
            p.drawRoundedRect(box, 1.5, 1.5);
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            QPainterPath half;
            half.addRoundedRect(box, 1.5, 1.5);
            QPainterPath tri;
            tri.addPolygon(QPolygonF{box.topRight(), box.bottomRight(), box.bottomLeft()});
            p.drawPath(half.intersected(tri));
            p.restore();
        };

    case ToolIcon::Text:
        return [](QPainter& p, const QColor&) {
            p.drawPath(polyline({{5.0, 6.5}, {5.0, 4.5}, {15.0, 4.5}, {15.0, 6.5}}));
            p.drawLine(QPointF(10.0, 4.5), QPointF(10.0, 15.5));
            p.drawLine(QPointF(7.8, 15.5), QPointF(12.2, 15.5));
        };

    case ToolIcon::Undo:
        return [](QPainter& p, const QColor&) { drawUndo(p); };

    case ToolIcon::Redo:
        return [](QPainter& p, const QColor&) {
            p.translate(kGrid, 0);
            p.scale(-1, 1);
            drawUndo(p);
        };

    case ToolIcon::LongCapture:
        return [](QPainter& p, const QColor&) {
            // 竖长的页面 + 上下双向箭头
            p.drawRoundedRect(QRectF(5.5, 2.5, 9.0, 15.0), 1.5, 1.5);
            p.drawLine(QPointF(10.0, 6.5), QPointF(10.0, 13.5));
            p.drawPath(polyline({{8.0, 8.3}, {10.0, 6.3}, {12.0, 8.3}}));
            p.drawPath(polyline({{8.0, 11.7}, {10.0, 13.7}, {12.0, 11.7}}));
        };

    case ToolIcon::Pin:
        return [](QPainter& p, const QColor&) {
            // 图钉：帽 + 收腰的钉身 + 针
            p.drawLine(QPointF(6.5, 3.5), QPointF(13.5, 3.5));
            p.drawPath(polyline({{8.2, 3.5}, {8.2, 8.0}, {5.5, 11.0}, {14.5, 11.0}, {11.8, 8.0}, {11.8, 3.5}}));
            p.drawLine(QPointF(10.0, 11.0), QPointF(10.0, 17.0));
        };

    case ToolIcon::Save:
        return [](QPainter& p, const QColor&) {
            // 软盘：切角外框 + 顶部标签 + 底部贴纸
            p.drawPath(polyline({{4.5, 4.5}, {13.0, 4.5}, {15.5, 7.0}, {15.5, 15.5}, {4.5, 15.5}, {4.5, 4.5}}));
            p.drawPath(polyline({{7.0, 4.5}, {7.0, 7.8}, {12.0, 7.8}, {12.0, 4.5}}));
            p.drawRoundedRect(QRectF(7.0, 11.0, 6.0, 4.5), 0.8, 0.8);
        };

    case ToolIcon::Cancel:
        return [](QPainter& p, const QColor&) {
            p.drawLine(QPointF(5.5, 5.5), QPointF(14.5, 14.5));
            p.drawLine(QPointF(14.5, 5.5), QPointF(5.5, 14.5));
        };

    case ToolIcon::Done:
        return [](QPainter& p, const QColor&) {
            p.save();
            p.setPen(QPen(p.pen().color(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(polyline({{4.5, 10.5}, {8.5, 14.5}, {15.5, 5.5}}));
            p.restore();
        };
    }
    return [](QPainter&, const QColor&) {};
}

} // namespace

QIcon toolIcon(ToolIcon id, bool accent)
{
    return makeIcon(drawerFor(id), accent);
}

QIcon widthIcon(qreal width)
{
    // 实际粗细 1~30 映射到图标里 1~6 的线宽
    const qreal w = qBound(1.0, 1.0 + (width - 1) * 0.2, 6.0);
    return makeIcon([w](QPainter& p, const QColor& c) {
        p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4.0, 10.0), QPointF(16.0, 10.0));
    });
}

QIcon colorIcon(const QColor& color)
{
    return makeIcon(
        [color](QPainter& p, const QColor&) {
            p.setPen(QPen(QColor(0, 0, 0, 60), 1.0));
            p.setBrush(color);
            p.drawEllipse(QPointF(10.0, 10.0), 6.5, 6.5);
        },
        false, true);
}
