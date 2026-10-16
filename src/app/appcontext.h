#pragma once

#include "actionregistry.h"
#include "appsettings.h"
#include "capture/overlaywindow.h"
#include "platform/platform.h"

#include <QObject>
#include <QPointer>
#include <memory>

class CaptureSession;
class ScrollCaptureSession;
class HistoryStore;
class HistoryWindow;
class PinManager;
class PinWindow;
class QMenu;
class QSystemTrayIcon;
class QTimer;
class Updater;

// 应用上下文：持有平台实现与各管理器，代替全局单例
class AppContext : public QObject {
    Q_OBJECT
public:
    explicit AppContext(QObject* parent = nullptr);
    ~AppContext() override;

    void start();
    const ActionRegistry& actions() const { return m_actions; }

private:
    void setupActions();
    void setupTray();
    void rebuildTrayMenu(); // 菜单项后附带当前快捷键，设置变化后重建
    void applySettings();
    void registerHotkeys();
    void openSettings();
    void openHistory();

    // editTarget 非空表示编辑该贴图：选择“贴图”时替换它的内容
    CaptureSession* newSession(PinWindow* editTarget = nullptr);
    void startCapture();
    void editPin(PinWindow* pin);
    void handleCaptureResult(const QImage& image, const QRect& globalRect, FinishAction action, PinWindow* editTarget);
    void startLongCapture(const QRect& globalRect);
    void pinImage(QImage image);

    Platform m_platform;
    ActionRegistry m_actions;
    AppSettings m_settings;
    PinManager* m_pins;
    HistoryStore* m_history;
    Updater* m_updater;
    QTimer* m_updateTimer;
    QSystemTrayIcon* m_tray = nullptr;
    std::unique_ptr<QMenu> m_trayMenu;
    QPointer<CaptureSession> m_session;
    QPointer<ScrollCaptureSession> m_scrollSession;
    QPointer<HistoryWindow> m_historyWindow;
    bool m_settingsOpen = false;
    QString m_quickSavedFolder; // 快速保存后点击托盘通知打开这个文件夹
};
