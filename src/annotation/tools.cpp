#include "tools.h"

#include "annotationdoc.h"

#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <functional>

namespace {

// 拖拽两点生成的图形：矩形、椭圆、箭头、马赛克
class DragTool : public ITool {
public:
    using Factory = std::function<std::unique_ptr<DragShape>(const Style&)>;
    explicit DragTool(Factory f) : m_factory(std::move(f)) {}

    void press(const QPointF& pos, ToolContext& ctx) override
    {
        m_shape = m_factory(ctx.style);
        m_shape->p1 = m_shape->p2 = pos;
    }
    void move(const QPointF& pos, ToolContext&) override
    {
        if (m_shape)
            m_shape->p2 = pos;
    }
    void release(const QPointF& pos, ToolContext& ctx) override
    {
        if (!m_shape)
            return;
        m_shape->p2 = pos;
        if (m_shape->isValid())
            ctx.doc->add(std::move(m_shape));
        m_shape.reset();
    }
    void paintPreview(QPainter& p, const RenderContext& rc) const override
    {
        if (m_shape)
            m_shape->paint(p, rc);
    }

private:
    Factory m_factory;
    std::unique_ptr<DragShape> m_shape;
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

private:
    bool m_marker;
    std::unique_ptr<PenShape> m_shape;
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
        edit->setFont(TextShape::fontFor(m_style));
        edit->setTextMargins(0, 0, 0, 0);
        edit->setStyleSheet(QStringLiteral("QLineEdit{background:rgba(255,255,255,40);color:%1;"
                                           "border:1px dashed %1;padding:0px;}")
                                .arg(m_style.color.name()));
        const QFontMetrics fm(edit->font());
        edit->setGeometry(QRect(pos.toPoint() - QPoint(2, 2), QSize(fm.averageCharWidth() * 8, fm.height() + 6)));
        QObject::connect(edit, &QLineEdit::textChanged, edit, [edit](const QString& t) {
            const QFontMetrics m(edit->font());
            edit->resize(qMax(m.averageCharWidth() * 8, m.horizontalAdvance(t) + m.averageCharWidth() * 2),
                         edit->height());
        });
        QObject::connect(edit, &QLineEdit::returnPressed, edit, [this] { if (m_ctx) commit(*m_ctx); });
        edit->installEventFilter(this);
        edit->show();
        edit->setFocus();
        m_editor = edit;
    }
    void move(const QPointF&, ToolContext&) override {}
    void release(const QPointF&, ToolContext&) override {}
    void finish(ToolContext& ctx) override { commit(ctx); }

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
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<RectShape>(s); });
    case ToolType::Ellipse:
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<EllipseShape>(s); });
    case ToolType::Arrow:
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<ArrowShape>(s); });
    case ToolType::Mosaic:
        return std::make_unique<DragTool>([](const Style& s) { return std::make_unique<MosaicShape>(s); });
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
