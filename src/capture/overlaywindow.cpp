#include "overlaywindow.h"

#include "editortoolbar.h"
#include "overlaystates.h"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMap>
#include <QPainter>
#include <QScreen>

namespace {
constexpr int kMagnifierCells = 17; // 放大区域：17×17 物理像素
constexpr int kMagnifierCell = 8;   // 每个像素放大为 8×8
} // namespace

OverlayWindow::OverlayWindow(QScreen* screen, const QPixmap& shot, QList<DetectedWindow> windows)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_screen(screen)
    , m_shot(shot)
    , m_shotImage(shot.toImage())
    , m_windows(std::move(windows))
{
    setAttribute(Qt::WA_OpaquePaintEvent); // 每次都整屏重绘，跳过背景擦除
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setScreen(screen);
    setGeometry(screen->geometry());
    setupFloatingWindow(this, FloatingLayer::Overlay);

    m_renderCtx.source = &m_shot;
    m_toolCtx.doc = &m_doc;
    m_toolCtx.host = this;

    m_toolbar = new EditorToolbar(m_doc.undoStack(), this);
    m_toolbar->hide();
    m_toolCtx.style = m_toolbar->style();
    connect(m_toolbar, &EditorToolbar::toolClicked, this, [this](ToolType t) {
        selectTool(t == m_toolType ? ToolType::None : t);
    });
    connect(m_toolbar, &EditorToolbar::styleChanged, this, [this](const Style& s) { m_toolCtx.style = s; });
    connect(m_toolbar, &EditorToolbar::pinRequested, this, [this] { finish(FinishAction::Pin); });
    connect(m_toolbar, &EditorToolbar::saveRequested, this, [this] { finish(FinishAction::Save); });
    connect(m_toolbar, &EditorToolbar::longCaptureRequested, this, [this] { finish(FinishAction::LongCapture); });
    connect(m_toolbar, &EditorToolbar::copyRequested, this, [this] { finish(FinishAction::Copy); });
    connect(m_toolbar, &EditorToolbar::cancelRequested, this, &OverlayWindow::cancel);
    connect(&m_doc, &AnnotationDoc::changed, this, qOverload<>(&QWidget::update));

    setState(std::make_unique<IdleState>(this));
}

OverlayWindow::~OverlayWindow() = default;

void OverlayWindow::setState(std::unique_ptr<OverlayState> state)
{
    m_retiredState = std::move(m_state);
    m_state = std::move(state);
    update();
}

QRect OverlayWindow::windowAt(const QPoint& pos) const
{
    // 找到最上层包含该点的顶层窗口，再在它的子控件里找面积最小的
    int group = -1;
    QRect best;
    for (const DetectedWindow& w : m_windows) {
        if (group < 0) {
            if (!w.isChild && w.rect.contains(pos)) {
                group = w.group;
                best = w.rect;
            }
        } else if (w.group != group) {
            break;
        } else if (w.rect.contains(pos) && qint64(w.rect.width()) * w.rect.height() < qint64(best.width()) * best.height()) {
            best = w.rect;
        }
    }
    return group < 0 ? rect() : (best & rect());
}

void OverlayWindow::setHover(const QRect& r)
{
    if (r != m_hover) {
        m_hover = r;
        update();
    }
}

void OverlayWindow::setSelection(const QRect& r)
{
    m_selection = r;
    updateToolbarGeometry();
    update();
}

void OverlayWindow::presetSelection(const QRect& r)
{
    setSelection(r & rect());
    setState(std::make_unique<AdjustingState>(this));
}

void OverlayWindow::clearSelection()
{
    selectTool(ToolType::None);
    m_selection = QRect();
    // 图形都经由撤销栈加入，全部撤销即可清空，再丢弃历史
    QUndoStack* undo = m_doc.undoStack();
    while (undo->canUndo())
        undo->undo();
    undo->clear();
    update();
}

void OverlayWindow::setToolbarVisible(bool visible)
{
    if (visible && hasSelection()) {
        updateToolbarGeometry();
        m_toolbar->show();
        m_toolbar->raise();
    } else {
        m_toolbar->hide();
    }
}

void OverlayWindow::updateToolbarGeometry()
{
    if (!hasSelection())
        return;
    const QSize s = m_toolbar->sizeHint();
    const int gap = 6;
    int x = qBound(0, m_selection.right() + 1 - s.width(), qMax(0, width() - s.width()));
    int y = m_selection.bottom() + 1 + gap;
    if (y + s.height() > height())
        y = m_selection.top() - s.height() - gap; // 下方放不下放上方
    if (y < 0)
        y = m_selection.bottom() - s.height() - gap; // 都放不下放选区内
    m_toolbar->setGeometry(QRect(QPoint(x, qMax(0, y)), s));
}

void OverlayWindow::selectTool(ToolType type)
{
    if (m_tool)
        m_tool->finish(m_toolCtx);
    m_doc.setSelected(nullptr);
    m_toolType = type;
    m_tool = createTool(type);
    m_toolbar->setCurrentTool(type);
    if (!hasSelection())
        return;
    if (type == ToolType::None)
        setState(std::make_unique<AdjustingState>(this));
    else
        setState(std::make_unique<AnnotatingState>(this));
}

QImage OverlayWindow::renderResult() const
{
    const qreal dpr = m_shot.devicePixelRatio();
    const QRect phys = QRectF(m_selection.x() * dpr, m_selection.y() * dpr,
                              m_selection.width() * dpr, m_selection.height() * dpr).toRect()
                       & QRect(QPoint(0, 0), m_shot.size());
    QPixmap out = m_shot.copy(phys);
    out.setDevicePixelRatio(dpr);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(-phys.x() / dpr, -phys.y() / dpr);
        m_doc.paint(p, m_renderCtx);
    }
    return out.toImage();
}

void OverlayWindow::finish(FinishAction action)
{
    if (!hasSelection())
        return;
    if (m_tool)
        m_tool->finish(m_toolCtx);
    const QRect global(m_screen->geometry().topLeft() + m_selection.topLeft(), m_selection.size());
    emit finished(renderResult(), global, action);
}

void OverlayWindow::cancel()
{
    emit cancelled();
}

QPoint OverlayWindow::physicalCursor() const
{
    return (QPointF(m_cursor) * m_shot.devicePixelRatio()).toPoint();
}

QColor OverlayWindow::colorAtCursor() const
{
    const QPoint p = physicalCursor();
    return m_shotImage.valid(p) ? m_shotImage.pixelColor(p) : QColor();
}

// ---------------- 绘制 ----------------

void OverlayWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.drawPixmap(0, 0, m_shot);

    const QRect highlight = hasSelection() ? m_selection : m_hover;
    p.save();
    p.setClipRegion(QRegion(rect()).subtracted(QRegion(highlight)));
    p.fillRect(rect(), QColor(0, 0, 0, 110));
    p.restore();

    if (hasSelection()) {
        p.save();
        p.setClipRect(m_selection);
        p.setRenderHint(QPainter::Antialiasing);
        m_doc.paint(p, m_renderCtx);
        if (m_tool)
            m_tool->paintPreview(p, m_renderCtx);
        p.restore();
        paintSizeLabel(p);
    }

    m_state->paint(p);

    if (m_state->showMagnifier() && m_cursorInside)
        paintMagnifier(p);
}

void OverlayWindow::paintSizeLabel(QPainter& p) const
{
    const qreal dpr = m_shot.devicePixelRatio();
    const QString text = QStringLiteral("%1 × %2").arg(qRound(m_selection.width() * dpr)).arg(qRound(m_selection.height() * dpr));
    const QFontMetrics fm(font());
    QRect box(0, 0, fm.horizontalAdvance(text) + 12, fm.height() + 6);
    box.moveBottomLeft(m_selection.topLeft() - QPoint(0, 4));
    if (box.top() < 0)
        box.moveTopLeft(m_selection.topLeft() + QPoint(4, 4));
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, text);
    p.restore();
}

void OverlayWindow::paintMagnifier(QPainter& p) const
{
    const QPoint phys = physicalCursor();
    const int half = kMagnifierCells / 2;
    const QImage patch = m_shotImage.copy(phys.x() - half, phys.y() - half, kMagnifierCells, kMagnifierCells);
    const int zoom = kMagnifierCells * kMagnifierCell;

    const QFontMetrics fm(font());
    const int infoH = fm.height() * 2 + 10;
    QRect box(0, 0, zoom, zoom + infoH);
    QPoint at = m_cursor + QPoint(20, 20);
    if (at.x() + box.width() > width())
        at.setX(m_cursor.x() - 20 - box.width());
    if (at.y() + box.height() > height())
        at.setY(m_cursor.y() - 20 - box.height());
    box.moveTopLeft(at);

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const QRect zoomRect(box.topLeft(), QSize(zoom, zoom));
    p.drawImage(zoomRect, patch);

    // 十字准线
    const QColor cross(0, 174, 255, 110);
    const int c = half * kMagnifierCell;
    p.fillRect(QRect(zoomRect.left(), zoomRect.top() + c, zoom, kMagnifierCell), cross);
    p.fillRect(QRect(zoomRect.left() + c, zoomRect.top(), kMagnifierCell, zoom), cross);
    p.setPen(QPen(Qt::white, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRect(zoomRect.left() + c, zoomRect.top() + c, kMagnifierCell, kMagnifierCell));

    // 坐标与颜色
    const QRect info(box.left(), zoomRect.bottom() + 1, zoom, infoH);
    p.fillRect(info, QColor(0, 0, 0, 200));
    const QColor color = colorAtCursor();
    p.fillRect(QRect(info.left() + 6, info.top() + 6, 12, 12), color);
    p.setPen(Qt::white);
    p.drawText(info.adjusted(24, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("(%1, %2)\nRGB(%3, %4, %5)")
                   .arg(phys.x()).arg(phys.y())
                   .arg(color.red()).arg(color.green()).arg(color.blue()));

    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.drawRect(box.adjusted(0, 0, -1, -1));
    p.restore();
}

// ---------------- 输入事件：转发给当前状态 ----------------

void OverlayWindow::mousePressEvent(QMouseEvent* e)
{
    m_retiredState.reset();
    m_state->mousePress(e);
}

void OverlayWindow::mouseMoveEvent(QMouseEvent* e)
{
    m_retiredState.reset();
    m_cursor = e->position().toPoint();
    m_cursorInside = true;
    m_state->mouseMove(e);
    if (m_state->showMagnifier())
        update();
}

void OverlayWindow::mouseReleaseEvent(QMouseEvent* e)
{
    m_retiredState.reset();
    m_state->mouseRelease(e);
}

void OverlayWindow::mouseDoubleClickEvent(QMouseEvent* e)
{
    m_retiredState.reset();
    m_state->mouseDoubleClick(e);
}

void OverlayWindow::addElements(const QList<DetectedWindow>& elements)
{
    // 插到所属顶层窗口条目的末尾，保持同一 group 连续（windowAt 依赖这一点）
    QMap<int, QList<DetectedWindow>> byGroup;
    for (const DetectedWindow& e : elements)
        byGroup[e.group].append(e);

    QList<DetectedWindow> merged;
    merged.reserve(m_windows.size() + elements.size());
    for (int i = 0; i < m_windows.size(); ++i) {
        merged.append(m_windows[i]);
        const int g = m_windows[i].group;
        const bool lastOfGroup = i + 1 == m_windows.size() || m_windows[i + 1].group != g;
        if (lastOfGroup)
            merged.append(byGroup.take(g)); // 顶层窗口不在本屏上的结果自然被丢弃
    }
    m_windows = std::move(merged);
    if (!hasSelection())
        refreshHover();
}

void OverlayWindow::showEvent(QShowEvent*)
{
    refreshHover();
}

void OverlayWindow::refreshHover()
{
    // 用当前光标位置刷新窗口识别与放大镜，不必等下一次鼠标移动
    const QPoint local = mapFromGlobal(QCursor::pos());
    if (!rect().contains(local))
        return;
    QMouseEvent move(QEvent::MouseMove, QPointF(local), QPointF(QCursor::pos()), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    mouseMoveEvent(&move);
}

void OverlayWindow::leaveEvent(QEvent*)
{
    m_cursorInside = false;
    setHover({});
    update();
}

void OverlayWindow::keyPressEvent(QKeyEvent* e)
{
    m_retiredState.reset();

    if (e->key() == Qt::Key_Escape) {
        cancel();
        return;
    }
    if (e->matches(QKeySequence::Undo)) {
        m_doc.undoStack()->undo();
        return;
    }
    if (e->matches(QKeySequence::Redo) || (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_Y)) {
        m_doc.undoStack()->redo();
        return;
    }

    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && m_doc.selected()) {
        m_doc.remove(m_doc.selected());
        return;
    }

    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (hasSelection()) {
        if ((ctrl && e->key() == Qt::Key_C) || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            finish(FinishAction::Copy);
            return;
        }
        if (ctrl && e->key() == Qt::Key_S) {
            finish(FinishAction::Save);
            return;
        }
        if (ctrl && e->key() == Qt::Key_T) {
            finish(FinishAction::Pin);
            return;
        }
        if (ctrl && e->key() == Qt::Key_L && scrollCaptureSupported()) {
            finish(FinishAction::LongCapture);
            return;
        }
    } else if (e->key() == Qt::Key_C && !ctrl) {
        // 无选区时按 C：复制光标处颜色并退出
        QGuiApplication::clipboard()->setText(colorAtCursor().name().toUpper());
        cancel();
        return;
    }

    // 方向键：有选区时微调选区，否则逐像素移动光标
    QPoint d;
    switch (e->key()) {
    case Qt::Key_Left: d = {-1, 0}; break;
    case Qt::Key_Right: d = {1, 0}; break;
    case Qt::Key_Up: d = {0, -1}; break;
    case Qt::Key_Down: d = {0, 1}; break;
    default: QWidget::keyPressEvent(e); return;
    }
    if (m_state->canNudgeSelection()) {
        QRect r = m_selection;
        if (e->modifiers() & Qt::ShiftModifier) // Shift+方向键：调整大小
            r.setBottomRight(r.bottomRight() + d);
        else
            r.translate(d);
        r &= rect();
        if (!r.isEmpty())
            setSelection(r);
    } else if (!hasSelection()) {
        QCursor::setPos(mapToGlobal(m_cursor + d));
    }
}
