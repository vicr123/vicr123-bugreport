#ifndef SOCKETAPI_H
#define SOCKETAPI_H

#include <QObject>
#include "connection.h"
#include "functions.h"
#include "api.h"

#define FORCE_CLOSE sendCloseFrame();conn->disconnectFromHost();return;
#define SEND_ERROR(reason) sendTextFrame("ERROR\n" + QString(reason));return;

class Connection;

class FourBits {
public:
    FourBits();
    FourBits(bool a, bool b, bool c, bool d);

    char toChar() const;

    bool a;
    bool b;
    bool c;
    bool d;

    operator char() const {
        return toChar();
    }
};

class EightBits {
public:
    EightBits();
    EightBits(FourBits a, FourBits b);
    EightBits(char ch);

    char toChar() const;

    FourBits a;
    FourBits b;

    static char bitsToChar(bool a, bool b, bool c, bool d, bool e, bool f, bool g, bool h);

    operator char() const {
        return toChar();
    }
};

class SocketApi : public QObject
{
    Q_OBJECT
public:
    explicit SocketApi(Connection* conn, QObject *parent = nullptr);

signals:
    void closed();

public slots:
    void dataAvailable();
    void sendTextFrame(QString text);
    void sendPongFrame(QByteArray payload = QByteArray());
    void sendPingFrame(QByteArray payload = QByteArray());
    void sendCloseFrame();

    void processText(QString text);

    int project();
    QString bug();
    int user();

private:
    Connection* conn;
    bool awaitingClose = false;
    QByteArray buffer;

    FourBits opcode;

    int currentProject = -1;
    QString currentBug = "";
    QString currentProjectName;
    int authenticatedUser = -1;
    bool userIsAdmin = false;
};

#endif // SOCKETAPI_H
