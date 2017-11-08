#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <QObject>
#include <QDateTime>
#include <QDataStream>
#define LineCheck log("@ " + QString::number(__LINE__));

namespace Functions {
    void log(QString content);
    void err(QString content);
    void good(QString content);
    void warn(QString content);
    QByteArray gzip(const QByteArray& data);

    QString getHTTPStatusCode(int code);
    QString getRandomString(int length);
}

#endif // FUNCTIONS_H
