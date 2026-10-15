#include "macplatform.h"

#include <QDateTime>
#include <QImage>
#include <QScreen>

#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

namespace {

constexpr qint64 kContentTtlMs = 5000;                 // 显示器列表缓存时长
constexpr int64_t kTimeoutNs = 3 * int64_t(NSEC_PER_SEC); // ScreenCaptureKit 是异步接口，同步等待的上限

CGDirectDisplayID displayIdFor(QScreen* screen)
{
    // Qt 的全局逻辑坐标与 CoreGraphics 全局坐标一致
    const QPoint c = screen->geometry().center();
    CGDirectDisplayID id = 0;
    uint32_t count = 0;
    if (CGGetDisplaysWithPoint(CGPointMake(c.x(), c.y()), 1, &id, &count) != kCGErrorSuccess || count == 0)
        return CGMainDisplayID();
    return id;
}

// 转成不带 alpha 的 32 位图（与 Windows 实现一致，后续的拼接、取色都按 RGB32 处理）
QImage toImage(CGImageRef cg)
{
    const size_t w = CGImageGetWidth(cg);
    const size_t h = CGImageGetHeight(cg);
    QImage image(int(w), int(h), QImage::Format_RGB32);
    if (image.isNull())
        return {};
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(image.bits(), w, h, 8, size_t(image.bytesPerLine()), cs,
                                             kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    if (!ctx)
        return {};
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
    CGContextRelease(ctx);
    return image;
}

} // namespace

struct MacScreenCapturer::Impl {
    SCShareableContent* content = nil;
    qint64 fetchedAt = 0;

    SCDisplay* display(CGDirectDisplayID id, bool refresh)
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (refresh || !content || now - fetchedAt > kContentTtlMs) {
            __block SCShareableContent* fetched = nil;
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                       onScreenWindowsOnly:YES
                                                         completionHandler:^(SCShareableContent* c, NSError*) {
                                                             fetched = c;
                                                             dispatch_semaphore_signal(done);
                                                         }];
            if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, kTimeoutNs)) != 0)
                return nil;
            content = fetched;
            fetchedAt = now;
        }
        for (SCDisplay* d in content.displays) {
            if (d.displayID == id)
                return d;
        }
        return nil;
    }

    QImage capture(SCDisplay* display, const QSize& pixelSize)
    {
        // 不排除任何窗口：贴图也要截进去；截图遮罩此时还没显示
        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        config.width = size_t(pixelSize.width());
        config.height = size_t(pixelSize.height());
        config.showsCursor = NO;
        config.captureResolution = SCCaptureResolutionBest;
        config.colorSpaceName = kCGColorSpaceSRGB;

        __block CGImageRef result = nullptr;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [SCScreenshotManager captureImageWithFilter:filter
                                      configuration:config
                                  completionHandler:^(CGImageRef image, NSError*) {
                                      if (image)
                                          result = CGImageRetain(image);
                                      dispatch_semaphore_signal(done);
                                  }];
        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, kTimeoutNs)) != 0)
            return {}; // 超时：回调晚到时 result 泄漏一帧，可以接受
        if (!result)
            return {};
        QImage image = toImage(result);
        CGImageRelease(result);
        return image;
    }
};

MacScreenCapturer::MacScreenCapturer()
    : d(new Impl)
{
}

MacScreenCapturer::~MacScreenCapturer()
{
    delete d;
}

QPixmap MacScreenCapturer::grab(QScreen* screen)
{
    const qreal dpr = screen->devicePixelRatio();
    const QSize pixelSize = (QSizeF(screen->geometry().size()) * dpr).toSize();
    const CGDirectDisplayID id = displayIdFor(screen);

    QImage image;
    if (CGPreflightScreenCaptureAccess()) {
        SCDisplay* display = d->display(id, false);
        if (!display) // 显示器插拔后缓存失效
            display = d->display(id, true);
        if (display)
            image = d->capture(display, pixelSize);
    }
    if (image.isNull())
        return screen->grabWindow(0);

    QPixmap pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}
