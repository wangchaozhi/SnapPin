#include "capturesession.h"

#include "platform/platform.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

CaptureSession::CaptureSession(IScreenCapturer* capturer, IWindowFinder* finder, QObject* parent)
    : QObject(parent)
    , m_capturer(capturer)
    , m_finder(finder)
{
}

CaptureSession::~CaptureSession()
{
    qDeleteAll(m_overlays);
}

void CaptureSession::start()
{
    const QList<QScreen*> screens = QGuiApplication::screens();

    // 先截图、先识别窗口，再显示任何遮罩，避免把遮罩自己截进去
    QList<QPixmap> shots;
    for (QScreen* s : screens) {
        shots.append(m_capturer->grab(s));
        if (m_includeCursor)
            m_capturer->paintCursor(shots.last(), s);
    }
    QList<QList<DetectedWindow>> windows;
    for (QScreen* s : screens)
        windows.append(m_finder->windowsOnScreen(s));

    m_finder->requestElements(this, [this](QScreen* screen, const QList<DetectedWindow>& elements) {
        for (OverlayWindow* w : std::as_const(m_overlays)) {
            if (w->targetScreen() == screen)
                w->addElements(elements);
        }
    });

    OverlayWindow* active = nullptr;
    for (int i = 0; i < screens.size(); ++i) {
        OverlayWindow* w = addOverlay(screens[i], shots[i], windows[i]);
        if (screens[i]->geometry().contains(QCursor::pos()))
            active = w;
    }
    showAll(active);
}

void CaptureSession::startEdit(const QImage& image, const QPoint& globalTopLeft)
{
    const QSizeF logicalSize = QSizeF(image.size()) / image.devicePixelRatio();
    const QRect globalRect(globalTopLeft, logicalSize.toSize());
    QScreen* screen = QGuiApplication::screenAt(globalRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    QPixmap shot = m_capturer->grab(screen);
    const QList<DetectedWindow> windows = m_finder->windowsOnScreen(screen);

    const QRect local = globalRect.translated(-screen->geometry().topLeft());
    {
        QPainter p(&shot); // shot 已设置 devicePixelRatio，按逻辑坐标绘制
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(local.topLeft(), logicalSize), image);
    }

    OverlayWindow* w = addOverlay(screen, shot, windows);
    w->presetSelection(local);
    showAll(w);
}

OverlayWindow* CaptureSession::addOverlay(QScreen* screen, const QPixmap& shot, const QList<DetectedWindow>& windows)
{
    auto* w = new OverlayWindow(screen, shot, windows);
    connect(w, &OverlayWindow::finished, this, [this](const QImage& img, const QRect& r, FinishAction a) {
        closeAll();
        emit finished(img, r, a);
    });
    connect(w, &OverlayWindow::cancelled, this, &CaptureSession::closeAll);
    m_overlays.append(w);
    return w;
}

void CaptureSession::showAll(OverlayWindow* active)
{
    for (OverlayWindow* w : std::as_const(m_overlays))
        w->show();
    if (!active && !m_overlays.isEmpty())
        active = m_overlays.first();
    if (active) {
        active->raise();
        active->activateWindow();
        active->setFocus();
    }
}

void CaptureSession::closeAll()
{
    for (OverlayWindow* w : std::as_const(m_overlays)) {
        w->hide();
        w->deleteLater();
    }
    m_overlays.clear();
    emit closed();
}
