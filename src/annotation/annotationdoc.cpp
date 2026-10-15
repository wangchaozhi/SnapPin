#include "annotationdoc.h"

#include <QPainter>
#include <algorithm>

class AddShapeCommand : public QUndoCommand {
public:
    AddShapeCommand(AnnotationDoc* doc, std::shared_ptr<Shape> shape)
        : m_doc(doc), m_shape(std::move(shape)) {}

    void redo() override { m_doc->insertAt(m_doc->m_shapes.size(), m_shape); }
    void undo() override { m_doc->erase(m_shape); }

private:
    AnnotationDoc* m_doc;
    std::shared_ptr<Shape> m_shape;
};

class RemoveShapeCommand : public QUndoCommand {
public:
    RemoveShapeCommand(AnnotationDoc* doc, std::shared_ptr<Shape> shape)
        : m_doc(doc), m_shape(std::move(shape)) {}

    void redo() override { m_index = m_doc->erase(m_shape); }
    void undo() override { m_doc->insertAt(m_index, m_shape); }

private:
    AnnotationDoc* m_doc;
    std::shared_ptr<Shape> m_shape;
    size_t m_index = 0;
};

class MoveShapeCommand : public QUndoCommand {
public:
    MoveShapeCommand(AnnotationDoc* doc, std::shared_ptr<Shape> shape, const QPointF& delta)
        : m_doc(doc), m_shape(std::move(shape)), m_delta(delta) {}

    void redo() override
    {
        if (m_applied) { // 拖动时已经移动过，首次入栈不重复移动
            m_applied = false;
            return;
        }
        m_shape->translate(m_delta);
        emit m_doc->changed();
    }
    void undo() override
    {
        m_shape->translate(-m_delta);
        emit m_doc->changed();
    }

private:
    AnnotationDoc* m_doc;
    std::shared_ptr<Shape> m_shape;
    QPointF m_delta;
    bool m_applied = true;
};

AnnotationDoc::AnnotationDoc(QObject* parent)
    : QObject(parent)
{
}

void AnnotationDoc::add(std::unique_ptr<Shape> shape)
{
    m_undo.push(new AddShapeCommand(this, std::move(shape)));
}

void AnnotationDoc::remove(Shape* shape)
{
    if (auto sp = find(shape))
        m_undo.push(new RemoveShapeCommand(this, sp));
}

void AnnotationDoc::commitMove(Shape* shape, const QPointF& delta)
{
    if (delta.isNull())
        return;
    if (auto sp = find(shape))
        m_undo.push(new MoveShapeCommand(this, sp, delta));
}

Shape* AnnotationDoc::shapeAt(const QPointF& pos) const
{
    for (auto it = m_shapes.rbegin(); it != m_shapes.rend(); ++it) {
        if ((*it)->hitTest(pos, 4))
            return it->get();
    }
    return nullptr;
}

void AnnotationDoc::setSelected(Shape* shape)
{
    if (m_selected == shape)
        return;
    m_selected = shape;
    emit changed();
}

std::shared_ptr<Shape> AnnotationDoc::find(Shape* shape) const
{
    for (const auto& sp : m_shapes) {
        if (sp.get() == shape)
            return sp;
    }
    return nullptr;
}

void AnnotationDoc::insertAt(size_t index, const std::shared_ptr<Shape>& shape)
{
    m_shapes.insert(m_shapes.begin() + std::min(index, m_shapes.size()), shape);
    emit changed();
}

size_t AnnotationDoc::erase(const std::shared_ptr<Shape>& shape)
{
    const auto it = std::find(m_shapes.begin(), m_shapes.end(), shape);
    const size_t index = size_t(it - m_shapes.begin());
    if (it != m_shapes.end())
        m_shapes.erase(it);
    if (m_selected == shape.get())
        m_selected = nullptr;
    emit changed();
    return index;
}

void AnnotationDoc::paint(QPainter& p, const RenderContext& ctx) const
{
    for (const auto& s : m_shapes) {
        p.save();
        s->paint(p, ctx);
        p.restore();
    }
}
