#include "ratelimit.h"

Ratelimit::Ratelimit(QObject *parent) : QObject(parent)
{
    ratelimitFileName = settings.value("ratelimit/file", QDir::homePath() + "/.bugreport-ratelimit.json").toString();
    defaultRateLimit = settings.value("ratelimit/defaultRateLimit", 1000).toInt();
    defaultResetTime = settings.value("ratelimit/defaultResetTime", 3600).toInt();

    QFile ratelimitFile(ratelimitFileName);
    if (ratelimitFile.open(QFile::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(ratelimitFile.readAll());
        ratelimitFile.close();

        QJsonArray arr = doc.array();
        for (QJsonValue val : arr) {
            QJsonObject ratelimit = val.toObject();
            RatelimitItem item(ratelimit);
            this->ratelimits.append(item);
        }
    }
}

RatelimitItem Ratelimit::getRateLimit(QHostAddress remoteAddress) {
    Q_IPV6ADDR addr = remoteAddress.toIPv6Address();
    RatelimitItem item;
    item.ip = addr;
    item.remaining = defaultRateLimit;
    item.resetTime = 0;

    bool found = false;
    for (int i = 0; i < ratelimits.count(); i++) {
        RatelimitItem rlItem = ratelimits.value(i);

        bool same = true;
        for (int j = 0; j < 16; j++) {
            if (rlItem.ip[j] != addr[j]) {
                same = false;
            }
        }

        if (same) {
            item = rlItem;

            if (item.resetTime < QDateTime::currentSecsSinceEpoch()) {
                item.remaining = defaultRateLimit;
                item.resetTime = QDateTime::currentSecsSinceEpoch() + defaultResetTime;
                ratelimits.replace(i, item);
            }
            found = true;
            break;
        }
    }

    if (!found) {
        ratelimits.append(item);
    }

    return item;
}

void Ratelimit::decrement(QHostAddress remoteAddress) {
    Q_IPV6ADDR addr = remoteAddress.toIPv6Address();

    for (int i = 0; i < ratelimits.count(); i++) {
        RatelimitItem rlItem = ratelimits.value(i);

        bool same = true;
        for (int j = 0; j < 16; j++) {
            if (rlItem.ip[j] != addr[j]) {
                same = false;
            }
        }

        if (same) {
            if (rlItem.remaining != 0) {
                rlItem.remaining = rlItem.remaining - 1;
            }
            ratelimits.replace(i, rlItem);
            commit();
            break;
        }
    }
}

void Ratelimit::commit() {
    QJsonArray arr;
    for (RatelimitItem item : ratelimits) {
        arr.append(item.toJsonObject());
    }

    QFile ratelimitFile(ratelimitFileName);
    ratelimitFile.open(QFile::WriteOnly);
    ratelimitFile.write(QJsonDocument(arr).toJson());
    ratelimitFile.close();
}

RatelimitItem::RatelimitItem(QJsonObject item) {
    this->remaining = item.value("remaining").toInt();
    this->resetTime = item.value("resetTime").toInt();
    QJsonArray ipAddress = item.value("ip").toArray();
    for (int i = 0; i < 16; i++) {
        ip[i] = ipAddress.at(i).toInt();
    }
}

RatelimitItem::RatelimitItem() {

}

QJsonObject RatelimitItem::toJsonObject() {
    QJsonObject obj;
    obj.insert("remaining", this->remaining);
    obj.insert("resetTime", this->resetTime);

    QJsonArray ipAddress; // = obj.value("ip").toArray();
    for (int i = 0; i < 16; i++) {
        ipAddress.append(ip[i]);
    }
    obj.insert("ip", ipAddress);
    return obj;
}
