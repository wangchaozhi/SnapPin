#include "tools.h"

#include "annotationdoc.h"

#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <cmath>
#include <functional>

namespace {

// 按住 Shift 时的约束：框类图形变成正方形 / 圆，线类图形吸附到 45° 的整数倍
enum class Constraint { Box, Line };

QPointF constrained(const QPointF& from, const QPointF& to, Constraint c)
{
    const QPointF d = to - from;
    if (c == Constraint::Box) {
        const qreal s = qMax(qAbs(d.x()), qAbs(d.y()));
        return from + QPointF(d.x() < 0 ? -s : s, d.y() < 0 ? -s : s);
    }
    const qreal len = std::hypot(d.x(), d.y());
    const qreal step = 3.14159265358979323846 / 4; // MSVC 默认不提供 M_PI
    const qreal angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
    return from + QPointF(std::cos(angle), std::sin(angle)) * len;
}

// 拖拽两点生成的图形：矩形、椭圆、直线、箭头、马赛克
class DragTool : public ITool {
public:
    using Factory = std::function<std::unique_ptr<DragShape>(const Style&)>;
    DragTool(Factory f, Constraint c) : m_factory(std::move(f)), m_constraint(c) {}

    void press(const QPointF& pos, ToolContext& ctx) override
    {
        m_shape = m_factory(ctx.style);
        m_shape->p1 = m_shape->p2 = pos;
    }
    void move(const QPointF& pos, ToolContext& ctx) override
    {
        if (!m_shape)
            return;
        m_shape->p2 = endPoint(pos, ctx);
        m_shape->style = ctx.style; // 拖拽中滚轮调粗细，预览立即生效
    }
    void release(const QPointF& pos, ToolContext& ctx) override
    {
        if (!m_shape)
            return;
        m_shape->p2 = endPoint(pos, ctx);
        m_shape->style = ctx.style;
        if (m_shape->isValid())
            ctx.doc->add(std::move(m_shape));
        m_shape.reset();
    }
    void paintPreview(QPainter& p, const RenderContext& rc) const override
    {
        if (m_shape)
            m_shape->paint(p, rc);
    }
    void styleChanged(ToolContext& ctx) override
    {
        if (m_shape)
            m_shape->style = ctx.style;
    }

private:
    QPointF endPoint(const QPointF& pos, const ToolContext& ctx) const
    {
        return (ctx.modifiers & Qt::ShiftModifier) ? constrained(m_shape->p1, pos, m_constraint) : pos;
    }

    Factory m_factory;
    Constraint m_constraint;
    std::unique_ptr<DragShape> m_shape;
};

// 序号：单击放下一个自动递增的编号，按住拖动可拉出指向目标的尾巴
class NumberTool : public ITool {
public:
    void press(const QPointF& pos, ToolContext& ctx) override
    {
        m_shape = std::make_unique<NumberShape>(ctx.style);
        m_shape->center = m_shape->tail = pos;
        m_shape->number = ctx.doc->nextNumber();
    }
    void move(const QPointF& pos, ToolContext& ctx) override
    {
        if (!m_shape)
            return;
        m_shape->tail = pos;
        m_shape->style = ctx.style;
    }
    void release(const QPointF& pos, ToolContext& ctx) override
    {
        if (!m_shape)
            return;
        m_shape->tail = pos;
        m_shape->style = ctx.style;
        ctx.doc->add(std::move(m_shape));
        m_shape.reset();
    }
    void paintPreview(QPainter& p, const RenderContext& rc) const override
    {
        if (m_shape)
            m_shape->paint(p, rc);
    }
    void styleChanged(ToolContext& ctx) override
    {
        if (m_shape)
            m_shape->style = ctx.style;
    }

private:
    std::unique_ptr<NumberShape> m_shape;
};

class PenTool : public ITool {
public:
    explicit PenTool(bool marker) : m_marker(marker) {}

    void press(const QPointF& pos, ToolContext& ctx) override
    {
        m_shape = std::make_unique<PenShape>(ctx.style, m_marker);
        m_shape->path.moveTo(pos);
        m_shape->path.lineTo(pos + QPointF(0.01, 0)); // 单击也能留下一个点
    }
    void move(const QPointF& pos, ToolContext&) override
    {
        if (m_shape)
            m_shape->path.lineTo(pos);
    }
    void release(const QPointF&, ToolContext& ctx) override
    {
        if (m_shape)
            ctx.doc->add(std::move(m_shape));
        m_shape.reset();
    }
    void paintPreview(QPainter& p, const RenderContext& rc) const override
    {
        if (m_shape)
            m_shape->paint(p, rc);
    }
    void styleChanged(ToolContext& ctx) override
    {
        if (m_shape)
            m_shape->style = ctx.style;
    }

private:
    bool m_marker;
    std::unique_ptr<PenShape> m_shape;
};

// 橡皮擦：像画笔一样涂抹，涂过的地方露出原始截图
class EraserTool : public ITool {
public:
    void press(const QPointF& pos, ToolContext& ctx) override
    {
        m_shape = std::make_unique<EraserShape>(ctx.style);
        m_shape->path.moveTo(pos);
        m_shape->path.lineTo(pos + QPointF(0.01, 0));
    }
    void move(const QPointF& pos, ToolContext&) override
    {
        if (m_shape)
            m_shape->path.lineTo(pos);
    }
    void release(const QPointF&, ToolContext& ctx) override
    {
        if (m_shape)
            ctx.doc->add(std::move(m_shape));
        m_shape.reset();
    }
    void paintPreview(QPainter& p, const RenderContext& rc) const override
    {
        if (m_shape)
            m_shape->paint(p, rc);
    }
    void styleChanged(ToolContext& ctx) override
    {
        if (m_shape)
            m_shape->style = ctx.style;
    }

private:
    std::unique_ptr<EraserShape> m_shape;
};

// 文字：在宿主窗口上放一个无边框输入框，回车或失去焦点时提交
class TextTool : public ITool, public QObject {
public:
    ~TextTool() override { delete m_editor; }

    void press(const QPointF& pos, ToolContext& ctx) override
    {
        if (m_editor) { // 点在别处：先提交当前输入
            commit(ctx);
            return;
        }
        m_ctx = &ctx;
        m_style = ctx.style;

        auto* edit = new QLineEdit(ctx.host);
        edit->setFrame(false);
        edit->setTextMargins(0, 0, 0, 0);
        edit->move(pos.toPoint() - QPoint(2, 2));
        QObject::connect(edit, &QLineEdit::textChanged, edit, [this] { fitEditor(); });
        QObject::connect(edit, &QLineEdit::returnPressed, edit, [this] { if (m_ctx) commit(*m_ctx); });
        edit->installEventFilter(this);
        m_editor = edit;
        applyEditorStyle();
        edit->show();
        edit->setFocus();
    }
    void move(const QPointF&, ToolContext&) override {}
    void release(const QPointF&, ToolContext&) override {}
    void finish(ToolContext& ctx) override { commit(ctx); }
    void styleChanged(ToolContext& ctx) override
    {
        // 输入中的文字立即换颜色 / 字号
        m_style = ctx.style;
        if (m_editor)
            applyEditorStyle();
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override
    {
        if (obj == m_editor && e->type() == QEvent::KeyPress
            && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            discard(); // Esc 只取消当前输入，不退出截图
            return true;
        }
        return false;
    }

private:
    void applyEditorStyle()
    {
        m_editor->setFont(TextShape::fontFor(m_style));
        m_editor->setStyleSheet(QStringLiteral("QLineEdit{background:rgba(255,255,255,40);color:%1;"
                                               "border:1px dashed %1;padding:0px;}")
                                    .arg(m_style.color.name()));
        fitEditor();
    }
    void fitEditor()
    {
        const QFontMetrics m(m_editor->font());
        m_editor->resize(qMax(m.averageCharWidth() * 8, m.horizontalAdvance(m_editor->text()) + m.averageCharWidth() * 2),
                         m.height() + 6);
    }

    void commit(ToolContext& ctx)
    {
        if (!m_editor)
            return;
        QLineEdit* edit = m_editor;
        m_editor = nullptr; // 先置空，防止 returnPressed 与焦点变化重复提交
        auto shape = std::make_unique<TextShape>(m_style);
        const QFontMetrics fm(edit->font());
        shape->pos = QPointF(edit->x() + 3, edit->y() + (edit->height() - fm.height()) / 2.0);
        shape->text = edit->text();
        edit->hide();
        edit->deleteLater();
        if (shape->isValid())
            ctx.doc->add(std::move(shape));
        if (ctx.host)
            ctx.host->setFocus();
    }
    void discard()
    {
        if (!m_editor)
            return;
        QLineEdit* edit = m_editor;
        m_editor = nullptr;
        edit->hide();
        edit->deleteLater();
    }

    QPointer<QLineEdit> m_editor;
    ToolContext* m_ctx = nullptr;
    Style m_style;
};

} // namespace

std::unique_ptr<ITool> createTool(ToolType type)
{
    switch (type) {
    case ToolType::Rect:
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<RectShape>(s); }, Constraint::Box);
    case ToolType::Ellipse:
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<EllipseShape>(s); }, Constraint::Box);
    case ToolType::Line:
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<LineShape>(s); }, Constraint::Line);
    case ToolType::Arrow:
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<ArrowShape>(s); }, Constraint::Line);
    case ToolType::Mosaic:
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<MosaicShape>(s); }, Constraint::Box);
    case ToolType::Number:
        return std::make_unique<NumberTool>();
    case ToolType::Eraser:
        return std::make_unique<EraserTool>();
    case ToolType::Pen:
        return std::make_unique<PenTool>(false);
    case ToolType::Marker:
        return std::make_unique<PenTool>(true);
    case ToolType::Text:
        return std::make_unique<TextTool>();
    case ToolType::None:
        break;
    }
    return nullptr;
}
