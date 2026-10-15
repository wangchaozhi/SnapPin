#pragma once

#include "overlaywindow.h"
#include "scrollstitcher.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

class IScreenCapturer;
class IWheelThrottle;
class QLabel;
class QScreen;
class QWidget;

// 长截图会话：在选区外画一圈边框（鼠标可穿透），用户用滚轮滚动内容，
// 定时截取选区并交给 ScrollStitcher 拼接；旁边的控制面板显示预览和完成按钮。
// 选区内的滚轮输入由 IWheelThrottle 接管，每拼接完一步才放出下一步，滚得再快也能接上
class ScrollCaptureSession : public QObject {
    Q_OBJECT
public:
    // globalRect：选区的全局逻辑坐标，必须位于 screen 内
    ScrollCaptureSession(IScreenCapturer* capturer, IWheelThrottle* throttle, QScreen* screen,
                         const QRect& globalRect, QObject* parent = nullptr);
    ~ScrollCaptureSession() override;

    void start();

signals:
    void finished(const QImage& image, const QRect& globalRect, FinishAction action);
    void closed();

private:
    void tick();
    void replayNextStep();
    void adaptReplayStep();
    void updatePanel(ScrollStitcher::Result r);
    void updateStatus();
    void finish(FinishAction action);
    void close();
    QWidget* createPanel();

    IScreenCapturer* m_capturer;
    IWheelThrottle* m_throttle;
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
    QString m_statusBase;
    bool m_closed = false;
    QImage m_pending;           // 最近一次截到的帧
    bool m_pendingUsed = false; // 是否已经交给拼接器

    bool m_throttleActive = false;
    bool m_awaitingResponse = false; // 已重放一步，等画面滚动并停稳
    QElapsedTimer m_sinceReplay;
    int m_replayStep = 120;          // 每步重放的滚轮量，按实际滚动距离自适应
    int m_lastReplayDelta = 0;
};
