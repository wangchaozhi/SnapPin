#include "pinmanager.h"

#include "app/imageutil.h"
#include "pinwindow.h"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QMimeData>
#include <QScreen>
#include <QUrl>

PinManager::~PinManager()
{
    closeAll();
}

namespace {
constexpr int kMaxClosed = 20;
}

PinWindow* PinManager::create(const QImage& image)
{
    auto* w = new PinWindow(image);
    connect(w, &PinWindow::closeAllRequested, this, &PinManager::closeAll);
    connect(w, &PinWindow::editRequested, this, [this, w] { emit editRequested(w); });
    connect(w, &PinWindow::ocrRequested, this, [this, w] { emit ocrRequested(w->image()); });
    connect(w, &PinWindow::translateRequested, this, [this, w] { emit translateRequested(w); });
    connect(w, &PinWindow::clickThroughChanged, this, [this](bool on) {
        if (on)
            emit clickThroughEnabled();
    });
    connect(w, &PinWindow::closing, this, [this](const PinSnapshot& s) {
        m_closed.append(s);
        if (m_closed.size() > kMaxClosed)
            m_closed.removeFirst();
    });
    connect(w, &QObject::destroyed, this, [this] {
        m_pins.removeIf([](const QPointer<PinWindow>& p) { return p.isNull(); });
    });
    m_pins.append(w);
    return w;
}

bool PinManager::restoreClosed()
{
    if (m_closed.isEmpty())
        return false;
    PinWindow* w = create(QImage());
    w->applySnapshot(m_closed.takeLast());
    w->show();
    w->raise();
    w->activateWindow();
    return true;
}

int PinManager::clearClickThrough()
{
    int n = 0;
    for (const auto& p : std::as_const(m_pins)) {
        if (p && p->clickThrough()) {
            p->setClickThrough(false);
            ++n;
        }
    }
    return n;
}

PinManager::ClickThroughToggle PinManager::toggleClickThroughAt(const QPoint& globalPos)
{
    // 后创建的贴图在上层，从后往前找
    for (auto it = m_pins.crbegin(); it != m_pins.crend(); ++it) {
        PinWindow* p = *it;
        if (p && p->isVisible() && p->geometry().contains(globalPos)) {
            const bool on = !p->clickThrough();
            p->setClickThrough(on);
            return on ? ClickThroughToggle::On : ClickThroughToggle::Off;
        }
    }
    return clearClickThrough() > 0 ? ClickThroughToggle::ClearedAll : ClickThroughToggle::Nothing;
}

PinWindow* PinManager::pin(const QImage& image, const QPoint& globalTopLeft)
{
    PinWindow* w = create(image);
    w->moveImageTo(globalTopLeft);
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

    // 在资源管理器里复制的图片文件：每个文件贴一张，依次错开
    if (mime->hasUrls()) {
        int n = 0;
        for (const QUrl& url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            QImage img(url.toLocalFile());
            if (img.isNull())
                continue;
            img.setDevicePixelRatio(dpr);
            const QSize logical = (QSizeF(img.size()) / dpr).toSize();
            pin(img, cursor - QPoint(logical.width() / 2, logical.height() / 2) + QPoint(24, 24) * n++);
        }
        if (n > 0)
            return true;
    }

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
