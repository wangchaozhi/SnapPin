#include "winplatform.h"

#include <QCoreApplication>

#include <windows.h>

namespace {

UINT toVirtualKey(Qt::Key key)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return VK_F1 + (key - Qt::Key_F1);
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        return UINT(key); // 与 ASCII 一致
    switch (key) {
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Pause: return VK_PAUSE;
    default: return 0;
    }
}

} // namespace

WinHotkeyManager::WinHotkeyManager(QObject* parent)
    : IHotkeyManager(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WinHotkeyManager::~WinHotkeyManager()
{
    unregisterAll();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool WinHotkeyManager::registerHotkey(const QString& actionId, const QKeySequence& seq)
{
    if (seq.isEmpty())
        return false;
    const QKeyCombination combo = seq[0];
    const UINT vk = toVirtualKey(combo.key());
    if (!vk)
        return false;

    UINT mods = MOD_NOREPEAT;
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    if (m & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (m & Qt::AltModifier) mods |= MOD_ALT;
    if (m & Qt::ShiftModifier) mods |= MOD_SHIFT;
    if (m & Qt::MetaModifier) mods |= MOD_WIN;

    const int id = m_nextId++;
    // hwnd 为空：WM_HOTKEY 投递到线程消息队列，由 Qt 事件循环交给 nativeEventFilter
    if (!RegisterHotKey(nullptr, id, mods, vk))
        return false;
    m_ids.insert(id, actionId);
    return true;
}

void WinHotkeyManager::unregisterAll()
{
    for (auto it = m_ids.cbegin(); it != m_ids.cend(); ++it)
        UnregisterHotKey(nullptr, it.key());
    m_ids.clear();
}

bool WinHotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY)
        return false;
    const auto it = m_ids.constFind(int(msg->wParam));
    if (it == m_ids.cend())
        return false;
    emit activated(*it);
    return true;
}
