#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QRect>
#include <functional>
#include <memory>

class QScreen;
class QWidget;

// 截屏：返回指定屏幕的整屏图像（物理像素，devicePixelRatio 设为该屏缩放比例）
class IScreenCapturer {
public:
    virtual ~IScreenCapturer() = default;
    virtual QPixmap grab(QScreen* screen) = 0;
    // 截屏后端，取值由平台决定（Windows: "auto" = DXGI 优先，"gdi"）
    virtual void setBackend(const QString&) {}
    // 把截图那一刻的鼠标指针（实际形状）画进截图；不支持的平台什么也不做
    virtual void paintCursor(QPixmap& shot, QScreen* screen) { Q_UNUSED(shot) Q_UNUSED(screen) }
};

struct DetectedWindow {
    QRect rect;           // 屏幕局部的逻辑坐标
    int group = 0;        // 所属顶层窗口的 Z 序编号，0 为最上层
    bool isChild = false; // 顶层窗口内的子控件
};

// 窗口/控件识别
class IWindowFinder {
public:
    using ElementsCallback = std::function<void(QScreen*, const QList<DetectedWindow>&)>;

    virtual ~IWindowFinder() = default;
    // 同步，窗口级。必须在遮罩显示之前调用，否则会把遮罩自己识别进去
    virtual QList<DetectedWindow> windowsOnScreen(QScreen* screen) = 0;
    // 异步补充控件级矩形（如 UI Automation），结果在主线程回调；context 销毁后不再回调。
    // 必须在 windowsOnScreen 之后、遮罩显示之前调用
    virtual void requestElements(QObject* context, ElementsCallback onReady) { Q_UNUSED(context) Q_UNUSED(onReady) }
    virtual void setElementDetection(bool) {}
};

// 全局快捷键
class IHotkeyManager : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    // 返回 false 表示按键被占用或当前平台不支持
    virtual bool registerHotkey(const QString& actionId, const QKeySequence& seq) = 0;
    virtual void unregisterAll() = 0;
    // 当前环境根本不支持全局快捷键时返回给用户的说明（如 Wayland），支持时为空
    virtual QString unavailableReason() const { return {}; }

signals:
    void activated(const QString& actionId);
};

// 长截图：接管目标窗口上的滚轮输入，由调用方按节奏重放，保证每一步滚动都能拼接上
class IWheelThrottle {
public:
    virtual ~IWheelThrottle() = default;
    // localPhysRect：选区在该屏截图中的物理像素矩形；接管范围是选区所在的整个窗口。
    // 必须在长截图自己的边框、面板显示之后调用。不支持时返回 false（滚轮照常直达程序）
    virtual bool start(QScreen* screen, const QRect& localPhysRect) { Q_UNUSED(screen) Q_UNUSED(localPhysRect) return false; }
    virtual void stop() {}
    virtual int pendingDelta() const { return 0; } // 已拦截尚未重放的滚轮量，正数向上
    // 重放不超过 maxDelta 的滚轮量；光标已离开目标窗口时丢弃队列并返回 false
    virtual bool replay(int maxDelta) { Q_UNUSED(maxDelta) return false; }
    virtual void clear() {} // 丢弃排队的滚轮量（已滚到头）
    virtual void requeue(int delta) { Q_UNUSED(delta) } // 把一步滚动放回队列（撤回后重新分步滚）
    virtual bool inject(int delta) { Q_UNUSED(delta) return false; } // 直接注入一次滚动，不经过队列
    // 缺少系统权限而无法接管时为 true（macOS 的“辅助功能”），界面据此提示用户授权；授权后 start() 即可成功
    virtual bool needsPermission() const { return false; }
    virtual void openPermissionSettings() {}
};

struct Platform {
    std::unique_ptr<IScreenCapturer> capturer;
    std::unique_ptr<IWindowFinder> windowFinder;
    std::unique_ptr<IHotkeyManager> hotkeys;
    std::unique_ptr<IWheelThrottle> wheelThrottle;
};

// 抽象工厂：由各平台的实现文件提供（CMake 按平台选择编译）
Platform createPlatform();

// 置顶窗口的层级。macOS 上 Qt 的 WindowStaysOnTopHint 盖不住菜单栏和全屏应用，需要平台单独设置
enum class FloatingLayer {
    Pin,     // 贴图：浮在普通窗口之上，跟随所有桌面空间
    Overlay, // 截图遮罩、长截图边框和面板：盖住菜单栏和 Dock
};
// 在窗口构造时调用一次；层级在每次显示时生效。Overlay 显示时会把本程序切到前台（除非窗口不接受焦点）
void setupFloatingWindow(QWidget* window, FloatingLayer layer);

// 长截图：边框和面板要摆在选区周围，还要每 50ms 截一次屏。Wayland 下两者都做不到
bool scrollCaptureSupported();

// 开机自启
bool autostartSupported();
bool autostartEnabled();
bool setAutostartEnabled(bool enabled);
