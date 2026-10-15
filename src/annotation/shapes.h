#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <memory>

class QPainter;
class QPixmap;

struct Style {
    QColor color = QColor(255, 59, 48);
    qreal width = 4;
};

struct RenderContext {
    const QPixmap* source = nullptr; // 原始截图（马赛克取样用），已设置 devicePixelRatio
};

// 标注图形：坐标为截图所在屏幕的局部逻辑坐标
class Shape {
public:
    explicit Shape(const Style& s) : style(s) {}
    virtual ~Shape() = default;

    virtual void paint(QPainter& p, const RenderContext& ctx) const = 0;
    virtual QRectF boundingRect() const = 0;
    virtual std::unique_ptr<Shape> clone() const = 0; // 原型：复制、贴图再编辑
    virtual bool isValid() const;
    // 命中测试：默认按外接矩形；线条类图形只在笔画附近命中
    virtual bool hitTest(const QPointF& pos, qreal tolerance) const;
    virtual void translate(const QPointF& delta) = 0;

    Style style;

protected:
    bool strokeHit(const QPainterPath& path, const QPointF& pos, qreal tolerance) const;
};

// 由拖拽起止两点确定的图形
class DragShape : public Shape {
public:
    using Shape::Shape;
    QRectF rect() const { return QRectF(p1, p2).normalized(); }
    QRectF boundingRect() const override { return rect(); }
    void translate(const QPointF& d) override { p1 += d; p2 += d; }

    QPointF p1, p2;
};

class RectShape : public DragShape {
public:
    using DragShape::DragShape;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    bool hitTest(const QPointF& pos, qreal tolerance) const override;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<RectShape>(*this); }
};

class EllipseShape : public DragShape {
public:
    using DragShape::DragShape;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    bool hitTest(const QPointF& pos, qreal tolerance) const override;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<EllipseShape>(*this); }
};

class ArrowShape : public DragShape {
public:
    using DragShape::DragShape;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    bool hitTest(const QPointF& pos, qreal tolerance) const override;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<ArrowShape>(*this); }
};

class MosaicShape : public DragShape {
public:
    using DragShape::DragShape;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<MosaicShape>(*this); }

private:
    mutable QRect m_cacheRect; // 预览拖拽时避免每帧重新取样
    mutable QImage m_cache;
};

class PenShape : public Shape {
public:
    PenShape(const Style& s, bool marker) : Shape(s), marker(marker) {}
    void paint(QPainter& p, const RenderContext& ctx) const override;
    QRectF boundingRect() const override { return path.boundingRect(); }
    bool hitTest(const QPointF& pos, qreal tolerance) const override;
    void translate(const QPointF& d) override { path.translate(d); }
    std::unique_ptr<Shape> clone() const override { return std::make_unique<PenShape>(*this); }

    QPainterPath path;
    bool marker; // 荧光笔：半透明粗笔
};

class TextShape : public Shape {
public:
    using Shape::Shape;
    void paint(QPainter& p, const RenderContext& ctx) const override;
    QRectF boundingRect() const override;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<TextShape>(*this); }
    bool isValid() const override { return !text.isEmpty(); }
    void translate(const QPointF& d) override { pos += d; }

    static QFont fontFor(const Style& s);

    QPointF pos; // 文字左上角
    QString text;
};
