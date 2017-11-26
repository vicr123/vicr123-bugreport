#ifndef API_H
#define API_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSettings>
#include <QSqlRecord>
#include <QSqlField>
#include <QSqlQuery>
#include <QSqlResult>
#include <QSqlDriver>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QTimer>
#include "ratelimit.h"
#include "socketapi.h"
#include "mail/mailsender.h"

#define SQL_CHECK if (q.lastError().type() != QSqlError::NoError) err(q.lastError().text() + "\n @ " + QString::number(__LINE__));
#define ASSERT_TABLE(table) if (!projectExists(table)) {rsp.statusCode = 404;return rsp;}

#define ASSERT_PRIVATE_BUG_ALLOW_READ(author, isAdmin) \
if (author == "") { \
    if (q.value("isprivate").toBool()) { \
        rsp.statusCode = 404; \
        return rsp; \
    } \
} else if (!isAdmin) { \
    if (q.value("author").toString() != author && q.value("isprivate").toBool()) { \
        rsp.statusCode = 404; \
        return rsp; \
    } \
}

struct Response;
struct Request;
class SocketApi;

class Api : public QObject
{
    Q_OBJECT
public:
    explicit Api(QObject *parent = nullptr);

signals:

public slots:
    Response processPath(Request req);

    bool projectExists(QString table);
    bool userExists(QString id);
    bool passwordMatch(QString password, QByteArray hashed, QString salt);

    QJsonArray comments(int table, QString id);
    void setComments(int table, QString id, QStringList commentIDs);
    void appendCommentId(int table, QString id, QString commentID);
    QSqlQuery user(QString token);
    QString getPictureUrl(QString email);

    QString getValidToken();
    QSqlQuery getBug(int projectId, QString bugId);
    QJsonObject extractBug(QSqlQuery q);
    QStringList getTotp(QByteArray key);
    QString getOneTotp(QByteArray key, qint64 t);
    QByteArray hmacSha1(QByteArray key, QByteArray baseString);

    int getProjectId(QString name);
    int getNumberOfBugs(int id);

private:
    QSqlDatabase db;
    QList<SocketApi*> sockets;

    QList<SocketApi*> listeningSockets(int projectId, QString bugId, int user = -1);

    QSettings settings;
};

#endif // API_H
