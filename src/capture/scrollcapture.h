#pragma once

#include "overlaywindow.h"
#include "scrollstitcher.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class IScreenCapturer;
class QLabel;
class QScreen;
class QWidget;

// 长截图会话：在选区外画一圈边框（鼠标可穿透），用户用滚轮滚动内容，
// 定时截取选区并交给 ScrollStitcher 拼接；旁边的控制面板显示预览和完成按钮
class ScrollCaptureSession : public QObject {
    Q_OBJECT
public:
    // globalRect：选区的全局逻辑坐标，必须位于 screen 内
    ScrollCaptureSession(IScreenCapturer* capturer, QScreen* screen, const QRect& globalRect, QObject* parent = nullptr);
    ~ScrollCaptureSession() override;

    void start();

signals:
    void finished(const QImage& image, const QRect& globalRect, FinishAction action);
    void closed();

private:
    void tick();
    void updatePanel(ScrollStitcher::Result r);
    void finish(FinishAction action);
    void close();
    QWidget* createPanel();

    IScreenCapturer* m_capturer;
    QPointer<QScreen> m_screen;
    QRect m_globalRect;
    QRect m_physRect; // 截图中的物理像素区域
    qreal m_dpr;
    int m_ignoreRight; // 物理像素
    ScrollStitcher m_stitcher;
    QTimer m_timer;
    QWidget* m_frame = nullptr;
    QWidget* m_panel = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_status = nullptr;
    bool m_closed = false;
    QImage m_pending;          // 最近一次截到的帧
    bool m_pendingUsed = false; // 是否已经交给拼接器
};
