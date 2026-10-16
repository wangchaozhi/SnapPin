#include "appcontext.h"

#include "appicon.h"
#include "capture/capturesession.h"
#include "capture/scrollcapture.h"
#include "history/historystore.h"
#include "history/historywindow.h"
#include "imageutil.h"
#include "ocr/ocrwindow.h"
#include "ocr/textrecognizer.h"
#include "pin/pinmanager.h"
#include "pin/pinwindow.h"
#include "translate/overlayrender.h"
#include "translate/translator.h"
#include "settingsdialog.h"
#include "update/updater.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QUrl>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>

namespace {

constexpr int kPinHideDelayMs = 120; // 隐藏贴图后等 DWM 把它从屏幕上移除再截图
constexpr int kFirstUpdateCheckMs = 10 * 1000;          // 启动后稍等再检查，不拖慢开机
constexpr int kUpdateIntervalMs = 24 * 60 * 60 * 1000;  // 之后每天检查一次

} // namespace

AppContext::AppContext(QObject* parent)
    : QObject(parent)
    , m_platform(createPlatform())
    , m_settings(AppSettings::load())
    , m_pins(new PinManager(this))
    , m_history(new HistoryStore(this))
    , m_ocr(new TextRecognizer(this))
    , m_translator(new Translator(this))
    , m_updater(new Updater(this))
    , m_updateTimer(new QTimer(this))
{
    connect(m_pins, &PinManager::editRequested, this, &AppContext::editPin);
    connect(m_pins, &PinManager::ocrRequested, this, &AppContext::recognizeText);
    connect(m_pins, &PinManager::translateRequested, this,
            [this](PinWindow* pin) { translatePin(pin, false); });
    // 贴图穿透后收不到鼠标，提示怎么恢复
    connect(m_pins, &PinManager::clickThroughEnabled, this, [this] {
        const QString keys = m_settings.hotkeys.value(QStringLiteral("toggle_click_through"));
        const QString how = keys.isEmpty()
                                ? tr("从托盘菜单“切换贴图的鼠标穿透”恢复")
                                : tr("把光标移到贴图上按 %1 恢复").arg(QKeySequence(keys).toString(QKeySequence::NativeText));
        m_tray->showMessage(tr("SnapPin"), tr("贴图已开启鼠标穿透，%1").arg(how), QSystemTrayIcon::Information, 3000);
    });
    connect(m_updater, &Updater::notify, this, [this](const QString& title, const QString& text) {
        if (m_tray)
            m_tray->showMessage(title, text, QSystemTrayIcon::Information, 4000);
    });
    m_updateTimer->setInterval(kUpdateIntervalMs);
    connect(m_updateTimer, &QTimer::timeout, this, [this] { m_updater->check(false); });
}

AppContext::~AppContext()
{
    delete m_session;
    delete m_scrollSession;
    delete m_historyWindow;
}

void AppContext::start()
{
    setupActions();
    setupTray();
    connect(m_platform.hotkeys.get(), &IHotkeyManager::activated, this,
            [this](const QString& id) { m_actions.trigger(id); });
    applySettings();
}

void AppContext::setupActions()
{
    m_actions.add(QStringLiteral("capture"), tr("截图"), [this] { startCapture(); });
    m_actions.add(QStringLiteral("ocr_capture"), tr("截图识字"), [this] { startCapture(FinishAction::Ocr); });
    m_actions.add(QStringLiteral("translate_capture"), tr("截图翻译"), [this] { startCapture(FinishAction::Translate); });
    m_actions.add(QStringLiteral("pin_clipboard"), tr("贴图（剪贴板）"), [this] {
        if (!m_pins->pinClipboard() && m_tray)
            m_tray->showMessage(tr("SnapPin"), tr("剪贴板中没有图片或文字"), QSystemTrayIcon::Information, 2000);
    });
    m_actions.add(QStringLiteral("toggle_pins"), tr("隐藏/显示所有贴图"), [this] { m_pins->toggleVisible(); });
    m_actions.add(QStringLiteral("close_pins"), tr("关闭所有贴图"), [this] { m_pins->closeAll(); });
    m_actions.add(QStringLiteral("restore_pin"), tr("恢复关闭的贴图"), [this] {
        if (!m_pins->restoreClosed())
            m_tray->showMessage(tr("SnapPin"), tr("没有可恢复的贴图"), QSystemTrayIcon::Information, 2000);
    });
    m_actions.add(QStringLiteral("toggle_click_through"), tr("切换贴图的鼠标穿透"), [this] {
        switch (m_pins->toggleClickThroughAt(QCursor::pos())) {
        case PinManager::ClickThroughToggle::ClearedAll:
            m_tray->showMessage(tr("SnapPin"), tr("已取消所有贴图的鼠标穿透"), QSystemTrayIcon::Information, 2000);
            break;
        case PinManager::ClickThroughToggle::Nothing:
            m_tray->showMessage(tr("SnapPin"), tr("把光标移到贴图上再按，可以切换它的鼠标穿透"),
                                QSystemTrayIcon::Information, 2000);
            break;
        default:
            break;
        }
    });
    m_actions.add(QStringLiteral("history"), tr("截图历史"), [this] { openHistory(); });
    m_actions.add(QStringLiteral("settings"), tr("设置..."), [this] { openSettings(); });
    m_actions.add(QStringLiteral("check_update"), tr("检查更新..."), [this] { m_updater->check(true); });
    m_actions.add(QStringLiteral("quit"), tr("退出"), [] { QApplication::quit(); });
}

void AppContext::setupTray()
{
    m_trayMenu = std::make_unique<QMenu>();
    m_tray = new QSystemTrayIcon(makeAppIcon(m_settings.iconPalette), this);
    m_tray->setContextMenu(m_trayMenu.get());
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
#ifdef Q_OS_WIN
        // Windows 沿用 Snipaste 的习惯：左键截图、中键贴剪贴板、右键菜单
        if (r == QSystemTrayIcon::Trigger)
            m_actions.trigger(QStringLiteral("capture"));
        else if (r == QSystemTrayIcon::MiddleClick)
            m_actions.trigger(QStringLiteral("pin_clipboard"));
#else
        // macOS / Linux 按平台惯例：单击托盘图标只弹出菜单，截图用快捷键或菜单第一项。
        // macOS 上 Qt 自己会在左右键单击时弹出菜单；Linux 左键只上报 Trigger，要手动弹出
#ifndef Q_OS_MACOS
        if (r == QSystemTrayIcon::Trigger)
            m_trayMenu->popup(QCursor::pos());
#endif
        Q_UNUSED(r)
#endif
    });
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (!m_quickSavedFolder.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_quickSavedFolder));
        m_quickSavedFolder.clear();
    });
    m_tray->show();
}

void AppContext::rebuildTrayMenu()
{
    static const char* const kMenu[] = {"capture", "ocr_capture", "translate_capture", "pin_clipboard", nullptr,
                                        "history", "toggle_pins", "close_pins", "restore_pin", "toggle_click_through", nullptr,
                                        "settings", "check_update", "quit"};
    m_trayMenu->clear();
    for (const char* id : kMenu) {
        if (!id) {
            m_trayMenu->addSeparator();
            continue;
        }
        const QString actionId = QString::fromLatin1(id);
        // 鼠标穿透用快捷键切换；只有没设快捷键时才放进托盘菜单，免得穿透后没法恢复
        if (actionId == QLatin1String("toggle_click_through") && !m_settings.hotkeys.value(actionId).isEmpty())
            continue;
        QString text = m_actions.text(actionId);
        const QString keys = m_settings.hotkeys.value(actionId);
        if (!keys.isEmpty())
            text += QLatin1Char('\t') + QKeySequence(keys).toString(QKeySequence::NativeText);
        m_trayMenu->addAction(text, this, [this, actionId] { m_actions.trigger(actionId); });
    }

    const QString captureKeys = m_settings.hotkeys.value(QStringLiteral("capture"));
    m_tray->setToolTip(captureKeys.isEmpty()
                           ? QStringLiteral("SnapPin " APP_VERSION)
                           : tr("SnapPin %1\n单击或按 %2 截图，中键贴剪贴板").arg(QStringLiteral(APP_VERSION), captureKeys));
}

void AppContext::applySettings()
{
    m_platform.capturer->setBackend(m_settings.captureBackend);
    m_history->setEnabled(m_settings.historyEnabled);
    m_history->setMaxCount(m_settings.historyMax);
    m_translator->configure(m_settings);
    registerHotkeys();
    rebuildTrayMenu();
    PinWindow::setClickThroughShortcut(m_settings.hotkeys.value(QStringLiteral("toggle_click_through")));
    const QIcon icon = makeAppIcon(m_settings.iconPalette);
    m_tray->setIcon(icon);
    QApplication::setWindowIcon(icon);

    if (m_settings.autoUpdate && !m_updateTimer->isActive()) {
        QTimer::singleShot(kFirstUpdateCheckMs, m_updater, [this] { m_updater->check(false); });
        m_updateTimer->start();
    } else if (!m_settings.autoUpdate) {
        m_updateTimer->stop();
    }
}

void AppContext::registerHotkeys()
{
    m_platform.hotkeys->unregisterAll();
    const QString unavailable = m_platform.hotkeys->unavailableReason();
    if (!unavailable.isEmpty()) {
        m_tray->showMessage(tr("不支持全局快捷键"), unavailable, QSystemTrayIcon::Information, 8000);
        return;
    }
    QStringList failed;
    for (auto it = m_settings.hotkeys.cbegin(); it != m_settings.hotkeys.cend(); ++it) {
        if (it.value().isEmpty())
            continue;
        if (!m_platform.hotkeys->registerHotkey(it.key(), QKeySequence(it.value())))
            failed << QStringLiteral("%1（%2）").arg(it.value(), m_actions.text(it.key()));
    }
    if (!failed.isEmpty()) {
        m_tray->showMessage(tr("快捷键注册失败"),
                            tr("以下快捷键可能已被其他程序占用：\n%1").arg(failed.join(QLatin1Char('\n'))),
                            QSystemTrayIcon::Warning, 5000);
    }
}

void AppContext::openSettings()
{
    if (m_settingsOpen)
        return;
    m_settingsOpen = true;
    // 编辑快捷键时先注销全局快捷键，否则按下的组合键会被系统拦截
    m_platform.hotkeys->unregisterAll();

    SettingsDialog dlg(m_settings, m_actions, m_history->directory());
    if (dlg.exec() == QDialog::Accepted) {
        m_settings = dlg.settings();
        m_settings.save();
        dlg.saveSecrets();
        if (autostartSupported() && dlg.autostart() != autostartEnabled() && !setAutostartEnabled(dlg.autostart()))
            m_tray->showMessage(tr("设置"), tr("无法修改开机自启设置"), QSystemTrayIcon::Warning, 3000);
    }
    applySettings();
    m_settingsOpen = false;
}

void AppContext::openHistory()
{
    if (!m_historyWindow) {
        m_historyWindow = new HistoryWindow(m_history);
        connect(m_historyWindow, &HistoryWindow::pinRequested, this, &AppContext::pinImage);
    }
    m_historyWindow->show();
    m_historyWindow->raise();
    m_historyWindow->activateWindow();
}

CaptureSession* AppContext::newSession(PinWindow* editTarget, FinishAction copyAction)
{
    m_session = new CaptureSession(m_platform.capturer.get(), m_platform.windowFinder.get(), this);
    QPointer<PinWindow> target(editTarget);
    connect(m_session, &CaptureSession::finished, this,
            [this, target, copyAction](const QImage& image, const QRect& rect, FinishAction action) {
                if (action == FinishAction::Copy)
                    action = copyAction;
                // 延后到遮罩销毁后再处理，避免保存对话框在遮罩事件处理中弹出
                QTimer::singleShot(0, this, [this, image, rect, action, target] {
                    handleCaptureResult(image, rect, action, target);
                });
            });
    connect(m_session, &CaptureSession::closed, this, [target] {
        if (target) // 编辑结束（无论完成或取消）恢复原贴图
            target->show();
    });
    connect(m_session, &CaptureSession::closed, m_session, &QObject::deleteLater);
    return m_session;
}

void AppContext::startCapture(FinishAction copyAction)
{
    if (m_session || m_scrollSession || m_settingsOpen) // 已在截图中，或正在设置快捷键
        return;
    CaptureSession* session = newSession(nullptr, copyAction);
    session->setIncludeCursor(m_settings.captureCursor);
    session->start();
}

void AppContext::editPin(PinWindow* pin)
{
    if (m_session)
        return;
    const QImage image = pin->image();
    const QPoint pos = pin->imagePos();
    CaptureSession* session = newSession(pin);
    pin->hide();
    QPointer<CaptureSession> guard(session);
    QTimer::singleShot(kPinHideDelayMs, this, [guard, image, pos] {
        if (guard)
            guard->startEdit(image, pos);
    });
}

void AppContext::handleCaptureResult(const QImage& image, const QRect& globalRect, FinishAction action,
                                     PinWindow* editTarget)
{
    if (action == FinishAction::LongCapture) {
        startLongCapture(globalRect);
        return;
    }
    m_history->add(image);
    switch (action) {
    case FinishAction::LongCapture:
        break;
    case FinishAction::Copy:
        QGuiApplication::clipboard()->setImage(image);
        break;
    case FinishAction::Ocr:
        recognizeText(image);
        break;
    case FinishAction::Translate:
        translatePin(m_pins->pin(image, globalRect.topLeft()), true);
        break;
    case FinishAction::Save:
        saveImageInteractive(image);
        break;
    case FinishAction::QuickSave: {
        const QString path = quickSaveImage(image);
        if (path.isEmpty()) {
            m_tray->showMessage(tr("保存失败"), tr("无法写入快速保存目录：\n%1").arg(quickSaveDir()), QSystemTrayIcon::Warning, 4000);
        } else {
            m_quickSavedFolder = QFileInfo(path).absolutePath(); // 点击通知打开所在文件夹
            m_tray->showMessage(tr("已保存"), QDir::toNativeSeparators(path), QSystemTrayIcon::Information, 3000);
        }
        break;
    }
    case FinishAction::Pin:
        if (editTarget) {
            editTarget->setImage(image);
            editTarget->moveImageTo(globalRect.topLeft());
            editTarget->show();
            editTarget->activateWindow();
        } else {
            m_pins->pin(image, globalRect.topLeft());
        }
        break;
    }
}

void AppContext::recognizeText(const QImage& image)
{
    auto* window = new OcrWindow(m_translator);
    window->show();
    window->raise();
    window->activateWindow();
    m_ocr->recognize(image, window, [window](const OcrResult& r) { window->setResult(r); });
}

void AppContext::translatePin(PinWindow* pin, bool closeOnFail)
{
    // 原文在 m_original 里时（重新翻译）用原文，不翻译译文
    const QImage image = pin->hasTranslation() ? pin->originalImage() : pin->image();
    pin->setStatus(tr("正在识别文字…"));
    auto fail = [this, closeOnFail](PinWindow* pin, const QString& message) {
        pin->setStatus(QString());
        if (closeOnFail)
            pin->close();
        m_tray->showMessage(tr("翻译"), message, QSystemTrayIcon::Warning, 4000);
    };
    m_ocr->recognize(image, pin, [this, pin, image, fail](const OcrResult& r) {
        if (!r.error.isEmpty())
            return fail(pin, r.error);
        const QList<TextBlock> blocks = groupTextBlocks(r.fragments);
        if (blocks.isEmpty())
            return fail(pin, tr("没有识别到文字"));
        QStringList texts;
        for (const TextBlock& b : blocks)
            texts << b.text;
        pin->setStatus(m_translator->remoteHost().isEmpty()
                           ? tr("正在翻译…")
                           : tr("正在翻译（文字发送到 %1）…").arg(m_translator->remoteHost()));
        m_translator->translate(texts, Translator::targetLanguage(), pin, pin,
                                [pin, image, blocks, fail](const TranslateResult& t) {
                                    if (!t.error.isEmpty())
                                        return fail(pin, tr("翻译失败：%1").arg(t.error));
                                    pin->setStatus(QString());
                                    pin->showTranslation(renderTranslatedImage(image, blocks, t.texts));
                                });
    });
}

void AppContext::startLongCapture(const QRect& globalRect)
{
    if (m_scrollSession)
        return;
    QScreen* screen = QGuiApplication::screenAt(globalRect.center());
    if (!screen)
        return;
    m_scrollSession = new ScrollCaptureSession(m_platform.capturer.get(), m_platform.wheelThrottle.get(), screen,
                                               globalRect, this);
    connect(m_scrollSession, &ScrollCaptureSession::finished, this,
            [this](const QImage& image, const QRect& rect, FinishAction action) {
                QTimer::singleShot(0, this, [this, image, rect, action] { handleCaptureResult(image, rect, action, nullptr); });
            });
    connect(m_scrollSession, &ScrollCaptureSession::closed, m_scrollSession, &QObject::deleteLater);
    m_scrollSession->start();
}

void AppContext::pinImage(QImage image)
{
    const QPoint cursor = QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(cursor);
    image.setDevicePixelRatio(screen ? screen->devicePixelRatio() : 1.0); // 按物理像素 1:1 显示
    const QSize logical = (QSizeF(image.size()) / image.devicePixelRatio()).toSize();
    m_pins->pin(image, cursor - QPoint(logical.width() / 2, logical.height() / 2));
}
