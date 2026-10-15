// 其他平台（Linux 等）的临时实现：用 Qt 自带接口截屏，暂不支持窗口识别、全局快捷键和开机自启。
// 后续替换为 XShm/xdg-desktop-portal 等原生实现。
#include "platform/platform.h"

#include <QScreen>

namespace {

class QtScreenCapturer : public IScreenCapturer {
public:
    QPixmap grab(QScreen* screen) override { return screen->grabWindow(0); }
};

class NullWindowFinder : public IWindowFinder {
public:
    QList<DetectedWindow> windowsOnScreen(QScreen*) override { return {}; }
};

class NullHotkeyManager : public IHotkeyManager {
public:
    bool registerHotkey(const QString&, const QKeySequence&) override { return false; }
    void unregisterAll() override {}
};

} // namespace

Platform createPlatform()
{
    Platform p;
    p.capturer = std::make_unique<QtScreenCapturer>();
    p.windowFinder = std::make_unique<NullWindowFinder>();
    p.hotkeys = std::make_unique<NullHotkeyManager>();
    p.wheelThrottle = std::make_unique<IWheelThrottle>(); // 不接管滚轮
    return p;
}

void setupFloatingWindow(QWidget*, FloatingLayer) {}

bool autostartSupported() { return false; }
bool autostartEnabled() { return false; }
bool setAutostartEnabled(bool) { return false; }
