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
    return strokeHit(path, pos, tolerance);
}

bool EllipseShape::hitTest(const QPointF& pos, qreal tolerance) const
{
    QPainterPath path;
    path.addEllipse(rect());
    return strokeHit(path, pos, tolerance);
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
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect());
}

void EllipseShape::paint(QPainter& p, const RenderContext&) const
{
    p.setPen(QPen(style.color, style.width));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(rect());
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

    if (src != m_cacheRect) {
        const int block = qMax(4, int(style.width * 3 * dpr));
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
    p.setPen(style.color);
    p.drawText(QRectF(pos, QSizeF(1, 1)), kTextFlags, text);
}

QRectF TextShape::boundingRect() const
{
    return QFontMetricsF(fontFor(style)).boundingRect(QRectF(pos, QSizeF(1, 1)), kTextFlags, text);
}
