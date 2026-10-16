#pragma once

#include "overlaywindow.h"

#include <QList>
#include <QObject>

class IScreenCapturer;
class IWindowFinder;

// 一次截图会话：先截下所有屏幕，再在每块屏幕上显示一个遮罩
class CaptureSession : public QObject {
    Q_OBJECT
public:
    CaptureSession(IScreenCapturer* capturer, IWindowFinder* finder, QObject* parent = nullptr);
    ~CaptureSession() override;

    void setIncludeCursor(bool on) { m_includeCursor = on; } // 截图里带上鼠标指针
    void start();
    // 编辑已有图片（贴图）：把图片按原始大小合成到所在屏幕的截图上，并预先选中它
    void startEdit(const QImage& image, const QPoint& globalTopLeft);

signals:
    void finished(const QImage& image, const QRect& globalRect, FinishAction action);
    void closed(); // 完成或取消后发出，会话随后可销毁

private:
    OverlayWindow* addOverlay(QScreen* screen, const QPixmap& shot, const QList<DetectedWindow>& windows);
    void showAll(OverlayWindow* active);
    void closeAll();

    IScreenCapturer* m_capturer;
    IWindowFinder* m_finder;
    QList<OverlayWindow*> m_overlays;
    bool m_includeCursor = false;
};
