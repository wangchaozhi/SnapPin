#include "winplatform.h"

#include <QImage>
#include <QScreen>
#include <QtGui/qscreen_platform.h>

#include <cstring>
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

void WinScreenCapturer::paintCursor(QPixmap& shot, QScreen* screen)
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
        return;
    ICONINFO ii{};
    if (!GetIconInfo(ci.hCursor, &ii))
        return;
    const QRect phys = physicalScreenRect(screen);
    const int x = ci.ptScreenPos.x - int(ii.xHotspot) - phys.left();
    const int y = ci.ptScreenPos.y - int(ii.yHotspot) - phys.top();
    if (ii.hbmMask)
        DeleteObject(ii.hbmMask);
    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);

    // 用 GDI 的 DrawIconEx 画，彩色指针、反色的 I 形光标都和系统显示一致
    QImage img = shot.toImage().convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    if (x >= w || y >= h || x <= -64 || y <= -64) // 指针不在这块屏幕上（多屏时其他屏也会走到这里）
        return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(memDc, bmp);
        std::memcpy(bits, img.constBits(), size_t(img.sizeInBytes()));
        if (DrawIconEx(memDc, x, y, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL | DI_DEFAULTSIZE)) {
            GdiFlush();
            std::memcpy(img.bits(), bits, size_t(img.sizeInBytes()));
            const qreal dpr = shot.devicePixelRatio();
            shot = QPixmap::fromImage(img);
            shot.setDevicePixelRatio(dpr);
        }
        SelectObject(memDc, old);
        DeleteObject(bmp);
    }
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
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
