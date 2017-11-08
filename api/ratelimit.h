#ifndef RATELIMIT_H
#define RATELIMIT_H

#include <QObject>
#include <QHostAddress>
#include <QFile>
#include <QSettings>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

struct RatelimitItem {
    RatelimitItem();
    RatelimitItem(QJsonObject item);

    int remaining;
    qint64 resetTime;
    Q_IPV6ADDR ip;

    QJsonObject toJsonObject();
};

class Ratelimit : public QObject
{
    Q_OBJECT
public:
    explicit Ratelimit(QObject *parent = nullptr);

signals:

public slots:
    RatelimitItem getRateLimit(QHostAddress remoteAddress);
    void decrement(QHostAddress remoteAddress);

private:
    QList<RatelimitItem> ratelimits;
    QSettings settings;
    QString ratelimitFileName;
    void commit();

    int defaultRateLimit;
    int defaultResetTime;
};


#endif // RATELIMIT_H
