#include "app/appcontext.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>

namespace {
// 直接打开程序时发给正在运行的实例：请它退出，由新打开的这个接管（比如运行着的是旧版本）
const QByteArray kReplaceRequest = QByteArrayLiteral("__replace__");
constexpr int kReplaceWaitMs = 5000;

// 等正在运行的实例退出（它的本地服务消失）；超时返回 false
bool waitForServerGone(const QString& serverName)
{
    for (int waited = 0; waited < kReplaceWaitMs; waited += 100) {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (!probe.waitForConnected(100))
            return true;
        probe.disconnectFromServer();
        QThread::msleep(100);
    }
    return false;
}
} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SnapPin"));
    QApplication::setOrganizationName(QStringLiteral("SnapPin"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setQuitOnLastWindowClosed(false); // 托盘常驻

    // 命令行可以指定动作（如 SnapPin pin_clipboard），用于在系统设置里绑定快捷键（Wayland 下只能这样）
    QString action;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && !args[1].startsWith(QLatin1Char('-')))
        action = args[1];

    // 单实例。已有实例在运行时：
    // - 命令行带动作（快捷键命令）：把动作转给它后退出
    // - 直接打开程序：让它退出，由新打开的这个接管，保证运行的是刚点开的（最新的）那份程序
    const QString serverName = QStringLiteral("SnapPin-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(300)) {
            socket.write(action.isEmpty() ? kReplaceRequest : action.toUtf8());
            socket.waitForBytesWritten(300);
            if (!action.isEmpty())
                return 0;
            socket.waitForDisconnected(1000);
            if (!waitForServerGone(serverName))
                return 0; // 旧版本不认识替换请求，保留原来的实例
        }
    }

    AppContext ctx;
    ctx.start();

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&server, &ctx] {
        while (QLocalSocket* s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [s, &ctx] {
                // 排队执行：动作可能跑嵌套事件循环（如 Wayland 下等 portal 截图），
                // 期间客户端断开会删掉 s，不能还在 s 的信号里
                const QByteArray request = s->readAll().trimmed();
                if (request == kReplaceRequest) { // 又打开了一份程序：退出，让它接管
                    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
                    return;
                }
                const QString action = QString::fromUtf8(request);
                QTimer::singleShot(0, &ctx, [&ctx, action] { ctx.actions().trigger(action); });
            });
            QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        }
    });

    if (!action.isEmpty())
        QTimer::singleShot(0, &ctx, [&ctx, action] { ctx.actions().trigger(action); });

    return app.exec();
}
