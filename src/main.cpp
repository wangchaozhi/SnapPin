#include "app/appcontext.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SnapPin"));
    QApplication::setOrganizationName(QStringLiteral("SnapPin"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setQuitOnLastWindowClosed(false); // 托盘常驻

    // 单实例：已有实例在运行时，通知它截图后退出
    const QString serverName = QStringLiteral("SnapPin-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(300)) {
            socket.write("capture");
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

    return app.exec();
}
