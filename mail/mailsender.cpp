#include "mailsender.h"

using namespace Functions;

MailMessage::MailMessage() {

}

MailMessage::MailMessage(QString templ, QStringList args) {
    QSettings settings;
    QFile f(settings.value("mail/mailMessages").toString() + "/" + templ + ".html");
    f.open(QFile::ReadOnly);

    html = f.readAll();
    for (QString a : args) {
        html = html.arg(a);
    }

    f.close();
}

MailSender::MailSender(QObject *parent) : QObject(parent)
{
    mgr = new QNetworkAccessManager(this);

    connect(mgr, &QNetworkAccessManager::finished, [=](QNetworkReply* reply) {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            //All good!
            log("Email Successfully Sent!");
        } else {
            warn("Email Error");
            warn(reply->readAll());
        }
        reply->deleteLater();
    });
}

void MailSender::send(MailMessage m) {
    QList<MailMessage> messages;
    messages.append(m);
    send(messages);
}

void MailSender::send(QList<MailMessage> msgs) {
    if (msgs.count() > 0) {
        //Build JSON
        QJsonObject root;
        QJsonArray messages;

        for (MailMessage m : msgs) {
            QJsonObject message;

            QJsonObject from;
            from.insert("Email", settings.value("mail/fromAddress").toString());
            from.insert("Name", settings.value("mail/fromName").toString());
            message.insert("From", from);

            QJsonArray to;
            for (QString rec : m.to.keys()) {
                QJsonObject recipient;
                recipient.insert("Email", rec);
                recipient.insert("Name", m.to.value(rec));
                to.append(recipient);
            }
            message.insert("To", to);
            message.insert("Subject", m.subject);
            message.insert("HTMLPart", m.html);

            messages.append(message);
        }
        root.insert("Messages", messages);

        QJsonDocument doc(root);
        QByteArray payload = doc.toJson();

        QString authorization = settings.value("mail/apiKey").toString() + ":" + settings.value("mail/secretKey").toString();

        QNetworkRequest req;
        req.setUrl(QUrl("https://api.mailjet.com/v3.1/send"));
        req.setRawHeader("Authorization", "Basic " + authorization.toUtf8().toBase64());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        mgr->post(req, payload);
    }
}
