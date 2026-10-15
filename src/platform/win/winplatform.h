#pragma once

#include "platform/platform.h"

#include <QAbstractNativeEventFilter>
#include <QHash>

#include <windows.h>

class GdiScreenCapturer : public IScreenCapturer {
public:
    QPixmap grab(QScreen* screen) override;
};

// DXGI Desktop Duplication：每个显示器保持一个复制会话，没有新画面时复用上一帧
class DxgiScreenCapturer : public IScreenCapturer {
public:
    DxgiScreenCapturer();
    ~DxgiScreenCapturer() override;
    QPixmap grab(QScreen* screen) override; // 失败返回空 pixmap

    struct Output; // 定义在 dxgicapturer.cpp

private:
    std::vector<std::unique_ptr<Output>> m_outputs;
};

// 策略组合：按设置选择 DXGI 或 GDI，DXGI 失败时自动退回 GDI
class WinScreenCapturer : public IScreenCapturer {
public:
    QPixmap grab(QScreen* screen) override;
    void setBackend(const QString& backend) override { m_backend = backend; }

private:
    QString m_backend = QStringLiteral("auto");
    DxgiScreenCapturer m_dxgi;
    GdiScreenCapturer m_gdi;
};

class WinWindowFinder : public IWindowFinder {
public:
    QList<DetectedWindow> windowsOnScreen(QScreen* screen) override;
    void requestElements(QObject* context, ElementsCallback onReady) override;
    void setElementDetection(bool enabled) override { m_elementDetection = enabled; }

private:
    QList<HWND> m_topLevels; // 最近一次枚举的顶层窗口，下标即 group
    bool m_elementDetection = true;
};

class WinHotkeyManager : public IHotkeyManager, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit WinHotkeyManager(QObject* parent = nullptr);
    ~WinHotkeyManager() override;

    bool registerHotkey(const QString& actionId, const QKeySequence& seq) override;
    void unregisterAll() override;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    QHash<int, QString> m_ids;
    int m_nextId = 1;
};

// 屏幕在虚拟桌面中的物理像素矩形（进程为 Per-Monitor DPI Aware V2，Win32 坐标即物理像素）
QRect physicalScreenRect(QScreen* screen);
HMONITOR monitorHandle(QScreen* screen);

// 在当前线程（需已初始化 COM）用 UI Automation 收集窗口内可见控件的物理像素矩形
QList<QRect> collectUiaElements(HWND hwnd, int maxCount);
