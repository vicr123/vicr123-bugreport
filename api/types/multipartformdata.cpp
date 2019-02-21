/****************************************
 *
 *   INSERT-PROJECT-NAME-HERE - INSERT-GENERIC-NAME-HERE
 *   Copyright (C) 2019 Victor Tran
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *************************************/
#include "multipartformdata.h"

#include <QBuffer>

struct MultiPartFormDataPrivate {
    QList<MultiPartPart> parts;
};

MultiPartFormData::MultiPartFormData(QByteArray data, QString boundary) {
    d = new MultiPartFormDataPrivate();

    //Split up each set of data
    QBuffer buf(&data);
    buf.open(QBuffer::ReadOnly);

    QByteArray currentPart;
    while (buf.canReadLine()) {
        QByteArray line = buf.readLine();
        if (line.endsWith("\r\n")) {
            if (line.trimmed() == ("--" + boundary)) {
                //Start a new part

                if (!currentPart.trimmed().isEmpty()) {
                    d->parts.append(MultiPartPart(currentPart));
                    currentPart.clear();
                }
                continue;
            } else if (line.trimmed() == ("--" + boundary + "--")) {
                //End of parts

                if (!currentPart.trimmed().isEmpty()) {
                    d->parts.append(MultiPartPart(currentPart));
                }
                return;
            }
        }

        //Append to the current part
        currentPart.append(line);
    }

    d->parts.append(MultiPartPart(currentPart));
    return;
}

MultiPartFormData::~MultiPartFormData() {
    delete d;
}

MultiPartPart::MultiPartPart(QByteArray data) {
    //Removing the trailing \r\n
    if (data.endsWith("\r\n")) {
        data.chop(2);
    }
    if (data.startsWith("\r\n")) {
        data.remove(0, 2);
    }

    QBuffer buf(&data);
    buf.open(QBuffer::ReadOnly);

    bool readHeaders = true;
    while (buf.canReadLine()) {
        QByteArray line = buf.readLine();
        if (readHeaders) {
            if (line == "\r\n") {
                //Done reading the headers
                readHeaders = false;
            } else {
                if (line.indexOf(":") != -1) {
                    QString key = line.left(line.indexOf(":")).trimmed();
                    QString value = line.mid(line.indexOf(":") + 1).trimmed();
                    headers.insert(key, value);
                } else {
                    headers.insert(line.trimmed(), "");
                }
            }
        } else {
            this->data.append(line);
        }
    }
}

QList<MultiPartPart> MultiPartFormData::parts() {
    return d->parts;
}
