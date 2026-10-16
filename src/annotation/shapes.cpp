#include "shapes.h"

#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

namespace {
constexpr int kTextFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextDontClip;
}

bool Shape::isValid() const
{
    const QRectF r = boundingRect();
    return r.width() > 2 || r.height() > 2;
}

bool Shape::hitTest(const QPointF& pos, qreal tolerance) const
{
    return boundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(pos);
}

bool Shape::strokeHit(const QPainterPath& path, const QPointF& pos, qreal tolerance) const
{
    QPainterPathStroker stroker;
    stroker.setWidth(style.width + tolerance * 2);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path).contains(pos);
}

bool RectShape::hitTest(const QPointF& pos, qreal tolerance) const
{
    QPainterPath path;
    path.addRect(rect());
    return (style.fill && path.contains(pos)) || strokeHit(path, pos, tolerance);
}

bool EllipseShape::hitTest(const QPointF& pos, qreal tolerance) const
{
    QPainterPath path;
    path.addEllipse(rect());
    return (style.fill && path.contains(pos)) || strokeHit(path, pos, tolerance);
}

bool ArrowShape::hitTest(const QPointF& pos, qreal tolerance) const
{
    QPainterPath path(p1);
    path.lineTo(p2);
    return strokeHit(path, pos, tolerance + style.width); // 箭头头部较宽
}

bool PenShape::hitTest(const QPointF& pos, qreal tolerance) const
{
    return strokeHit(path, pos, marker ? tolerance + style.width * 1.5 : tolerance);
}

void RectShape::paint(QPainter& p, const RenderContext&) const
{
    p.setPen(QPen(style.color, style.width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(style.fill ? QBrush(style.color) : QBrush(Qt::NoBrush));
    p.drawRect(rect());
}

void EllipseShape::paint(QPainter& p, const RenderContext&) const
{
    p.setPen(QPen(style.color, style.width));
    p.setBrush(style.fill ? QBrush(style.color) : QBrush(Qt::NoBrush));
    p.drawEllipse(rect());
}

void EraserShape::paint(QPainter& p, const RenderContext& ctx) const
{
    if (!ctx.source)
        return;
    QPainterPathStroker stroker;
    stroker.setWidth(brush());
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    p.setClipPath(stroker.createStroke(path), Qt::IntersectClip);
    p.drawPixmap(0, 0, *ctx.source); // 截图与标注同在屏幕局部的逻辑坐标系
}

QRectF EraserShape::boundingRect() const
{
    const qreal r = brush() / 2;
    return path.boundingRect().adjusted(-r, -r, r, r);
}

bool EraserShape::hitTest(const QPointF& pos, qreal tolerance) const
{
    QPainterPathStroker stroker;
    stroker.setWidth(brush() + tolerance * 2);
    stroker.setCapStyle(Qt::RoundCap);
    return stroker.createStroke(path).contains(pos);
}

void ArrowShape::paint(QPainter& p, const RenderContext&) const
{
    const QLineF line(p1, p2);
    const qreal len = line.length();
    if (len < 1)
        return;

    const qreal head = qMin(len, qMax(12.0, style.width * 4));
    const QPointF dir = (p2 - p1) / len;
    const QPointF normal(-dir.y(), dir.x());
    const QPointF base = p2 - dir * head;

    if (len > head) {
        p.setPen(QPen(style.color, style.width, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(p1, base + dir); // 多画 1px 与箭头衔接
    }
    const QPolygonF tip{p2, base + normal * head * 0.5, base - normal * head * 0.5};
    p.setPen(Qt::NoPen);
    p.setBrush(style.color);
    p.drawPolygon(tip);
}

void LineShape::paint(QPainter& p, const RenderContext&) const
{
    p.setPen(QPen(style.color, style.width, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(p1, p2);
}

bool LineShape::hitTest(const QPointF& pos, qreal tolerance) const
{
    QPainterPath path(p1);
    path.lineTo(p2);
    return strokeHit(path, pos, tolerance);
}

bool NumberShape::hasTail() const
{
    return QLineF(center, tail).length() > radius() + 2;
}

void NumberShape::paint(QPainter& p, const RenderContext&) const
{
    const qreal r = radius();
    p.setPen(Qt::NoPen);
    p.setBrush(style.color);
    if (hasTail()) {
        // 尾巴：从圆的两侧切线收向尾端的三角形
        const QLineF axis(center, tail);
        const QPointF dir = (tail - center) / axis.length();
        const QPointF normal(-dir.y(), dir.x());
        const QPolygonF tri{center + normal * r * 0.6, tail, center - normal * r * 0.6};
        p.drawPolygon(tri);
    }
    p.drawEllipse(center, r, r);

    QFont f;
    f.setBold(true);
    f.setPixelSize(qRound(r * (number >= 10 ? 1.0 : 1.2)));
    p.setFont(f);
    // 浅色底用深色数字，保证可读
    p.setPen(style.color.lightnessF() > 0.7 ? QColor(30, 30, 30) : QColor(Qt::white));
    p.drawText(QRectF(center.x() - r, center.y() - r, 2 * r, 2 * r), Qt::AlignCenter, QString::number(number));
}

QRectF NumberShape::boundingRect() const
{
    const qreal r = radius();
    QRectF box(center.x() - r, center.y() - r, 2 * r, 2 * r);
    if (hasTail())
        box = box.united(QRectF(tail, QSizeF(1, 1)));
    return box;
}

bool NumberShape::hitTest(const QPointF& pos, qreal tolerance) const
{
    if (QLineF(center, pos).length() <= radius() + tolerance)
        return true;
    if (!hasTail())
        return false;
    QPainterPath path(center);
    path.lineTo(tail);
    return strokeHit(path, pos, tolerance + 2);
}

void MosaicShape::paint(QPainter& p, const RenderContext& ctx) const
{
    if (!ctx.source)
        return;
    const qreal dpr = ctx.source->devicePixelRatio();
    const QRectF r = rect();
    const QRect src = QRectF(r.x() * dpr, r.y() * dpr, r.width() * dpr, r.height() * dpr).toAlignedRect()
                      & QRect(QPoint(0, 0), ctx.source->size());
    if (src.isEmpty())
        return;

    const int block = qMax(4, int(style.width * 3 * dpr));
    if (src != m_cacheRect || block != m_cacheBlock) { // 粗细（块大小）改变时也要重新取样
        m_cacheBlock = block;
        const QImage img = ctx.source->copy(src).toImage();
        const QImage small = img.scaled(qMax(1, src.width() / block), qMax(1, src.height() / block),
                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_cache = small.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
        m_cacheRect = src;
    }
    p.drawImage(QRectF(src.x() / dpr, src.y() / dpr, src.width() / dpr, src.height() / dpr), m_cache);
}

void PenShape::paint(QPainter& p, const RenderContext&) const
{
    QColor c = style.color;
    qreal w = style.width;
    if (marker) {
        c.setAlpha(110);
        w *= 4;
    }
    p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

QFont TextShape::fontFor(const Style& s)
{
    QFont f;
    f.setPixelSize(qRound(12 + s.width * 3));
    return f;
}

void TextShape::paint(QPainter& p, const RenderContext&) const
{
    p.setFont(fontFor(style));
    if (style.fill) {
        // 底色模式：所选颜色做圆角底，文字用黑或白保证对比度
        p.setPen(Qt::NoPen);
        p.setBrush(style.color);
        const QRectF bg = boundingRect();
        p.drawRoundedRect(bg, bg.height() * 0.2, bg.height() * 0.2);
        p.setPen(style.color.lightnessF() > 0.6 ? QColor(20, 20, 20) : QColor(Qt::white));
    } else {
        p.setPen(style.color);
    }
    p.drawText(QRectF(pos, QSizeF(1, 1)), kTextFlags, text);
}

QRectF TextShape::boundingRect() const
{
    const QFont f = fontFor(style);
    const QRectF r = QFontMetricsF(f).boundingRect(QRectF(pos, QSizeF(1, 1)), kTextFlags, text);
    const qreal pad = style.fill ? f.pixelSize() * 0.3 : 0; // 底色留出内边距
    return r.adjusted(-pad, -pad * 0.5, pad, pad * 0.5);
}
