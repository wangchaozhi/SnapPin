#include "pinwindow.h"

#include "app/imageutil.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QWheelEvent>

PinWindow::PinWindow(const QImage& image)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_image(image)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setScale(1.0, QPoint());
}

void PinWindow::setImage(const QImage& image)
{
    m_image = image;
    m_scale = 1.0;
    setScale(1.0, QPoint());
}

void PinWindow::setScale(qreal scale, const QPoint& anchorGlobal)
{
    scale = qBound(0.1, scale, 8.0);
    const QSizeF base = QSizeF(m_image.size()) / m_image.devicePixelRatio();
    const QSize newSize = (base * scale).toSize().expandedTo(QSize(8, 8));
    if (!anchorGlobal.isNull()) {
        // 以光标为中心缩放：光标下的图像点保持不动
        const QPointF offset = QPointF(anchorGlobal - pos()) * (scale / m_scale);
        move(anchorGlobal - offset.toPoint());
    }
    m_scale = scale;
    resize(newSize);
    setToolTip(tr("缩放 %1%").arg(qRound(m_scale * 100)));
    update();
}

void PinWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, !qFuzzyCompare(m_scale, 1.0));
    p.drawImage(rect(), m_image);
    p.setPen(hasFocus() ? QColor(0, 150, 255) : QColor(128, 128, 128, 160));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
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
    if (m_dragging)
        move(e->globalPosition().toPoint() - m_dragOffset);
}

void PinWindow::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        m_dragging = false;
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        close();
}

void PinWindow::wheelEvent(QWheelEvent* e)
{
    const int delta = e->angleDelta().y();
    if (delta == 0)
        return;
    if (e->modifiers() & Qt::ControlModifier) {
        setWindowOpacity(qBound(0.2, windowOpacity() + (delta > 0 ? 0.1 : -0.1), 1.0));
        return;
    }
    setScale(m_scale * (delta > 0 ? 1.1 : 1 / 1.1), e->globalPosition().toPoint());
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
    else if (e->key() == Qt::Key_1 && !ctrl)
        setScale(1.0, QPoint());
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
    menu.addAction(tr("原始大小\t1"), this, [this] { setScale(1.0, QPoint()); });
    QMenu* opacity = menu.addMenu(tr("透明度"));
    for (int pct : {100, 80, 60, 40}) {
        QAction* a = opacity->addAction(QStringLiteral("%1%").arg(pct), this, [this, pct] { setWindowOpacity(pct / 100.0); });
        a->setCheckable(true);
        a->setChecked(qRound(windowOpacity() * 100) == pct);
    }
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
    QGuiApplication::clipboard()->setImage(m_image);
}

void PinWindow::save()
{
    saveImageInteractive(m_image, this);
}
