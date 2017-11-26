#ifndef MAILSENDER_H
#define MAILSENDER_H

#include <QObject>
#include <QSettings>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include "functions.h"

struct MailMessage {
    MailMessage();
    MailMessage(QString templ, QStringList args = QStringList());

    QString html;
    QMap<QString, QString> to;
    QString subject;
};

class MailSender : public QObject
{
        Q_OBJECT
    public:
        explicit MailSender(QObject *parent = nullptr);

    signals:

    public slots:
        void send(MailMessage m);
        void send(QList<MailMessage> m);

    private:
        QNetworkAccessManager* mgr;
        QSettings settings;
};

#endif // MAILSENDER_H
