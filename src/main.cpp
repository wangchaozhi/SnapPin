#include "app/appcontext.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

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

    // 单实例：已有实例在运行时，把动作（默认截图）转给它后退出
    const QString serverName = QStringLiteral("SnapPin-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(300)) {
            socket.write(action.isEmpty() ? QByteArray("capture") : action.toUtf8());
            socket.waitForBytesWritten(300);
            return 0;
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
                ctx.actions().trigger(QString::fromUtf8(s->readAll()).trimmed());
            });
            QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        }
    });

    if (!action.isEmpty())
        QTimer::singleShot(0, &ctx, [&ctx, action] { ctx.actions().trigger(action); });

    return app.exec();
}
