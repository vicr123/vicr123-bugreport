#include "server.h"

using namespace Functions;

extern QSslKey privateKey;
extern QString certificatePath;

Server::Server(int port, bool useSsl, QObject *parent) : QTcpServer(parent)
{
    this->port = port;
    this->useSsl = useSsl;
}

void Server::start() {
    log("Starting server on port " + QString::number(port));

    connect(this, SIGNAL(newConnection()), this, SLOT(newConnection()));

    if (this->listen(QHostAddress::Any, port)) {
        good("Server initialized. Now listening for HTTP requests.");
    } else {
        err("Server didn't start properly.");
    }
}

void Server::newConnection() {
    //Connection* socket = new Connection((QSslSocket*) server.nextPendingConnection());
    this->nextPendingConnection();
}

void Server::incomingConnection(qintptr handle) {
    Connection* c = new Connection();
    if (c->setSocketDescriptor(handle)) {
        addPendingConnection(c);
        connect(c, static_cast<void(QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
            [=](QAbstractSocket::SocketError socketError){
            if (socketError == QSslSocket::SslHandshakeFailedError) {
                warn(c->remoteAddress() + ": SSL Handshake failure");
            } else if (socketError == QSslSocket::RemoteHostClosedError) {
            } else {
                warn(c->remoteAddress() + ": Socket Error " + QString::number(socketError));
            }
        });
        connect(c, static_cast<void(QSslSocket::*)(const QList<QSslError> &)>(&QSslSocket::sslErrors),
            [=](const QList<QSslError> &errors){
            for (QSslError error : errors) {
                warn(c->remoteAddress() + ": SSL Error " + error.errorString());
            }
        });

        if (useSsl) {
            c->setPrivateKey(privateKey);
            c->setLocalCertificate(certificatePath);
            c->setPeerVerifyMode(QSslSocket::VerifyNone);
            c->startServerEncryption();
        }

        c->initiate();
    }
}
