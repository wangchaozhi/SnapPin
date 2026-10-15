#include "linuxplatform.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kReuseMs = 500;       // 同一次截图会话里给每个屏幕裁剪时复用同一张整桌面截图
constexpr int kTimeoutMs = 60000;   // 首次使用时桌面环境会弹窗征求同意，给用户留足时间

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");

QRect virtualDesktop()
{
    QRect r;
    for (QScreen* s : QGuiApplication::screens())
        r |= s->geometry();
    return r;
}

} // namespace

QImage PortalScreenCapturer::requestDesktop()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return {};

    // 先订阅 Request 对象的 Response 信号再发起调用，避免结果先于订阅到达
    const QString token = QStringLiteral("snappin%1").arg(++m_token);
    QString sender = bus.baseService().mid(1); // ":1.42" -> "1_42"
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString handle = kPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;
    bus.connect(kService, handle, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(onResponse(uint, QVariantMap)));

    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.portal.Screenshot"),
                                                       QStringLiteral("Screenshot"));
    call << QString() << QVariantMap{{QStringLiteral("handle_token"), token}, {QStringLiteral("interactive"), false}};
    m_waiting = true;
    m_uri.clear();
    const QDBusMessage reply = bus.call(call);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_waiting = false;
    } else {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(this, &PortalScreenCapturer::responded, &loop, &QEventLoop::quit);
        timeout.start(kTimeoutMs);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    bus.disconnect(kService, handle, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                   this, SLOT(onResponse(uint, QVariantMap)));
    if (m_waiting || m_response != 0 || m_uri.isEmpty()) {
        m_waiting = false;
        return {};
    }

    // portal 把截图存成文件（GNOME 存在“图片”目录里），读进来后删掉，避免留下垃圾文件
    const QString file = QUrl(m_uri).toLocalFile();
    QImage image(file);
    QFile::remove(file);
    return image.convertToFormat(QImage::Format_RGB32);
}

void PortalScreenCapturer::onResponse(uint response, const QVariantMap& results)
{
    m_response = response;
    m_uri = results.value(QStringLiteral("uri")).toString();
    m_waiting = false;
    emit responded();
}

QPixmap PortalScreenCapturer::grab(QScreen* screen)
{
    if (m_busy) // 等待 portal 时嵌套的事件循环里又触发了截图
        return {};
    // 失败（用户拒绝、超时）也复用，否则多屏时每个屏幕都会再弹一次授权框
    if (!m_desktopAge.isValid() || m_desktopAge.elapsed() > kReuseMs) {
        m_busy = true;
        m_desktop = requestDesktop();
        m_busy = false;
        m_desktopAge.start();
    }
    if (m_desktop.isNull())
        return screen->grabWindow(0); // portal 不可用：在大多数合成器上只能得到黑屏

    // portal 的整桌面图按逻辑布局排列，按比例换算出该屏的区域
    const QRect desk = virtualDesktop();
    const qreal sx = qreal(m_desktop.width()) / desk.width();
    const qreal sy = qreal(m_desktop.height()) / desk.height();
    const QRect g = screen->geometry().translated(-desk.topLeft());
    QImage part = m_desktop.copy(QRectF(g.x() * sx, g.y() * sy, g.width() * sx, g.height() * sy).toRect());
    const qreal dpr = screen->devicePixelRatio();
    const QSize want = (QSizeF(g.size()) * dpr).toSize();
    if (part.size() != want)
        part = part.scaled(want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPixmap pm = QPixmap::fromImage(std::move(part));
    pm.setDevicePixelRatio(dpr);
    return pm;
}
