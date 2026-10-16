#include "pinwindow.h"

#include "app/imageutil.h"
#include "platform/platform.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QWheelEvent>
#include <QWindow>

namespace {
constexpr int kShadow = 10;         // 阴影宽度（逻辑像素），窗口比图片四周各大这么多
constexpr qreal kThumbnail = 64.0;  // 缩略图最长边
constexpr int kHintMs = 800;        // 缩放 / 不透明度提示显示时长
}

PinWindow::PinWindow(const QImage& image)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_source(image)
    , m_view(image)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground); // 阴影区域透明
    setFocusPolicy(Qt::StrongFocus);
    setupFloatingWindow(this, FloatingLayer::Pin);
    m_hintTimer.setSingleShot(true);
    m_hintTimer.setInterval(kHintMs);
    connect(&m_hintTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    setScale(1.0, QPoint());
}

void PinWindow::setImage(const QImage& image)
{
    m_source = image;
    m_rotation = 0;
    m_flipH = m_flipV = false;
    m_view = image;
    m_scale = 1.0;
    m_thumbnail = false;
    setScale(1.0, QPoint());
}

QPoint PinWindow::imagePos() const
{
    return pos() + QPoint(kShadow, kShadow);
}

void PinWindow::moveImageTo(const QPoint& globalTopLeft)
{
    move(globalTopLeft - QPoint(kShadow, kShadow));
}

QRect PinWindow::imageRect() const
{
    return rect().adjusted(kShadow, kShadow, -kShadow, -kShadow);
}

void PinWindow::setScale(qreal scale, const QPoint& anchorGlobal)
{
    scale = qBound(0.1, scale, 8.0);
    const QSizeF base = QSizeF(m_view.size()) / m_view.devicePixelRatio();
    const QSize imageSize = (base * scale).toSize().expandedTo(QSize(8, 8));
    if (!anchorGlobal.isNull()) {
        // 以光标为中心缩放：光标下的图像点保持不动
        const QPointF offset = QPointF(anchorGlobal - imagePos()) * (scale / m_scale);
        moveImageTo(anchorGlobal - offset.toPoint());
    }
    m_scale = scale;
    // 固定尺寸：sway 等平铺式合成器才会让它浮动，而不是铺满整个工作区
    setFixedSize(imageSize + QSize(2 * kShadow, 2 * kShadow));
    setToolTip(tr("缩放 %1%").arg(qRound(m_scale * 100)));
    update();
}

void PinWindow::updateView()
{
    const QPoint center = imagePos() + imageRect().center() - imageRect().topLeft();
    QTransform t;
    t.rotate(90 * m_rotation);
    QImage view = m_source.transformed(t);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    view = view.flipped((m_flipH ? Qt::Horizontal : Qt::Orientations()) | (m_flipV ? Qt::Vertical : Qt::Orientations()));
#else
    view = view.mirrored(m_flipH, m_flipV);
#endif
    view.setDevicePixelRatio(m_source.devicePixelRatio());
    m_view = view;
    setScale(m_scale, QPoint());
    // 旋转后宽高互换：保持图片中心位置不变
    moveImageTo(center - (imageRect().center() - imageRect().topLeft()));
}

void PinWindow::rotate(int quarterTurns)
{
    // 显示图 = 翻转(旋转(原图))。对已翻转的图再转 90°，等价于先多转 90° 再沿另一个方向翻转
    if (quarterTurns % 2 != 0 && m_flipH != m_flipV)
        std::swap(m_flipH, m_flipV);
    m_rotation = (m_rotation + quarterTurns + 4) % 4;
    updateView();
}

void PinWindow::flip(bool horizontal)
{
    // 翻转在旋转之后进行，方向就是屏幕上的方向
    if (horizontal)
        m_flipH = !m_flipH;
    else
        m_flipV = !m_flipV;
    updateView();
}

void PinWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect img = imageRect();

    // 柔和的阴影：由外向内逐圈加深；获得焦点时带一点主题蓝
    const QColor shade = hasFocus() ? QColor(0, 110, 255) : QColor(0, 0, 0);
    p.setBrush(Qt::NoBrush);
    for (int i = kShadow; i > 0; --i) {
        const qreal t = 1.0 - qreal(i) / kShadow;
        QColor c = shade;
        c.setAlphaF((hasFocus() ? 0.22 : 0.16) * t * t);
        p.setPen(QPen(c, 1.0));
        p.drawRoundedRect(QRectF(img).adjusted(-i + 0.5, -i + 0.5, i - 0.5, i - 0.5), i, i);
    }

    p.setRenderHint(QPainter::SmoothPixmapTransform, !qFuzzyCompare(m_scale, 1.0));
    p.drawImage(img, m_view);
    p.setRenderHint(QPainter::Antialiasing, false);
    if (clickThrough()) // 鼠标穿透：虚线边框提示
        p.setPen(QPen(QColor(0, 120, 255, 200), 1, Qt::DashLine));
    else
        p.setPen(hasFocus() ? QColor(0, 120, 255, 200) : QColor(0, 0, 0, 50));
    p.drawRect(img.adjusted(0, 0, -1, -1));

    if (m_hintTimer.isActive()) { // 缩放比例 / 不透明度提示
        p.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics fm(font());
        QRectF box(0, 0, fm.horizontalAdvance(m_hint) + 16, fm.height() + 8);
        box.moveCenter(QRectF(img).center());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, m_hint);
    }
}

void PinWindow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = e->globalPosition().toPoint() - pos();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        // Wayland 不允许程序自己摆放窗口，交给合成器拖动（放在移动时而不是按下时，双击关闭才不受影响）
        m_dragging = false;
        windowHandle()->startSystemMove();
        return;
    }
    move(e->globalPosition().toPoint() - m_dragOffset);
}

void PinWindow::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        m_dragging = false;
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (e->modifiers() & Qt::ShiftModifier) // Shift+双击：缩略图 / 还原
        toggleThumbnail();
    else
        close();
}

void PinWindow::wheelEvent(QWheelEvent* e)
{
    const int delta = e->angleDelta().y();
    if (delta == 0)
        return;
    if (e->modifiers() & Qt::ControlModifier) {
        setWindowOpacity(qBound(0.2, windowOpacity() + (delta > 0 ? 0.1 : -0.1), 1.0));
        showHint(tr("不透明度 %1%").arg(qRound(windowOpacity() * 100)));
        return;
    }
    m_thumbnail = false;
    setScale(m_scale * (delta > 0 ? 1.1 : 1 / 1.1), e->globalPosition().toPoint());
    showHint(QStringLiteral("%1%").arg(qRound(m_scale * 100)));
}

void PinWindow::showHint(const QString& text)
{
    m_hint = text;
    m_hintTimer.start();
    update();
}

void PinWindow::toggleThumbnail()
{
    // 缩成最长边 kThumbnail 的小图，再次切换恢复原来的缩放；以图片左上角为锚点
    if (!m_thumbnail) {
        const QSizeF base = QSizeF(m_view.size()) / m_view.devicePixelRatio();
        const qreal fit = kThumbnail / qMax(base.width(), base.height());
        if (fit >= m_scale)
            return; // 本来就很小
        m_scaleBeforeThumbnail = m_scale;
        m_thumbnail = true;
        setScale(fit, QPoint());
    } else {
        m_thumbnail = false;
        setScale(m_scaleBeforeThumbnail, QPoint());
    }
}

void PinWindow::setClickThrough(bool on)
{
    if (clickThrough() == on)
        return;
    // 改窗口标志会把窗口隐藏，需要重新显示
    const bool visible = isVisible();
    setWindowFlag(Qt::WindowTransparentForInput, on);
    if (visible)
        show();
    update();
    emit clickThroughChanged(on);
}

bool PinWindow::clickThrough() const
{
    return windowFlags().testFlag(Qt::WindowTransparentForInput);
}

PinSnapshot PinWindow::snapshot() const
{
    return {m_source, m_rotation, m_flipH, m_flipV, m_scale, windowOpacity(), imagePos()};
}

void PinWindow::applySnapshot(const PinSnapshot& s)
{
    m_source = s.source;
    m_rotation = s.rotation;
    m_flipH = s.flipH;
    m_flipV = s.flipV;
    m_scale = s.scale;
    setWindowOpacity(s.opacity);
    updateView();
    moveImageTo(s.imagePos);
}

void PinWindow::closeEvent(QCloseEvent* e)
{
    emit closing(snapshot());
    QWidget::closeEvent(e);
}

void PinWindow::keyPressEvent(QKeyEvent* e)
{
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (e->key() == Qt::Key_Escape || (ctrl && e->key() == Qt::Key_W))
        close();
    else if (ctrl && e->key() == Qt::Key_C)
        copyToClipboard();
    else if (ctrl && e->key() == Qt::Key_S)
        save();
    else if (e->key() == Qt::Key_0 && !ctrl)
        setScale(1.0, QPoint());
    else if (e->key() == Qt::Key_1 && !ctrl) // 与 Snipaste 一致：1/2 旋转，3/4 翻转
        rotate(-1);
    else if (e->key() == Qt::Key_2 && !ctrl)
        rotate(1);
    else if (e->key() == Qt::Key_3 && !ctrl)
        flip(true);
    else if (e->key() == Qt::Key_4 && !ctrl)
        flip(false);
    else if (e->key() == Qt::Key_Space)
        emit editRequested();
    else
        QWidget::keyPressEvent(e);
}

void PinWindow::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    menu.addAction(tr("编辑标注\t空格"), this, &PinWindow::editRequested);
    menu.addSeparator();
    menu.addAction(tr("复制\tCtrl+C"), this, &PinWindow::copyToClipboard);
    menu.addAction(tr("另存为...\tCtrl+S"), this, &PinWindow::save);
    menu.addAction(tr("原始大小\t0"), this, [this] { setScale(1.0, QPoint()); });
    QMenu* transform = menu.addMenu(tr("旋转 / 翻转"));
    transform->addAction(tr("向左旋转\t1"), this, [this] { rotate(-1); });
    transform->addAction(tr("向右旋转\t2"), this, [this] { rotate(1); });
    transform->addAction(tr("水平翻转\t3"), this, [this] { flip(true); });
    transform->addAction(tr("垂直翻转\t4"), this, [this] { flip(false); });
    QMenu* opacity = menu.addMenu(tr("透明度"));
    for (int pct : {100, 80, 60, 40}) {
        QAction* a = opacity->addAction(QStringLiteral("%1%").arg(pct), this, [this, pct] { setWindowOpacity(pct / 100.0); });
        a->setCheckable(true);
        a->setChecked(qRound(windowOpacity() * 100) == pct);
    }
    QAction* thumb = menu.addAction(tr("缩略图\tShift+双击"), this, &PinWindow::toggleThumbnail);
    thumb->setCheckable(true);
    thumb->setChecked(m_thumbnail);
    // 穿透后贴图收不到鼠标：把光标移到贴图上按快捷键切换回来（没设快捷键时从托盘菜单取消）
    const QString keys = s_clickThroughKeys.isEmpty()
                             ? QString()
                             : QLatin1Char('\t') + QKeySequence(s_clickThroughKeys).toString(QKeySequence::NativeText);
    menu.addAction(tr("鼠标穿透") + keys, this, [this] { setClickThrough(true); });
    menu.addSeparator();
    menu.addAction(tr("关闭\tEsc"), this, &QWidget::close);
    menu.addAction(tr("关闭所有贴图"), this, &PinWindow::closeAllRequested);
    menu.exec(e->globalPos());
}

void PinWindow::focusInEvent(QFocusEvent*)
{
    update();
}

void PinWindow::focusOutEvent(QFocusEvent*)
{
    update();
}

void PinWindow::copyToClipboard()
{
    QGuiApplication::clipboard()->setImage(m_view);
}

void PinWindow::save()
{
    saveImageInteractive(m_view, this);
}
