#include "overlaywindow.h"

#include "editortoolbar.h"
#include "imageregions.h"
#include "overlaystates.h"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QWheelEvent>
#include <algorithm>

namespace {
constexpr int kMagnifierCells = 17; // 放大区域：17×17 物理像素
constexpr int kMagnifierCell = 8;   // 每个像素放大为 8×8
constexpr int kWidthHintMs = 900;   // 滚轮调粗细后，光标处的粗细预览显示多久
constexpr qreal kMinWidth = 1;
constexpr qreal kMaxWidth = 30;
constexpr int kRegionHistoryMax = 20;
constexpr int kMinImageRegion = 8; // 从截图像素识别出的框，比这还小（逻辑像素）的不用
constexpr int kBlockGap = 8; // 内容块之间的空白超过它（逻辑像素）才算分开的两块
constexpr int kHoverSettleMs = 150; // 光标还在当前框里时，新识别到的框要稳定这么久才切换
constexpr int kSnapDistance = 5; // 拖动选区边时，离窗口 / 控件边缘这么近就吸附过去（逻辑像素）
const QString kRegionHistoryKey = QStringLiteral("capture/regionHistory");

bool g_colorHex = false; // 放大镜颜色显示 / 复制格式：HEX 或 RGB，本次运行内记住

// 每个工具各自记住上次用的颜色 / 粗细 / 填充（重启后仍有效），互不影响
Style defaultToolStyle(ToolType t)
{
    Style s;
    if (t == ToolType::Marker)
        s.color = QColor(0xFFCC00); // 荧光笔默认黄色
    return s;
}

QString toolStyleKey(ToolType t)
{
    return QStringLiteral("annotate/style%1").arg(int(t));
}

Style loadToolStyle(ToolType t)
{
    Style s = defaultToolStyle(t);
    const QStringList v = QSettings().value(toolStyleKey(t)).toString().split(QLatin1Char(','));
    if (v.size() == 3) {
        const QColor c(v[0]);
        if (c.isValid())
            s.color = c;
        s.width = qBound(1.0, v[1].toDouble(), 30.0);
        s.fill = v[2] == QLatin1String("1");
    }
    return s;
}

void saveToolStyle(ToolType t, const Style& s)
{
    if (t == ToolType::None)
        return;
    QSettings().setValue(toolStyleKey(t), QStringLiteral("%1,%2,%3").arg(s.color.name()).arg(s.width).arg(s.fill ? 1 : 0));
}

QString colorText(const QColor& c)
{
    return g_colorHex ? c.name().toUpper() : QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue());
}

// 截图区域历史（全局逻辑坐标，新的在前），按 , 和 . 切换
QList<QRect> loadRegionHistory()
{
    QList<QRect> list;
    for (const QString& s : QSettings().value(kRegionHistoryKey).toStringList()) {
        const QStringList v = s.split(QLatin1Char(','));
        if (v.size() == 4)
            list.append(QRect(v[0].toInt(), v[1].toInt(), v[2].toInt(), v[3].toInt()));
    }
    return list;
}

void pushRegionHistory(const QRect& r)
{
    QList<QRect> list = loadRegionHistory();
    list.removeAll(r);
    list.prepend(r);
    QStringList out;
    for (int i = 0; i < list.size() && i < kRegionHistoryMax; ++i)
        out << QStringLiteral("%1,%2,%3,%4").arg(list[i].x()).arg(list[i].y()).arg(list[i].width()).arg(list[i].height());
    QSettings().setValue(kRegionHistoryKey, out);
}
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
    // 选中了已画的图形时，颜色 / 粗细改到这个图形上（可撤销）；否则改当前工具的样式
    connect(m_toolbar, &EditorToolbar::styleChanged, this, [this](const Style& s) {
        if (Shape* shape = m_doc.selected()) {
            m_doc.restyle(shape, s);
        } else {
            m_toolCtx.style = s;
            saveToolStyle(m_toolType, s);
            if (m_tool)
                m_tool->styleChanged(m_toolCtx);
            update();
        }
    });
    connect(&m_doc, &AnnotationDoc::selectionChanged, this, [this](Shape* shape) {
        m_toolbar->setStyle(shape ? shape->style : m_toolCtx.style);
    });
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(kHoverSettleMs);
    connect(&m_hoverTimer, &QTimer::timeout, this, [this] { setHover(m_pendingHover); });
    m_widthHintTimer.setSingleShot(true);
    m_widthHintTimer.setInterval(kWidthHintMs);
    connect(&m_widthHintTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(m_toolbar, &EditorToolbar::pinRequested, this, [this] { finish(FinishAction::Pin); });
    connect(m_toolbar, &EditorToolbar::saveRequested, this, [this] { finish(FinishAction::Save); });
    connect(m_toolbar, &EditorToolbar::longCaptureRequested, this, [this] { finish(FinishAction::LongCapture); });
    connect(m_toolbar, &EditorToolbar::copyRequested, this, [this] { finish(FinishAction::Copy); });
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
    if (group < 0)
        best = rect();

    // 再在它里面按截图本身的像素细分：边框围成的矩形、按空白分开的文字段落 / 图标。
    // 远程桌面、虚拟机里的界面没有窗口 / 控件信息也能细到按钮、输入框
    const qreal dpr = m_shot.devicePixelRatio();
    if (!m_imageRegions)
        m_imageRegions = std::make_unique<ImageRegionDetector>(m_shotImage, 16, qRound(kBlockGap * dpr));
    const QRect bestPhys = QRectF(best.x() * dpr, best.y() * dpr, best.width() * dpr, best.height() * dpr).toRect();
    const QRect phys = m_imageRegions->elementAt((QPointF(pos) * dpr).toPoint(), bestPhys);
    if (!phys.isEmpty()) {
        const QRect r = QRectF(phys.x() / dpr, phys.y() / dpr, (phys.right() + 1) / dpr - phys.x() / dpr,
                               (phys.bottom() + 1) / dpr - phys.y() / dpr).toRect() & best;
        if (r.contains(pos) && r.width() >= kMinImageRegion && r.height() >= kMinImageRegion
            && qint64(r.width()) * r.height() < qint64(best.width()) * best.height())
            best = r;
    }
    return best & rect();
}

int OverlayWindow::snap(int value, Qt::Orientation o, bool farEdge, const QPoint& near) const
{
    // 按住 Alt 暂时不吸附
    if (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
        return value;

    // 候选边：没被更上层窗口完全盖住的顶层窗口 + 光标所在窗口里的控件 + 屏幕边缘。
    // 选区的右 / 下边是包含在内的坐标，所以远边吸到“窗口右边”或“紧挨着另一个窗口左边之前”
    const bool horizontal = o == Qt::Horizontal;
    int best = value;
    int bestDist = kSnapDistance + 1;
    auto consider = [&](const QRect& r) {
        const int a = horizontal ? (farEdge ? r.right() : r.left()) : (farEdge ? r.bottom() : r.top());
        const int b = horizontal ? (farEdge ? r.left() - 1 : r.right() + 1) : (farEdge ? r.top() - 1 : r.bottom() + 1);
        for (const int c : {a, b}) {
            const int d = qAbs(c - value);
            if (d < bestDist) {
                bestDist = d;
                best = c;
            }
        }
    };
    consider(rect());

    int nearGroup = -1;
    for (const DetectedWindow& w : m_windows) {
        if (!w.isChild && w.rect.contains(near)) {
            nearGroup = w.group;
            break;
        }
    }
    QList<QRect> above; // 已经遍历过的更上层顶层窗口
    for (const DetectedWindow& w : m_windows) {
        if (w.isChild) {
            if (w.group == nearGroup)
                consider(w.rect);
            continue;
        }
        const bool covered = std::any_of(above.cbegin(), above.cend(), [&](const QRect& a) { return a.contains(w.rect); });
        if (!covered)
            consider(w.rect);
        above.append(w.rect);
    }
    return best;
}

void OverlayWindow::proposeHover(const QRect& r, const QPoint& cursor)
{
    if (r == m_hover || m_hover.isEmpty() || !m_hover.contains(cursor)) {
        setHover(r);
        return;
    }
    if (!m_hoverTimer.isActive() || r != m_pendingHover) {
        m_pendingHover = r;
        m_hoverTimer.start();
    }
}

void OverlayWindow::setHover(const QRect& r)
{
    m_hoverTimer.stop();
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
    if (type != ToolType::None) { // 换上这个工具上次用的样式
        m_toolCtx.style = loadToolStyle(type);
        m_toolbar->setStyle(m_toolCtx.style);
    }
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
    pushRegionHistory(global);
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
    if (m_widthHintTimer.isActive() && m_cursorInside)
        paintWidthHint(p);
}

void OverlayWindow::paintWidthHint(QPainter& p) const
{
    // 光标处按实际效果显示当前粗细：线宽、荧光笔笔宽、马赛克块、序号圆、文字字号
    const qreal w = m_toolCtx.style.width;
    qreal d = w;
    QString label = QString::number(w);
    switch (m_toolType) {
    case ToolType::Marker: d = w * 4; break;
    case ToolType::Eraser: d = w * 4; break;
    case ToolType::Mosaic: d = w * 3; break;
    case ToolType::Number: d = 2 * (9 + w * 1.5); break;
    case ToolType::Text:
        d = 0;
        label = tr("字号 %1").arg(TextShape::fontFor(m_toolCtx.style).pixelSize());
        break;
    default: break;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = m_cursor;
    if (d > 0) {
        p.setPen(QPen(Qt::white, 1));
        QColor fill = m_toolCtx.style.color;
        fill.setAlpha(m_toolType == ToolType::Marker ? 110 : 200);
        if (m_toolType == ToolType::Eraser) { // 橡皮擦没有颜色：只画范围
            fill = QColor(255, 255, 255, 60);
            p.setPen(QPen(QColor(0, 0, 0, 160), 1, Qt::DashLine));
        }
        p.setBrush(fill);
        if (m_toolType == ToolType::Mosaic)
            p.drawRect(QRectF(c.x() - d / 2, c.y() - d / 2, d, d));
        else
            p.drawEllipse(c, d / 2, d / 2);
    }
    const QFontMetrics fm(font());
    QRectF box(0, 0, fm.horizontalAdvance(label) + 10, fm.height() + 4);
    box.moveTopLeft(c + QPointF(qMax(d / 2, 6.0) + 6, -box.height() / 2));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, label);
    p.restore();
}

void OverlayWindow::wheelEvent(QWheelEvent* e)
{
    // 滚轮调粗细：选中了已画的图形时改它，否则改当前工具
    Shape* shape = m_doc.selected();
    if (!hasSelection() || (!shape && m_toolType == ToolType::None)) {
        QWidget::wheelEvent(e);
        return;
    }
    m_cursor = e->position().toPoint(); // 预览画在滚动时的光标处，不依赖之前的鼠标移动
    m_cursorInside = true;
    m_wheelAccum += e->angleDelta().y();
    const int steps = m_wheelAccum / 120; // 高精度滚轮 / 触控板的小增量累计成整格
    if (steps == 0)
        return;
    m_wheelAccum -= steps * 120;

    Style s = shape ? shape->style : m_toolCtx.style;
    s.width = qBound(kMinWidth, s.width + steps, kMaxWidth);
    if (shape) {
        m_doc.restyle(shape, s);
    } else {
        m_toolCtx.style = s;
        saveToolStyle(m_toolType, s);
        if (m_tool)
            m_tool->styleChanged(m_toolCtx); // 正在拖拽的图形、输入中的文字立即变粗 / 变细
        m_toolbar->setStyle(s);
        m_widthHintTimer.start();
        update();
    }
}

void OverlayWindow::stepRegionHistory(int direction)
{
    // , 往更早、. 往更近；只取落在本屏上的区域
    const QList<QRect> history = loadRegionHistory();
    const QRect screenRect = m_screen->geometry();
    for (int i = m_regionIndex + direction; i >= 0 && i < history.size(); i += direction) {
        const QRect local = history[i].translated(-screenRect.topLeft()) & rect();
        if (local.width() < 2 || local.height() < 2)
            continue;
        m_regionIndex = i;
        applySelection(local);
        return;
    }
}

void OverlayWindow::applySelection(const QRect& r)
{
    if (m_toolType != ToolType::None && hasSelection())
        setSelection(r); // 正在标注：只换区域，保留工具
    else
        presetSelection(r);
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
    const int infoH = fm.height() * 3 + 10;
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
               QStringLiteral("(%1, %2)\n%3").arg(phys.x()).arg(phys.y()).arg(colorText(color)));
    p.setPen(QColor(255, 255, 255, 140));
    p.drawText(info.adjusted(6, 3, -4, -3), Qt::AlignLeft | Qt::AlignBottom, tr("Shift 切换格式 · C 复制"));

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
    if (ctrl && e->key() == Qt::Key_A) { // 全屏
        applySelection(rect());
        return;
    }
    if (!ctrl && (e->key() == Qt::Key_Comma || e->key() == Qt::Key_Period)) { // 上一次 / 下一次的截图区域
        stepRegionHistory(e->key() == Qt::Key_Comma ? 1 : -1);
        return;
    }
    if (e->key() == Qt::Key_Shift && !e->isAutoRepeat() && !hasSelection()) { // 切换颜色格式
        g_colorHex = !g_colorHex;
        update();
        return;
    }
    if (hasSelection()) {
        if ((ctrl && e->key() == Qt::Key_C) || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            finish(FinishAction::Copy);
            return;
        }
        if (ctrl && e->key() == Qt::Key_S) { // Ctrl+Shift+S：不弹对话框，直接存到快速保存目录
            finish((e->modifiers() & Qt::ShiftModifier) ? FinishAction::QuickSave : FinishAction::Save);
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
        // 无选区时按 C：按当前格式复制光标处颜色并退出
        QGuiApplication::clipboard()->setText(colorText(colorAtCursor()));
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
