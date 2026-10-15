#pragma once

#include "platform/platform.h"

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QVariantMap>

#include <xcb/xcb.h>

// Linux 有两套完全不同的显示协议，启动时按 Qt 的平台插件选择：
// - X11（xcb）：截屏、窗口识别、全局快捷键都能直接用 X 协议实现
// - Wayland：出于安全设计，程序无法读取屏幕和其他窗口，也不能抢全局按键。
//   截屏走 xdg-desktop-portal，快捷键需要用户在系统设置里绑定到命令（见 README）
// 坐标约定：X11 的原生坐标是物理像素；屏幕原生矩形 = 逻辑左上角 + 逻辑尺寸 × 缩放比例（Qt 的高 DPI 映射方式）

QRect nativeScreenRect(QScreen* screen);

// X11：枚举 _NET_CLIENT_LIST_STACKING，需要遵循 EWMH 的窗口管理器（GNOME、KDE、Xfce 等都支持）
class X11WindowFinder : public IWindowFinder {
public:
    explicit X11WindowFinder(xcb_connection_t* conn);
    QList<DetectedWindow> windowsOnScreen(QScreen* screen) override;

private:
    xcb_connection_t* m_conn;
};

// X11：在根窗口上 XGrabKey，按键事件经 Qt 的原生事件过滤器送达
class X11HotkeyManager : public IHotkeyManager, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    X11HotkeyManager(xcb_connection_t* conn, QObject* parent = nullptr);
    ~X11HotkeyManager() override;

    bool registerHotkey(const QString& actionId, const QKeySequence& seq) override;
    void unregisterAll() override;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    struct Grab {
        xcb_keycode_t keycode;
        uint16_t mods;
    };
    xcb_connection_t* m_conn;
    xcb_window_t m_root;
    QHash<quint32, QString> m_ids; // (keycode << 16 | mods) -> actionId
    QList<Grab> m_grabs;
};

// Wayland：程序拿不到全局按键
class WaylandHotkeyManager : public IHotkeyManager {
    Q_OBJECT
public:
    using IHotkeyManager::IHotkeyManager;
    bool registerHotkey(const QString&, const QKeySequence&) override { return false; }
    void unregisterAll() override {}
    QString unavailableReason() const override;
};

// Wayland：xdg-desktop-portal 的 Screenshot 接口，一次返回整个桌面，再按屏幕裁剪。
// 首次使用时桌面环境会弹窗询问是否允许
class PortalScreenCapturer : public QObject, public IScreenCapturer {
    Q_OBJECT
public:
    QPixmap grab(QScreen* screen) override;

signals:
    void responded();

private slots:
    void onResponse(uint response, const QVariantMap& results);

private:
    QImage requestDesktop();

    QImage m_desktop;          // 最近一次的整桌面截图
    QElapsedTimer m_desktopAge; // 同一次截图会话内连续 grab 多个屏幕时复用
    bool m_busy = false;
    bool m_waiting = false;
    uint m_response = 0;
    QString m_uri;
    int m_token = 0;
};
