#include "pinmanager.h"

#include "app/imageutil.h"
#include "pinwindow.h"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QMimeData>
#include <QScreen>

PinManager::~PinManager()
{
    closeAll();
}

PinWindow* PinManager::pin(const QImage& image, const QPoint& globalTopLeft)
{
    auto* w = new PinWindow(image);
    connect(w, &PinWindow::closeAllRequested, this, &PinManager::closeAll);
    connect(w, &PinWindow::editRequested, this, [this, w] { emit editRequested(w); });
    connect(w, &QObject::destroyed, this, [this] {
        m_pins.removeIf([](const QPointer<PinWindow>& p) { return p.isNull(); });
    });
    m_pins.append(w);

    w->move(globalTopLeft);
    if (m_hidden) { // 新贴图出现时恢复所有隐藏的贴图
        m_hidden = false;
        for (const auto& p : std::as_const(m_pins))
            if (p) p->show();
    }
    w->show();
    w->raise();
    w->activateWindow();
    return w;
}

bool PinManager::pinClipboard()
{
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return false;

    const QPoint cursor = QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(cursor);
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

    QImage image;
    if (mime->hasImage()) {
        image = qvariant_cast<QImage>(mime->imageData());
        image.setDevicePixelRatio(dpr); // 按物理像素 1:1 显示
    } else if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        image = renderTextImage(mime->text(), dpr);
    }
    if (image.isNull())
        return false;

    const QSize logical = (QSizeF(image.size()) / dpr).toSize();
    pin(image, cursor - QPoint(logical.width() / 2, logical.height() / 2));
    return true;
}

void PinManager::toggleVisible()
{
    m_hidden = !m_hidden;
    for (const auto& p : std::as_const(m_pins))
        if (p) p->setVisible(!m_hidden);
}

void PinManager::closeAll()
{
    const auto pins = m_pins;
    m_pins.clear();
    for (const auto& p : pins)
        if (p) p->close();
    m_hidden = false;
}
