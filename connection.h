#ifndef CONNECTION_H
#define CONNECTION_H

#include <QObject>
#include <QSslSocket>
#include <QHostAddress>
#include <QtConcurrent>
#include <QMimeDatabase>
#include "api/api.h"

class Connection;

struct Request {
    Request();
    void appendBuffer(QByteArray buffer);
    void processCompletedRequest();
    void timeout();

    bool invalid = false;
    QString version, path, method;
    QMap<QString, QString> headers;
    QByteArray body;

    QByteArray buffer;
    int bodyRead = 0;
    int bytesReceived = 0;
    Connection* connection;
    bool stopProcess = false;

    QByteArray requestContents;
};

class Connection : public QSslSocket
{
    Q_OBJECT
public:
    explicit Connection(QObject *parent = nullptr);
    QString remoteAddress();

    Request getRequest();
signals:

public slots:
    void initiate();
    void stopReading();
    void close();

private slots:
    void dataAvailable();

private:
    Request req;
};

struct Response {
    void WriteToConnection(Connection* connection, QString path) const;

    QMap<QString, QString> headers;
    QByteArray contents;
    QString errorExplanation;
    bool allowEmptyContents = false;
    int delay = 0;
    bool doNotWrite = false;
    bool doNotClose = false;

    int statusCode = 400;
};

#endif // CONNECTION_H
