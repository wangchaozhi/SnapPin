#include "linuxplatform.h"

#include <QScreen>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {

xcb_atom_t atom(xcb_connection_t* conn, const char* name)
{
    xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(conn, xcb_intern_atom(conn, 0, uint16_t(strlen(name)), name), nullptr);
    const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return a;
}

// 读取 32 位的数组属性（CARDINAL / WINDOW / ATOM）
QList<uint32_t> property32(xcb_connection_t* conn, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type, uint32_t maxItems = 4096)
{
    QList<uint32_t> out;
    xcb_get_property_reply_t* r = xcb_get_property_reply(conn, xcb_get_property(conn, 0, w, prop, type, 0, maxItems), nullptr);
    if (r && r->format == 32) {
        const auto* data = static_cast<const uint32_t*>(xcb_get_property_value(r));
        const int n = xcb_get_property_value_length(r) / 4;
        out.reserve(n);
        for (int i = 0; i < n; ++i)
            out.append(data[i]);
    }
    free(r);
    return out;
}

struct Atoms {
    xcb_atom_t stacking, pid, frameExtents, gtkFrameExtents, windowType, typeDesktop, state, stateHidden;

    explicit Atoms(xcb_connection_t* c)
        : stacking(atom(c, "_NET_CLIENT_LIST_STACKING"))
        , pid(atom(c, "_NET_WM_PID"))
        , frameExtents(atom(c, "_NET_FRAME_EXTENTS"))
        , gtkFrameExtents(atom(c, "_GTK_FRAME_EXTENTS"))
        , windowType(atom(c, "_NET_WM_WINDOW_TYPE"))
        , typeDesktop(atom(c, "_NET_WM_WINDOW_TYPE_DESKTOP"))
        , state(atom(c, "_NET_WM_STATE"))
        , stateHidden(atom(c, "_NET_WM_STATE_HIDDEN"))
    {
    }
};

// 窗口在根窗口坐标系中的外框（含窗口管理器的标题栏，不含 GTK 客户端阴影），物理像素
QRect frameRect(xcb_connection_t* conn, xcb_window_t root, xcb_window_t w, const Atoms& a)
{
    xcb_get_geometry_reply_t* g = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, w), nullptr);
    xcb_translate_coordinates_reply_t* t =
        xcb_translate_coordinates_reply(conn, xcb_translate_coordinates(conn, w, root, 0, 0), nullptr);
    QRect r;
    if (g && t)
        r = QRect(t->dst_x, t->dst_y, g->width, g->height);
    free(g);
    free(t);
    if (r.isEmpty())
        return r;
    const QList<uint32_t> frame = property32(conn, w, a.frameExtents, XCB_ATOM_CARDINAL, 4);
    if (frame.size() == 4) // left, right, top, bottom
        r.adjust(-int(frame[0]), -int(frame[2]), int(frame[1]), int(frame[3]));
    const QList<uint32_t> shadow = property32(conn, w, a.gtkFrameExtents, XCB_ATOM_CARDINAL, 4);
    if (shadow.size() == 4)
        r.adjust(int(shadow[0]), int(shadow[2]), -int(shadow[1]), -int(shadow[3]));
    return r;
}

bool skipWindow(xcb_connection_t* conn, xcb_window_t w, const Atoms& a)
{
    xcb_get_window_attributes_reply_t* attrs =
        xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, w), nullptr);
    const bool viewable = attrs && attrs->map_state == XCB_MAP_STATE_VIEWABLE;
    free(attrs);
    if (!viewable) // 最小化或在其他工作区
        return true;
    const QList<uint32_t> pid = property32(conn, w, a.pid, XCB_ATOM_CARDINAL, 1);
    if (!pid.isEmpty() && pid[0] == uint32_t(getpid()))
        return true;
    if (property32(conn, w, a.windowType, XCB_ATOM_ATOM).contains(a.typeDesktop))
        return true;
    return property32(conn, w, a.state, XCB_ATOM_ATOM).contains(a.stateHidden);
}

} // namespace

X11WindowFinder::X11WindowFinder(xcb_connection_t* conn)
    : m_conn(conn)
{
}

QList<DetectedWindow> X11WindowFinder::windowsOnScreen(QScreen* screen)
{
    const Atoms a(m_conn);
    const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(m_conn)).data->root;
    const QList<uint32_t> stacking = property32(m_conn, root, a.stacking, XCB_ATOM_WINDOW); // 从下到上

    const QRect phys = nativeScreenRect(screen);
    const qreal dpr = screen->devicePixelRatio();
    QList<DetectedWindow> result;
    int group = 0;
    for (auto it = stacking.crbegin(); it != stacking.crend(); ++it) {
        if (skipWindow(m_conn, *it, a))
            continue;
        const QRect r = frameRect(m_conn, root, *it, a);
        if (r.width() < 4 || r.height() < 4)
            continue;
        const QRect clipped = r & phys;
        if (!clipped.isEmpty()) {
            const QRect local = QRectF((clipped.x() - phys.x()) / dpr, (clipped.y() - phys.y()) / dpr,
                                       clipped.width() / dpr, clipped.height() / dpr).toRect();
            result.append({local, group, false});
        }
        ++group; // 按全局 Z 序编号，与屏幕无关
    }
    return result;
}
