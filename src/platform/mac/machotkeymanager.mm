#include "macplatform.h"

#include <Carbon/Carbon.h>

namespace {

constexpr OSType kSignature = 'SNPP';

UInt32 toKeyCode(Qt::Key key)
{
    static const UInt32 fkeys[] = {kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8, kVK_F9, kVK_F10,
                                   kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20};
    static const UInt32 letters[] = {kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F, kVK_ANSI_G,
                                     kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
                                     kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U,
                                     kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z};
    static const UInt32 digits[] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
                                    kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
    if (key >= Qt::Key_F1 && key <= Qt::Key_F20)
        return fkeys[key - Qt::Key_F1];
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return letters[key - Qt::Key_A];
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return digits[key - Qt::Key_0];
    switch (key) {
    case Qt::Key_Print: return kVK_F13; // Mac 键盘没有 PrintScreen，外接 PC 键盘上它通常映射为 F13
    case Qt::Key_Space: return kVK_Space;
    case Qt::Key_Delete: return kVK_ForwardDelete;
    case Qt::Key_Home: return kVK_Home;
    case Qt::Key_End: return kVK_End;
    case Qt::Key_PageUp: return kVK_PageUp;
    case Qt::Key_PageDown: return kVK_PageDown;
    default: return UINT32_MAX;
    }
}

OSStatus onHotkey(EventHandlerCallRef, EventRef event, void* userData)
{
    EventHotKeyID hk{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hk), nullptr, &hk) != noErr
        || hk.signature != kSignature)
        return eventNotHandledErr;
    static_cast<MacHotkeyManager*>(userData)->dispatch(hk.id);
    return noErr;
}

} // namespace

MacHotkeyManager::MacHotkeyManager(QObject* parent)
    : IHotkeyManager(parent)
{
    const EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
    EventHandlerRef handler = nullptr;
    InstallApplicationEventHandler(&onHotkey, 1, &spec, this, &handler);
    m_handler = handler;
}

MacHotkeyManager::~MacHotkeyManager()
{
    unregisterAll();
    if (m_handler)
        RemoveEventHandler(EventHandlerRef(m_handler));
}

bool MacHotkeyManager::registerHotkey(const QString& actionId, const QKeySequence& seq)
{
    if (seq.isEmpty() || !m_handler)
        return false;
    const QKeyCombination combo = seq[0];
    const UInt32 keyCode = toKeyCode(combo.key());
    if (keyCode == UINT32_MAX)
        return false;

    // Qt 在 macOS 上把 Command 映射为 ControlModifier，Control 映射为 MetaModifier
    UInt32 mods = 0;
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    if (m & Qt::ControlModifier) mods |= cmdKey;
    if (m & Qt::MetaModifier) mods |= controlKey;
    if (m & Qt::AltModifier) mods |= optionKey;
    if (m & Qt::ShiftModifier) mods |= shiftKey;

    const quint32 id = m_nextId++;
    EventHotKeyRef ref = nullptr;
    if (RegisterEventHotKey(keyCode, mods, EventHotKeyID{kSignature, id}, GetApplicationEventTarget(), 0, &ref) != noErr)
        return false;
    m_ids.insert(id, actionId);
    m_refs.insert(id, ref);
    return true;
}

void MacHotkeyManager::unregisterAll()
{
    for (void* ref : std::as_const(m_refs))
        UnregisterEventHotKey(EventHotKeyRef(ref));
    m_refs.clear();
    m_ids.clear();
}

void MacHotkeyManager::dispatch(quint32 id)
{
    const auto it = m_ids.constFind(id);
    if (it != m_ids.cend())
        emit activated(*it);
}
