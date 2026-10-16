#include "overlaystates.h"

#include "overlaywindow.h"

#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr int kDragThreshold = 3;
constexpr int kHandleSize = 7;
const QColor kAccent(0, 174, 255);

QList<QPoint> handlePoints(const QRect& r)
{
    const int cx = r.left() + r.width() / 2;
    const int cy = r.top() + r.height() / 2;
    return {r.topLeft(), {cx, r.top()}, r.topRight(), {r.right(), cy},
            r.bottomRight(), {cx, r.bottom()}, r.bottomLeft(), {r.left(), cy}};
}

} // namespace

void paintSelectionFrame(QPainter& p, const QRect& r, bool handles)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(kAccent, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    if (handles) {
        p.setBrush(kAccent);
        p.setPen(QPen(Qt::white, 1));
        for (const QPoint& pt : handlePoints(r.adjusted(0, 0, -1, -1)))
            p.drawRect(QRect(0, 0, kHandleSize, kHandleSize).translated(pt - QPoint(kHandleSize / 2, kHandleSize / 2)));
    }
    p.restore();
}

// ---------------- IdleState ----------------

IdleState::IdleState(OverlayWindow* w)
    : OverlayState(w)
{
    w->setCursor(Qt::CrossCursor);
    w->setToolbarVisible(false);
}

void IdleState::mousePress(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        w->cancel();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_pressed = true;
        m_pressPos = e->position().toPoint();
    }
}

void IdleState::mouseMove(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    if (m_pressed && (pos - m_pressPos).manhattanLength() > kDragThreshold) {
        auto next = std::make_unique<SelectingState>(w, m_pressPos);
        next->mouseMove(e);
        w->setState(std::move(next));
        return;
    }
    m_hover = w->windowAt(pos);
    w->setHover(m_hover);
}

void IdleState::mouseRelease(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_pressed)
        return;
    m_pressed = false;
    // 单击：直接选中识别到的窗口
    if (!m_hover.isEmpty()) {
        w->setSelection(m_hover);
        w->setState(std::make_unique<AdjustingState>(w));
    }
}

void IdleState::paint(QPainter& p) const
{
    if (m_hover.isEmpty())
        return;
    p.save();
    p.setPen(QPen(kAccent, 2));
    p.drawRect(m_hover.adjusted(1, 1, -1, -1));
    p.restore();
}

// ---------------- SelectingState ----------------

SelectingState::SelectingState(OverlayWindow* w, const QPoint& anchor)
    : OverlayState(w)
    , m_anchor(anchor)
{
    w->setHover({});
}

void SelectingState::mousePress(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        w->clearSelection();
        w->setState(std::make_unique<IdleState>(w));
    }
}

void SelectingState::mouseMove(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    // 包含光标所在像素
    QRect r = QRect(m_anchor, pos).normalized();
    r.setSize(r.size() + QSize(1, 1));
    w->setSelection(r & w->rect());
}

void SelectingState::mouseRelease(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QRect r = w->selection();
    if (r.width() < 3 || r.height() < 3) {
        w->clearSelection();
        w->setState(std::make_unique<IdleState>(w));
        return;
    }
    w->setState(std::make_unique<AdjustingState>(w));
}

void SelectingState::paint(QPainter& p) const
{
    paintSelectionFrame(p, w->selection(), false);
}

// ---------------- AdjustingState ----------------

AdjustingState::AdjustingState(OverlayWindow* w)
    : OverlayState(w)
{
    w->setCursor(Qt::ArrowCursor);
    w->setToolbarVisible(true);
}

int AdjustingState::hitTest(const QPoint& pos) const
{
    const QRect sel = w->selection();
    const auto pts = handlePoints(sel.adjusted(0, 0, -1, -1));
    for (int i = 0; i < pts.size(); ++i) {
        if ((pos - pts[i]).manhattanLength() <= kHandleSize + 2)
            return i + 1;
    }
    return sel.contains(pos) ? 9 : 0;
}

void AdjustingState::updateCursor(int hit)
{
    static const Qt::CursorShape shapes[] = {
        Qt::ArrowCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
        Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor, Qt::SizeAllCursor};
    w->setCursor(shapes[hit]);
}

void AdjustingState::mousePress(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) { // 右键：放弃选区，回到窗口识别
        w->clearSelection();
        w->setState(std::make_unique<IdleState>(w));
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint pos = e->position().toPoint();
    m_pressPos = pos;
    m_origin = w->selection();
    m_drag = hitTest(pos);

    // 手柄优先；其次是选区内已画的标注：选中并拖动
    AnnotationDoc* doc = w->toolContext().doc;
    Shape* shape = (m_drag == 9) ? doc->shapeAt(e->position()) : nullptr;
    doc->setSelected(shape);
    if (shape) {
        m_drag = kDragShape;
        m_shapeMoved = QPointF();
        return;
    }
    if (m_drag)
        w->setToolbarVisible(false);
}

void AdjustingState::mouseMove(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    if (!m_drag) {
        const int hit = hitTest(pos);
        const bool overShape = hit == 9 && w->toolContext().doc->shapeAt(e->position());
        w->setCursor(overShape ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (!overShape)
            updateCursor(hit);
        return;
    }

    if (m_drag == kDragShape) {
        AnnotationDoc* doc = w->toolContext().doc;
        if (Shape* s = doc->selected()) {
            const QPointF total = QPointF(pos - m_pressPos);
            s->translate(total - m_shapeMoved);
            m_shapeMoved = total;
            w->update();
        }
        return;
    }

    const QPoint d = pos - m_pressPos;
    QRect r = m_origin;
    if (m_drag == 9) {
        r.translate(d);
        const QRect bounds = w->rect();
        r.moveLeft(qBound(bounds.left(), r.left(), bounds.right() - r.width() + 1));
        r.moveTop(qBound(bounds.top(), r.top(), bounds.bottom() - r.height() + 1));
    } else {
        int l = r.left(), t = r.top(), rt = r.right(), b = r.bottom();
        switch (m_drag) {
        case 1: l += d.x(); t += d.y(); break;
        case 2: t += d.y(); break;
        case 3: rt += d.x(); t += d.y(); break;
        case 4: rt += d.x(); break;
        case 5: rt += d.x(); b += d.y(); break;
        case 6: b += d.y(); break;
        case 7: l += d.x(); b += d.y(); break;
        case 8: l += d.x(); break;
        }
        r = QRect(QPoint(l, t), QPoint(rt, b)).normalized() & w->rect();
    }
    if (r.width() >= 1 && r.height() >= 1)
        w->setSelection(r);
}

void AdjustingState::mouseRelease(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_drag == kDragShape) {
        AnnotationDoc* doc = w->toolContext().doc;
        if (Shape* s = doc->selected())
            doc->commitMove(s, m_shapeMoved);
    }
    m_drag = 0;
    w->setToolbarVisible(true);
    updateCursor(hitTest(e->position().toPoint()));
}

void AdjustingState::mouseDoubleClick(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && w->selection().contains(e->position().toPoint()))
        w->finish(FinishAction::Copy);
}

void AdjustingState::paint(QPainter& p) const
{
    paintSelectionFrame(p, w->selection(), true);
    if (const Shape* s = w->toolContext().doc->selected()) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(kAccent, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(s->boundingRect().adjusted(-4, -4, 4, 4));
        p.restore();
    }
}

// ---------------- AnnotatingState ----------------

AnnotatingState::AnnotatingState(OverlayWindow* w)
    : OverlayState(w)
{
    w->setCursor(w->toolType() == ToolType::Text ? Qt::IBeamCursor : Qt::CrossCursor);
    w->setToolbarVisible(true);
}

void AnnotatingState::mousePress(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) { // 右键：退出当前工具
        w->selectTool(ToolType::None);
        return;
    }
    if (e->button() != Qt::LeftButton || !w->tool())
        return;
    m_drawing = true;
    w->toolContext().modifiers = e->modifiers();
    w->tool()->press(e->position(), w->toolContext());
    w->update();
}

void AnnotatingState::mouseMove(QMouseEvent* e)
{
    if (!m_drawing)
        return;
    w->toolContext().modifiers = e->modifiers();
    w->tool()->move(e->position(), w->toolContext());
    w->update();
}

void AnnotatingState::mouseRelease(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_drawing)
        return;
    m_drawing = false;
    w->toolContext().modifiers = e->modifiers();
    w->tool()->release(e->position(), w->toolContext());
    w->update();
}

void AnnotatingState::paint(QPainter& p) const
{
    paintSelectionFrame(p, w->selection(), false);
}
