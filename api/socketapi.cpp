#include "socketapi.h"

using namespace Functions;
extern Api* api;

SocketApi::SocketApi(Connection* conn, QObject *parent) : QObject(parent)
{
    this->conn = conn;
    conn->stopReading();
    connect(conn, SIGNAL(readyRead()), this, SLOT(dataAvailable()));
    connect(this, SIGNAL(closed()), this, SLOT(deleteLater()));
}

FourBits::FourBits() {

}

char FourBits::toChar() const {
    char ret = 0;
    if (a) ret += 0b00001000;
    if (b) ret += 0b00000100;
    if (c) ret += 0b00000010;
    if (d) ret += 0b00000001;
    return ret;
}


EightBits::EightBits() {

}

EightBits::EightBits(char ch) {
    a.a = ch & 0b10000000;
    a.b = ch & 0b01000000;
    a.c = ch & 0b00100000;
    a.d = ch & 0b00010000;
    b.a = ch & 0b00001000;
    b.b = ch & 0b00000100;
    b.c = ch & 0b00000010;
    b.d = ch & 0b00000001;
}

char EightBits::toChar() const {
    char ret = 0;
    if (a.a) ret += 0b10000000;
    if (a.b) ret += 0b01000000;
    if (a.c) ret += 0b00100000;
    if (a.d) ret += 0b00010000;
    if (b.a) ret += 0b00001000;
    if (b.b) ret += 0b00000100;
    if (b.c) ret += 0b00000010;
    if (b.d) ret += 0b00000001;
    return ret;
}

char EightBits::bitsToChar(bool a, bool b, bool c, bool d, bool e, bool f, bool g, bool h) {
    EightBits bits;
    bits.a.a = a;
    bits.a.b = b;
    bits.a.c = c;
    bits.a.d = d;
    bits.b.a = e;
    bits.b.b = f;
    bits.b.c = g;
    bits.b.d = h;
    return bits.toChar();
}

void SocketApi::dataAvailable() {
    QByteArray available = conn->readAll();
    QBuffer buf(&available);
    buf.open(QBuffer::ReadWrite);

    char c;
    EightBits bits;
    buf.getChar(&c);
    bits = c;

    if (bits.b != 0x0) {
        opcode = bits.b;
    }

    bool finalFrame = bits.a.a;

    if (bits.a.b != 0 || bits.a.c != 0 || bits.a.d != 0) {
        FORCE_CLOSE
    }

    buf.getChar(&c);
    EightBits len = c;
    if (len.a.a != 1) {
        FORCE_CLOSE
    }

    len.a.a = 0;
    char payloadLength = len.toChar();
    int length;

    if (payloadLength == 127) {

    } else if (payloadLength == 126) {
        //Read next 16 bits
        char a, b;
        buf.getChar(&a);
        buf.getChar(&b);
        length = (a << 8) | b;
    } else {
        length = payloadLength;
    }

    char mask[4];
    buf.getChar(&mask[0]);
    buf.getChar(&mask[1]);
    buf.getChar(&mask[2]);
    buf.getChar(&mask[3]);

    switch (opcode) {
        case 0x1:   //Text frame
        case 0x2: { //Binary frame
            QByteArray maskedData = buf.read(length);
            QByteArray data;

            for (int i = 0; i < maskedData.length(); i++) {
                data.append(maskedData.at(i) ^ mask[i % 4]);
            }

            buffer.append(data);
            if (finalFrame) {
                if (opcode == 0x1) { //Text frame
                    processText(buffer);
                } else { //Binary frame

                }
                buffer.clear();
            }
            break;
        }
        case 0x8: { //Close frame
            if (awaitingClose) {
                sendCloseFrame();
            }
            conn->disconnectFromHost();
            emit closed();
            break;
        }
        case 0x9: { //Ping frame
            QByteArray maskedData = buf.read(length);
            QByteArray data;

            for (int i = 0; i < maskedData.length(); i++) {
                data.append(maskedData.at(i) ^ mask[i % 4]);
            }

            sendPongFrame(data);
            break;
        }
        case 0x10: { //Pong frame
            break;
        }
    }
}

void SocketApi::processText(QString text) {
    if (text.startsWith("UPDATES PROJECT ")) {
        QString projectUpdate = text.mid(16);

        int projectId = api->getProjectId(projectUpdate);
        if (projectId == -1 && projectUpdate != "") {
            SEND_ERROR("Unknown Project")
        }

        if (projectUpdate == "") {
            projectId = -1;
        }

        currentProject = projectId;
        currentProjectName = projectUpdate;
        currentBug = "";
        sendTextFrame("OK\nSubscribed to updates for project " + projectUpdate);
    } else if (text.startsWith("UPDATES BUG ")) {
        QString bug = text.mid(12);
        if (currentProject == -1) {
            SEND_ERROR("No current project")
        }

        QSqlQuery q = api->getBug(currentProject, bug);
        if (!q.next()) {
            SEND_ERROR("Unknown Bug in project " + currentProjectName)
        }
        if (authenticatedUser == -1) {
            if (q.value("isprivate").toBool()) {
                SEND_ERROR("Unknown Bug in project " + currentProjectName)
            }
        } else if (!userIsAdmin) {
            if (q.value("author").toInt() != authenticatedUser && q.value("isprivate").toBool()) {
                SEND_ERROR("Unknown Bug in project " + currentProjectName)
            }
        }

        currentBug = bug;
        sendTextFrame("OK\nSubscribed to updates for bug #" + bug + " in project " + currentProjectName);
    } else if (text.startsWith("AUTHENTICATE ")) {
        QString token = text.mid(13);

        //Get user
        QSqlQuery q = api->user(token);
        if (!q.next()) {
            QTimer::singleShot(1000, [=] {
                SEND_ERROR("Unable to authenticate")
            });
        }

        authenticatedUser = q.value("id").toInt();
        userIsAdmin = q.value("isAdmin").toBool();
        sendTextFrame("OK\nAuthenticated as user " + QString::number(authenticatedUser));
    } else if (text == "DEAUTHENTICATE") {
        authenticatedUser = -1;
        sendTextFrame("OK\nDeauthenticated");
    } else {
        SEND_ERROR("Unknown Command")
    }
}

void SocketApi::sendTextFrame(QString text) {
    QByteArray frame;
    QByteArray data = text.toUtf8();
    qulonglong textLength = data.length();

    //bool fin = true;
    /*if (textLength <= 0xFFFFFFFFFFFFFFFF) {
        fin = true;
    }*/
    frame.append(EightBits::bitsToChar(/*fin*/ 1, 0, 0, 0, 0, 0, 0, 1)); //FIN, RSV1, RSV2, RSV3, Opcode

    EightBits len;
    len.a.a = 0; //Mask

    //Determine text length
    if (textLength < 126) {
        len.a.b = textLength & 0b1000000;
        len.a.c = textLength & 0b0100000;
        len.a.d = textLength & 0b0010000;
        len.b.a = textLength & 0b0001000;
        len.b.b = textLength & 0b0000100;
        len.b.c = textLength & 0b0000010;
        len.b.d = textLength & 0b0000001;
        frame.append(len);
    } else {
        len.a.b = 1;
        len.a.c = 1;
        len.a.d = 1;
        len.b.a = 1;
        len.b.b = 1;
        len.b.c = 1;

        if (textLength <= 0xFFFF) {
            len.b.d = 0;
            frame.append(len);

            //Continuation frames
            char a = textLength >> 8;
            char b = (textLength & 0x00FF);
            frame.append(a);
            frame.append(b);
        } else {
            len.b.d = 1;
            frame.append(len);

            //Continuation frames
            //EightBits first(textLength >> 512);
            //first.a.a = 0;
            char a = 0; //first;
            char b = 0; //(textLength & 0x00FF000000000000) >> 256;
            char c = 0; //(textLength & 0x0000FF0000000000) >> 128;
            char d = 0; //(textLength & 0x000000FF00000000) >> 64;
            char e = (textLength & 0x00000000FF000000) >> 32;
            char f = (textLength & 0x0000000000FF0000) >> 16;
            char g = (textLength & 0x000000000000FF00) >> 8;
            char h = (textLength & 0x00000000000000FF);

            frame.append(a);
            frame.append(b);
            frame.append(c);
            frame.append(d);
            frame.append(e);
            frame.append(f);
            frame.append(g);
            frame.append(h);
        }
    }

    if (/*fin*/ 1) {
        //Write payload data
        frame.append(data);
        conn->write(frame);
    } else {
        //A continuation frame is needed
        //but we probably don't need to implement this
    }
}

void SocketApi::sendCloseFrame() {
    QByteArray frame;
    frame.append((char) 0b10001000); //FIN, RSV1, RSV2, RSV3, Opcode
    frame.append((char) 0b00000000); //Mask, Length

    //Write payload data
    conn->write(frame);
    awaitingClose = true;
}

void SocketApi::sendPongFrame(QByteArray payload) {
    QByteArray frame;
    frame.append((char) 0b10001001); //FIN, RSV1, RSV2, RSV3, Opcode

    EightBits len;
    len.a.a = 0;
    len.a.b = payload.length() & 0b1000000;
    len.a.c = payload.length() & 0b0100000;
    len.a.d = payload.length() & 0b0010000;
    len.b.a = payload.length() & 0b0001000;
    len.b.b = payload.length() & 0b0000100;
    len.b.c = payload.length() & 0b0000010;
    len.b.d = payload.length() & 0b0000001;
    frame.append(len);

    frame.append(payload);

    //Write payload data
    conn->write(frame);
}

void SocketApi::sendPingFrame(QByteArray payload) {
    QByteArray frame;
    frame.append((char) 0b10001010); //FIN, RSV1, RSV2, RSV3, Opcode

    EightBits len;
    len.a.a = 0;
    len.a.b = payload.length() & 0b1000000;
    len.a.c = payload.length() & 0b0100000;
    len.a.d = payload.length() & 0b0010000;
    len.b.a = payload.length() & 0b0001000;
    len.b.b = payload.length() & 0b0000100;
    len.b.c = payload.length() & 0b0000010;
    len.b.d = payload.length() & 0b0000001;
    frame.append(len);

    frame.append(payload);

    //Write payload data
    conn->write(frame);
}

QString SocketApi::bug() {
    return currentBug;
}

int SocketApi::project() {
    return currentProject;
}

int SocketApi::user() {
    return authenticatedUser;
}
