#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QTcpServer>
#include <QSslKey>
#include "connection.h"

class Server : public QTcpServer
{
    Q_OBJECT
public:
    explicit Server(int port, bool useSsl, QObject *parent = nullptr);

signals:

public slots:
    void start();

private slots:
    void newConnection();

private:
    int port;
    bool useSsl;

    void incomingConnection(qintptr handle);
};

#endif // SERVER_H
