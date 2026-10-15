#pragma once

#include "platform/platform.h"

#include <QHash>
#include <QRect>
#include <thread>

// macOS 坐标约定：CoreGraphics 的全局坐标以主屏左上角为原点、单位为点，与 Qt 的全局逻辑坐标一致

// ScreenCaptureKit 截屏（macOS 14+）。需要“屏幕录制”权限，没有权限时退回 Qt 实现（只能截到壁纸）
class MacScreenCapturer : public IScreenCapturer {
public:
    MacScreenCapturer();
    ~MacScreenCapturer() override;
    QPixmap grab(QScreen* screen) override;

    struct Impl; // 定义在 maccapturer.mm，持有 Objective-C 对象

private:
    Impl* d;
};

// 窗口级用 CGWindowList，控件级用辅助功能（Accessibility）API，后者需要“辅助功能”权限
class MacWindowFinder : public IWindowFinder {
public:
    QList<DetectedWindow> windowsOnScreen(QScreen* screen) override;
    void requestElements(QObject* context, ElementsCallback onReady) override;
    void setElementDetection(bool enabled) override { m_elementDetection = enabled; }

    struct TopLevel {
        unsigned windowId = 0;
        int pid = 0;
        QRect rect; // 全局逻辑坐标
    };

private:
    QList<TopLevel> m_topLevels; // 最近一次枚举的顶层窗口，下标即 group
    bool m_elementDetection = true;
};

// Carbon RegisterEventHotKey：不需要任何权限
class MacHotkeyManager : public IHotkeyManager {
    Q_OBJECT
public:
    explicit MacHotkeyManager(QObject* parent = nullptr);
    ~MacHotkeyManager() override;

    bool registerHotkey(const QString& actionId, const QKeySequence& seq) override;
    void unregisterAll() override;

    void dispatch(quint32 id); // 由 Carbon 事件回调调用

private:
    QHash<quint32, QString> m_ids;
    QHash<quint32, void*> m_refs; // EventHotKeyRef
    quint32 m_nextId = 1;
    void* m_handler = nullptr;    // EventHandlerRef
};

// CGEventTap 运行在独立线程的 RunLoop 上，吞掉选区内的滚动事件并累计，replay() 注入带标记的滚动事件。
// 需要“辅助功能”权限，没有时 start() 返回 false，滚动照常直达程序
class MacWheelThrottle : public IWheelThrottle {
public:
    ~MacWheelThrottle() override;
    bool start(QScreen* screen, const QRect& localPhysRect) override;
    void stop() override;
    int pendingDelta() const override;
    bool replay(int maxDelta) override;
    void clear() override;
    void requeue(int delta) override;
    bool inject(int delta) override;
    bool needsPermission() const override;
    void openPermissionSettings() override;

private:
    std::thread m_thread;
    void* m_runLoop = nullptr; // CFRunLoopRef
};

// 辅助功能权限：prompt 为 true 时，未授权会弹出系统提示（每次进程只弹一次）
bool macAccessibilityTrusted(bool prompt);
