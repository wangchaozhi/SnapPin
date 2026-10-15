#include "linuxplatform.h"

#include <QCoreApplication>
#include <cstdlib>

namespace {

// 锁定键会改变按键事件的修饰位，每个快捷键都要连同 CapsLock / NumLock 的组合一起注册
constexpr uint16_t kNumLock = XCB_MOD_MASK_2;
constexpr uint16_t kLockVariants[] = {0, XCB_MOD_MASK_LOCK, kNumLock, XCB_MOD_MASK_LOCK | kNumLock};
constexpr uint16_t kRelevantMods = XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_4;

uint32_t toKeysym(Qt::Key key)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return 0xffbe + uint32_t(key - Qt::Key_F1); // XK_F1
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return 'a' + uint32_t(key - Qt::Key_A); // 小写字母的 keysym 与 ASCII 一致
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return uint32_t(key);
    switch (key) {
    case Qt::Key_Print: return 0xff61;
    case Qt::Key_Space: return 0x20;
    case Qt::Key_Insert: return 0xff63;
    case Qt::Key_Delete: return 0xffff;
    case Qt::Key_Home: return 0xff50;
    case Qt::Key_End: return 0xff57;
    case Qt::Key_PageUp: return 0xff55;
    case Qt::Key_PageDown: return 0xff56;
    case Qt::Key_Pause: return 0xff13;
    default: return 0;
    }
}

xcb_keycode_t toKeycode(xcb_connection_t* conn, uint32_t keysym)
{
    const xcb_setup_t* setup = xcb_get_setup(conn);
    const int count = setup->max_keycode - setup->min_keycode + 1;
    xcb_get_keyboard_mapping_reply_t* r = xcb_get_keyboard_mapping_reply(
        conn, xcb_get_keyboard_mapping(conn, setup->min_keycode, uint8_t(count)), nullptr);
    if (!r)
        return 0;
    const xcb_keysym_t* syms = xcb_get_keyboard_mapping_keysyms(r);
    const int perCode = r->keysyms_per_keycode;
    xcb_keycode_t found = 0;
    for (int i = 0; i < count && !found; ++i) {
        for (int j = 0; j < perCode; ++j) {
            if (syms[i * perCode + j] == keysym) {
                found = xcb_keycode_t(setup->min_keycode + i);
                break;
            }
        }
    }
    free(r);
    return found;
}

quint32 grabKey(xcb_keycode_t code, uint16_t mods)
{
    return quint32(code) << 16 | mods;
}

} // namespace

X11HotkeyManager::X11HotkeyManager(xcb_connection_t* conn, QObject* parent)
    : IHotkeyManager(parent)
    , m_conn(conn)
    , m_root(xcb_setup_roots_iterator(xcb_get_setup(conn)).data->root)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

X11HotkeyManager::~X11HotkeyManager()
{
    unregisterAll();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool X11HotkeyManager::registerHotkey(const QString& actionId, const QKeySequence& seq)
{
    if (seq.isEmpty())
        return false;
    const QKeyCombination combo = seq[0];
    const uint32_t keysym = toKeysym(combo.key());
    const xcb_keycode_t code = keysym ? toKeycode(m_conn, keysym) : 0;
    if (!code)
        return false;

    uint16_t mods = 0;
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    if (m & Qt::ShiftModifier) mods |= XCB_MOD_MASK_SHIFT;
    if (m & Qt::ControlModifier) mods |= XCB_MOD_MASK_CONTROL;
    if (m & Qt::AltModifier) mods |= XCB_MOD_MASK_1;
    if (m & Qt::MetaModifier) mods |= XCB_MOD_MASK_4; // Super / Win 键

    QList<Grab> done;
    for (uint16_t lock : kLockVariants) {
        const xcb_void_cookie_t c = xcb_grab_key_checked(m_conn, 1, m_root, mods | lock, code,
                                                         XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        if (xcb_generic_error_t* err = xcb_request_check(m_conn, c)) { // BadAccess：已被其他程序抢占
            free(err);
            for (const Grab& g : std::as_const(done))
                xcb_ungrab_key(m_conn, g.keycode, m_root, g.mods);
            xcb_flush(m_conn);
            return false;
        }
        done.append({code, uint16_t(mods | lock)});
    }
    m_grabs += done;
    m_ids.insert(grabKey(code, mods), actionId);
    return true;
}

void X11HotkeyManager::unregisterAll()
{
    for (const Grab& g : std::as_const(m_grabs))
        xcb_ungrab_key(m_conn, g.keycode, m_root, g.mods);
    xcb_flush(m_conn);
    m_grabs.clear();
    m_ids.clear();
}

bool X11HotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    if (eventType != "xcb_generic_event_t")
        return false;
    const auto* ev = static_cast<const xcb_generic_event_t*>(message);
    if ((ev->response_type & ~0x80) != XCB_KEY_PRESS)
        return false;
    const auto* key = reinterpret_cast<const xcb_key_press_event_t*>(ev);
    if (key->event != m_root)
        return false; // 普通窗口的按键交给 Qt
    const auto it = m_ids.constFind(grabKey(key->detail, key->state & kRelevantMods));
    if (it == m_ids.cend())
        return false;
    emit activated(*it);
    return true;
}
