#include "winplatform.h"

#include <QImage>
#include <QScreen>
#include <QtGui/qscreen_platform.h>

#include <windows.h>

HMONITOR monitorHandle(QScreen* screen)
{
    auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
    return native ? native->handle() : nullptr;
}

QRect physicalScreenRect(QScreen* screen)
{
    if (HMONITOR mon = monitorHandle(screen)) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            const RECT& r = mi.rcMonitor;
            return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
        }
    }
    const qreal dpr = screen->devicePixelRatio();
    const QRect g = screen->geometry();
    return QRect(g.topLeft() * dpr, g.size() * dpr);
}

QPixmap GdiScreenCapturer::grab(QScreen* screen)
{
    const QRect r = physicalScreenRect(screen);
    const int w = r.width();
    const int h = r.height();

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // 自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);

    QImage image;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(memDc, bmp);
        // CAPTUREBLT：把分层（半透明）窗口也截进来
        if (BitBlt(memDc, 0, 0, w, h, screenDc, r.left(), r.top(), SRCCOPY | CAPTUREBLT)) {
            GdiFlush();
            // RGB32 忽略 alpha 通道（BitBlt 不保证 alpha 有效）
            image = QImage(static_cast<const uchar*>(bits), w, h, w * 4, QImage::Format_RGB32).copy();
        }
        SelectObject(memDc, old);
        DeleteObject(bmp);
    }
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    if (image.isNull()) // GDI 失败时退回 Qt 实现
        return screen->grabWindow(0);

    QPixmap pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(screen->devicePixelRatio());
    return pixmap;
}
