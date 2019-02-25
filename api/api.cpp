#include "api.h"
#include "types/multipartformdata.h"

#include <QRandomGenerator>

using namespace Functions;
extern Ratelimit* ratelimit;
extern bool useSsl;
extern MailSender* mail;

Api::Api(QObject *parent) : QObject(parent)
{
    db = QSqlDatabase::addDatabase("QPSQL");
    db.setHostName(settings.value("sql/hostname", "127.0.0.1").toString());
    db.setDatabaseName(settings.value("sql/dbname", "vicr123-bugs").toString());
    db.setUserName(settings.value("sql/username", "bugserver").toString());
    db.setPassword(settings.value("sql/password").toString());

    log("Connecting to SQL Server...");
    if (db.open()) {
        good("Connected to SQL Server");
    } else {
        err("Could not connect to SQL Server");
    }
}

Response Api::processPath(Request req) {
    Response rsp;
    QString token = req.headers.value("Authorization");
    QString headerPassword = req.headers.value("Password");
    QString contentType = req.headers.value("Content-Type");

    //Check ratelimits
    RatelimitItem limit = ratelimit->getRateLimit(req.connection->peerAddress());
    rsp.headers.insert("X-RateLimit-Remaining", QString::number(limit.remaining - 1));
    rsp.headers.insert("X-RateLimit-Reset", QString::number(limit.resetTime));

    if (limit.remaining == 0) {
        rsp.headers.insert("X-RateLimit-Remaining", "0");
        rsp.statusCode = 429;
        return rsp;
    }
    ratelimit->decrement(req.connection->peerAddress());

    if (!db.isOpen()) {
        HTTP_RET(500);
    } else {
        if (req.path == "/api/socket") {
            if (req.method != "GET") {
                HTTP_RET(405);
            }

            QStringList connectionParams = req.headers.value("Connection").split(", ");
            if (req.headers.value("Upgrade") != "websocket" || !connectionParams.contains("Upgrade")) {
                log("WebSockets Handshake Failed - Incorrect Upgrade Headers");
                HTTP_RET(400);
            }

            //Read handshake
            if (req.headers.value("Sec-WebSocket-Version") != "13") {
                log("WebSockets Handshake Failed - Incorrect WebSockets Version");
                rsp.headers.insert("Sec-WebSocket-Version", "13");
                HTTP_RET(400);
            }

            if (!req.headers.contains("Sec-WebSocket-Key")) {
                log("WebSockets Handshake Failed - Unknown WebSockets Key");
                HTTP_RET(400);
            }

            QString key = req.headers.value("Sec-WebSocket-Key");
            key.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
            QString accept = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toBase64();

            rsp.headers.insert("Sec-WebSocket-Accept", accept);
            rsp.headers.insert("Upgrade", "websocket");
            rsp.headers.insert("Connection", "Upgrade");
            rsp.statusCode = 101;
            rsp.allowEmptyContents = true;
            rsp.doNotClose = true;

            SocketApi* sApi = new SocketApi(req.connection);
            sockets.append(sApi);
            connect(sApi, &SocketApi::closed, [=] {
                sockets.removeAll(sApi);
            });
            QTimer::singleShot(0, [=] {
                sApi->sendTextFrame("HELLO");
            });

            return rsp;
        } else if (req.path == "/api/projects") { //Get projects
            if (req.method != "GET") {
                HTTP_RET(405);
            }
            QSqlQuery q = db.exec("SELECT * FROM projects ORDER BY \"order\" ASC");

            QJsonArray arr;
            while (q.next()) {
                QJsonObject obj;
                obj.insert("name", q.value(0).toString());
                obj.insert("icon", q.value(1).toString());
                arr.append(obj);
            }

            if (arr.count() == 0) {
                SQL_CHECK
                rsp.statusCode = 500;
            } else {
                QJsonDocument doc = QJsonDocument(arr);

                rsp.statusCode = 200;
                rsp.contents = doc.toJson();
                rsp.headers.insert("Content-Type", "application/json; charset=UTF-8");
            }
            return rsp;
        } else if (req.path.startsWith("/api/bugs/")) { //Projects
            QStringList parts = req.path.split("/");
            parts.removeAll("");

            QSqlQuery q;

            bool isAdmin = false;
            QString currentUser = "";
            if (token != "") {
                q = user(token);
                if (q.next()) {
                    isAdmin = q.value("isAdmin").toBool();
                    currentUser = q.value("id").toString();
                }
            }

            if (req.method == "GET") {
                if (parts.count() == 3) { //Get all bugs for project
                    QString table = parts.at(2);
                    table.replace("%20", " ");

                    int tableId = getProjectId(table);

                    if (tableId == -1) {
                        HTTP_RET(404);
                    }

                    if (currentUser == "") {
                        //See all non private bugs
                        q.prepare("SELECT * FROM \"bugs\" WHERE project=:TABLE AND isprivate=false ORDER BY (case when isOpen then 1 else 2 end) asc, id desc");
                    } else if (isAdmin) {
                        //See all bugs
                        q.prepare("SELECT * FROM \"bugs\" WHERE project=:TABLE ORDER BY (case when isOpen then 1 else 2 end) asc, id desc");
                    } else {
                        //See all non private bugs and bugs from user
                        q.prepare("SELECT * FROM \"bugs\" WHERE project=:TABLE AND (author=:ID OR isprivate=false) ORDER BY (case when isOpen then 1 else 2 end) asc, id desc");
                        q.bindValue(":ID", currentUser);
                    }
                    q.bindValue(":TABLE", tableId);
                    q.exec();

                    QJsonArray arr;
                    while (q.next()) {
                        QJsonObject obj;
                        obj.insert("id", q.value("projectnum").toString());
                        obj.insert("title", q.value("title").toString());
                        //obj.insert("body", q.value(2).toString());
                        obj.insert("timestamp", q.value("timestamp").toString());
                        obj.insert("author", q.value("author").toString());
                        obj.insert("isOpen", q.value("isopen").toBool());
                        obj.insert("private", q.value("isprivate").toBool());
                        obj.insert("importance", q.value("importance").toInt());
                        arr.append(obj);
                    }

                    QJsonDocument doc = QJsonDocument(arr);

                    rsp.statusCode = 200;
                    rsp.contents = doc.toJson();
                    rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                    return rsp;
                } else if (parts.count() == 4) { //Get bug for project
                    QString table = parts.at(2);
                    table.replace("%20", " ");
                    QString bugID = parts.at(3);

                    int tableId = getProjectId(table);

                    if (tableId == -1) {
                        HTTP_RET(404);
                    }

                    if (bugID == "create") { //Create new bug
                        HTTP_RET(405);
                    }

                    q = getBug(tableId, bugID);
                    if (!q.next()) {
                        HTTP_RET(404);
                    }
                    ASSERT_PRIVATE_BUG_ALLOW_READ(currentUser, isAdmin)

                    QJsonDocument doc = QJsonDocument(extractBug(q));

                    rsp.statusCode = 200;
                    rsp.contents = doc.toJson();
                    rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                    return rsp;
                } else if (parts.count() == 5) {
                    QString table = parts.at(2);
                    table.replace("%20", " ");
                    QString bugID = parts.at(3);
                    QString subObject = parts.at(4);
                    int tableId = getProjectId(table);

                    if (tableId == -1) {
                        HTTP_RET(404);
                    }

                    QSqlQuery q = getBug(tableId, bugID);
                    if (!q.next()) {
                        HTTP_RET(404);
                    }
                    ASSERT_PRIVATE_BUG_ALLOW_READ(currentUser, isAdmin)

                    if (subObject == "comments") { //Get all comments
                        QJsonDocument doc = QJsonDocument(comments(tableId, bugID));

                        rsp.statusCode = 200;
                        rsp.contents = doc.toJson();
                        rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                        return rsp;
                    } else {
                        HTTP_RET(404);
                    }
                } else {
                    HTTP_RET(404);
                }
            } else if (req.method == "POST") {
                if (parts.length() == 4) {
                    QString table = parts.at(2);
                    table.replace("%20", " ");
                    QString bugID = parts.at(3);
                    int tableId = getProjectId(table);

                    if (tableId == -1) {
                        HTTP_RET(404);
                    }

                    if (bugID == "create") {
                        //Get user
                        QSqlQuery q = user(token);
                        if (!q.next()) {
                            HTTP_RET(401);
                        }

                        //Create a new bug
                        QJsonDocument doc = QJsonDocument::fromJson(req.body);
                        QJsonObject obj = doc.object();

                        QString title = obj.value("title").toString();
                        QString body = obj.value("body").toString();
                        QJsonArray attachments = obj.value("attachments").toArray();
                        int author = q.value("id").toInt();
                        int importance = obj.value("importance").toInt(3);

                        if (body.length() > 10000) {
                            //Exceeded body limit
                            rsp.contents = "Body greater than 10000 characters";
                            HTTP_RET(400);
                        }

                        if (title.length() > 100) {
                            //Exceeded body limit
                            rsp.contents = "Title greater than 100 characters";
                            HTTP_RET(400);
                        }

                        if (importance < 0 || importance > 6) {
                            //Not within range
                            rsp.contents = "Importance not between 0 and 6";
                            HTTP_RET(400);
                        }

                        //Ensure each attachment exists
                        for (QJsonValue v : attachments) {
                            int attachmentId = v.toInt();
                            QSqlQuery q;
                            q.prepare("SELECT * FROM \"files\" WHERE id=:ID");
                            q.bindValue(":ID", attachmentId);
                            q.exec();

                            if (!q.next()) {
                                //Attachment does not exist
                                rsp.contents = "Attachment does not exist";
                                HTTP_RET(400);
                            }
                        }

                        q.prepare("INSERT INTO \"bugs\" (title, body, author, isopen, importance, project, projectNum, isprivate) VALUES (:TITLE, :BODY, :AUTHOR, :ISOPEN, :IMPORTANCE, :TABLE, :PROJECTNUM, :ISPRIVATE) RETURNING id");
                        q.bindValue(":TITLE", title);
                        q.bindValue(":BODY", body);
                        q.bindValue(":AUTHOR", author);
                        q.bindValue(":ISOPEN", true);
                        q.bindValue(":IMPORTANCE", importance);
                        q.bindValue(":TABLE", tableId);
                        q.bindValue(":PROJECTNUM", getNumberOfBugs(tableId) + 1);
                        q.bindValue(":ISPRIVATE", obj.value("private").toBool(false));
                        SQL_CHECK

                        if (!q.exec()) {
                            SQL_CHECK
                            HTTP_RET(500);
                        }

                        q.next();

                        int bugId = q.value("id").toInt();

                        //Connect each attachment
                        for (QJsonValue v : attachments) {
                            int attachmentId = v.toInt();

                            q.prepare("INSERT INTO \"bugsattachments\" (bugid, attachmentid) VALUES (:BUGID, :ATTACHMENTID)");
                            q.bindValue(":BUGID", bugId);
                            q.bindValue(":ATTACHMENTID", attachmentId);
                            SQL_CHECK

                            if (!q.exec()) {
                                SQL_CHECK
                                HTTP_RET(500);
                            }
                        }

                        //Tell the admin
                        if (settings.value("newBugNotification/enabled", false).toBool()) {
                            int count = settings.beginReadArray("newBugNotification/people");
                            for (int i = 0; i < count; i++) {
                                settings.setArrayIndex(i);

                                QString activationUrl = QUrl("http://" + settings.value("www/host").toString() + "/app/" + table + "/" + QString::number(bugId) + "/").toEncoded();

                                MailMessage msg("newbug", QStringList() << settings.value("name").toString() << table.replace("%20", " ") << activationUrl);
                                msg.subject = "A new bug has been filed!";

                                QMap<QString, QString> to;
                                to.insert(settings.value("email").toString(), settings.value("name").toString());
                                msg.to = to;

                                mail->send(msg);
                            }
                            settings.endArray();
                        }

                        rsp.statusCode = 200;
                        return rsp;
                    } else {
                        HTTP_RET(405);
                    }
                } else if (parts.count() == 5) {
                    QString table = parts.at(2);
                    table.replace("%20", " ");
                    QString bugID = parts.at(3);
                    QString subObject = parts.at(4);
                    int tableId = getProjectId(table);

                    if (tableId == -1) {
                        rsp.statusCode = 404;
                        return rsp;
                    }

                    QSqlQuery q = getBug(tableId, bugID);
                    if (!q.next()) {
                        SQL_CHECK
                        rsp.statusCode = 404;
                        return rsp;
                    }
                    ASSERT_PRIVATE_BUG_ALLOW_READ(currentUser, isAdmin);

                    QString bugAuthor = q.value("author").toString();

                    if (subObject == "comments") {
                        //Check User
                        q = user(token);
                        if (!q.next()) {
                            rsp.statusCode = 401;
                            return rsp;
                        }

                        //Append to comments
                        QJsonArray coms = comments(tableId, bugID);
                        QString author = q.value("id").toString();
                        QString userEmail = q.value("email").toString();
                        QString authorName = q.value("username").toString();

                        QJsonDocument doc = QJsonDocument::fromJson(req.body);
                        QJsonObject obj = doc.object();
                        QString body = obj.value("body").toString();
                        QJsonArray attachments = obj.value("attachments").toArray();

                        if (body == "") {
                            HTTP_RET(400);
                        }

                        if (body.length() > 10000) {
                            //Exceeded body limit
                            rsp.contents = "Body greater than 10000 characters";
                            HTTP_RET(400);
                        }

                        //Ensure each attachment exists
                        for (QJsonValue v : attachments) {
                            int attachmentId = v.toInt();
                            QSqlQuery q;
                            q.prepare("SELECT * FROM \"files\" WHERE id=:ID");
                            q.bindValue(":ID", attachmentId);
                            q.exec();

                            if (!q.next()) {
                                //Attachment does not exist
                                rsp.contents = "Attachment does not exist";
                                HTTP_RET(400);
                            }
                        }

                        q.prepare("INSERT INTO \"comments\" (text, author) VALUES (:TEXT, :AUTHOR) RETURNING *");
                        q.bindValue(":TEXT", body);
                        q.bindValue(":AUTHOR", author);
                        q.exec();

                        if (!q.next()) {
                            SQL_CHECK
                            HTTP_RET(500);
                        }

                        QString id = q.value("id").toString();
                        QString timestamp = q.value("timestamp").toString();


                        //Connect each attachment
                        for (QJsonValue v : attachments) {
                            int attachmentId = v.toInt();

                            q.prepare("INSERT INTO \"commentsattachments\" (commentid, attachmentid) VALUES (:COMMENTID, :ATTACHMENTID)");
                            q.bindValue(":COMMENTID", id);
                            q.bindValue(":ATTACHMENTID", attachmentId);
                            SQL_CHECK

                            if (!q.exec()) {
                                SQL_CHECK
                                HTTP_RET(500);
                            }
                        }

                        QMap<QString, QString> emails;
                        QStringList commentIds;
                        for (QJsonValue v : coms) {
                            QJsonObject object = v.toObject();
                            commentIds.append(object.value("id").toString());

                            q.prepare("SELECT * FROM \"users\" WHERE \"id\"=:ID");
                            q.bindValue(":ID", object.value("author").toString());
                            q.exec();

                            if (q.next()) {
                                QString email = q.value("email").toString();
                                if (!emails.contains(email) && email != userEmail && q.value("verified").toBool()) {
                                    emails.insert(email, q.value("username").toString());
                                }
                            }
                        }
                        commentIds.append(id);

                        setComments(tableId, bugID, commentIds);

                        q.prepare("SELECT * FROM \"users\" WHERE \"id\"=:ID");
                        q.bindValue(":ID", bugAuthor);
                        q.exec();

                        if (q.next()) {
                            QString email = q.value("email").toString();
                            if (!emails.contains(email) && email != userEmail && q.value("verified").toBool()) {
                                emails.insert(email, q.value("username").toString());
                            }
                        }

                        //Get Table Name
                        QSqlQuery q;
                        q.prepare("SELECT * FROM \"projects\" WHERE id=:ID");
                        q.bindValue(":ID", tableId);
                        q.exec();

                        if (q.next()) {
                            //Send an email about new comments
                            //Get all users email should send to

                            QList<MailMessage> messages;
                            for (QString email : emails.keys()) {
                                QString activationUrl = QUrl("http://" + settings.value("www/host").toString() + "/app/" + q.value("name").toString() + "/" + bugID + "/").toEncoded();

                                MailMessage msg("commentAdded", QStringList() << emails.value(email) << "#" + bugID << q.value("name").toString() << authorName << obj.value("body").toString() << activationUrl);
                                msg.subject = "New comment on bug #" + bugID + " in project " + q.value("name").toString();

                                QMap<QString, QString> to;
                                to.insert(email, emails.value(email));
                                msg.to = to;
                                messages.append(msg);
                            }

                            mail->send(messages);
                        }

                        {
                            QJsonObject obj;
                            obj.insert("body", body);
                            obj.insert("timestamp", timestamp);
                            obj.insert("id", id);
                            obj.insert("author", author);
                            obj.insert("system", false);

                            QJsonDocument doc(obj);

                            for (SocketApi* socket : listeningSockets(tableId, bugID, author.toInt())) {
                                socket->sendTextFrame("COMMENT NEW " + doc.toJson());
                            }

                            rsp.statusCode = 200;
                            rsp.contents = doc.toJson();
                            rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                            return rsp;
                        }
                    } else {
                        rsp.statusCode = 404;
                        return rsp;
                    }
                } else {
                    rsp.statusCode = 404;
                    return rsp;
                }
            } else if (req.method == "PATCH") {
                if (parts.length() == 4) {
                    QString table = parts.at(2);
                    table.replace("%20", " ");
                    QString bugID = parts.at(3);
                    int tableId = getProjectId(table);

                    if (tableId == -1) {
                        rsp.statusCode = 404;
                        return rsp;
                    }

                    //Get user
                    QSqlQuery q = user(token);
                    if (!q.next()) {
                        rsp.statusCode = 401;
                        return rsp;
                    }

                    bool isAdmin = q.value("isAdmin").toBool();
                    int userId = q.value("id").toInt();

                    //Get bug details
                    q = getBug(tableId, bugID);
                    if (!q.next()) {
                        rsp.statusCode = 404;
                        return rsp;
                    }
                    ASSERT_PRIVATE_BUG_ALLOW_READ(currentUser, isAdmin)

                    //Check if user is an administrator or the bug author
                    if (!isAdmin && q.value("author").toInt() != userId) {
                        rsp.statusCode = 403;
                        return rsp;
                    }

                    QJsonDocument doc = QJsonDocument::fromJson(req.body);
                    QJsonObject obj = doc.object();

                    if (obj.value("isOpen").toBool() != q.value("isOpen").toBool()) {
                        if (obj.value("isOpen").toBool()) {
                            appendCommentId(tableId, bugID, "-2");
                        } else {
                            appendCommentId(tableId, bugID, "-1");
                        }
                    }

                    bool isOpen = obj.value("isOpen").toBool(q.value("isOpen").toBool());
                    int importance = obj.value("importance").toInt(q.value("importance").toInt());

                    q.prepare("UPDATE \"bugs\" SET isOpen=:ISOPEN, importance=:IMPORTANCE WHERE projectNum=:ID AND project=:TABLE RETURNING *");
                    q.bindValue(":ISOPEN", isOpen);
                    q.bindValue(":IMPORTANCE", importance);
                    q.bindValue(":ID", bugID);
                    q.bindValue(":TABLE", tableId);

                    if (!q.exec()) {
                        rsp.statusCode = 500;
                        return rsp;
                    }

                    q.next();

                    rsp.statusCode = 200;
                    rsp.contents = QJsonDocument(extractBug(q)).toJson();
                    rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                    return rsp;
                } else if (parts.length() == 6) {
                    QString table = parts.at(2);
                    table.replace("%20", " ");
                    QString bugID = parts.at(3);
                    QString subObject = parts.at(4);
                    QString subObjectId = parts.at(5);
                    int tableId = getProjectId(table);

                    if (tableId == -1) {
                        rsp.statusCode = 404;
                        return rsp;
                    }

                    QSqlQuery q = getBug(tableId, bugID);
                    if (!q.next()) {
                        SQL_CHECK
                        rsp.statusCode = 404;
                        return rsp;
                    }
                    ASSERT_PRIVATE_BUG_ALLOW_READ(currentUser, isAdmin)

                    if (subObject == "comments") {
                        //Get user
                        q = user(token);
                        if (!q.next()) {
                            rsp.statusCode = 401;
                            return rsp;
                        }

                        //Check comments
                        QJsonArray coms = comments(tableId, bugID);
                        QStringList commentIds;
                        bool userOwns = false;
                        for (QJsonValue v : coms) {
                            commentIds.append(v.toObject().value("id").toString());
                            if (v.toObject().value("author").toString() == q.value("id")) userOwns = true;
                        }

                        if (!commentIds.contains(subObjectId)) {
                            rsp.statusCode = 404;
                            return rsp;
                        }

                        if (!userOwns) {
                            rsp.statusCode = 401;
                            return rsp;
                        }

                        QJsonDocument doc = QJsonDocument::fromJson(req.body);
                        QJsonObject obj = doc.object();
                        if (!obj.contains("body")) {
                            rsp.statusCode = 400;
                            return rsp;
                        }

                        //Edit comment
                        q.prepare("UPDATE \"comments\" SET text=:TEXT WHERE id=:ID");
                        q.bindValue(":ID", subObjectId);
                        q.bindValue(":TEXT", obj.value("body").toString());
                        if (!q.exec()) {
                            SQL_CHECK
                            rsp.statusCode = 500;
                            return rsp;
                        }

                        rsp.statusCode = 204;
                        return rsp;
                    } else {
                        rsp.statusCode = 404;
                        return rsp;
                    }
                } else {
                    rsp.statusCode = 404;
                    return rsp;
                }
            } else if (req.method == "DELETE") {
                if (parts.length() == 6) {
                    QString table = parts.at(2);
                    table.replace("%20", " ");
                    QString bugID = parts.at(3);
                    QString subObject = parts.at(4);
                    QString subObjectId = parts.at(5);
                    int tableId = getProjectId(table);

                    if (tableId == -1) {
                        rsp.statusCode = 404;
                        return rsp;
                    }

                    QSqlQuery q = getBug(tableId, bugID);
                    if (!q.next()) {
                        SQL_CHECK
                        rsp.statusCode = 404;
                        return rsp;
                    }
                    ASSERT_PRIVATE_BUG_ALLOW_READ(currentUser, isAdmin)

                    if (subObject == "comments") {

                        //Get user
                        q = user(token);
                        if (!q.next()) {
                            rsp.statusCode = 401;
                            return rsp;
                        }

                        //Check comments
                        QJsonArray coms = comments(tableId, bugID);
                        QStringList commentIds;
                        bool userOwns = false;
                        for (QJsonValue v : coms) {
                            commentIds.append(v.toObject().value("id").toString());
                            if (v.toObject().value("author").toString() == q.value("id")) userOwns = true;
                        }

                        if (!commentIds.contains(subObjectId)) {
                            rsp.statusCode = 404;
                            return rsp;
                        }

                        if (!userOwns && !isAdmin) {
                            rsp.statusCode = 401;
                            return rsp;
                        }

                        //Remove comment from bug
                        commentIds.removeAll(subObjectId);
                        setComments(tableId, bugID, commentIds);

                        //Remove comment from comments table
                        q.prepare("DELETE FROM \"comments\" WHERE id=:ID");
                        q.bindValue(":ID", subObjectId);
                        if (!q.exec()) {
                            SQL_CHECK
                            rsp.statusCode = 500;
                            return rsp;
                        }

                        rsp.statusCode = 204;
                        return rsp;
                    } else {
                        HTTP_RET(404);
                    }
                } else {
                    HTTP_RET(404);
                }
            } else {
                HTTP_RET(405);
            }
        } else if (req.path == "/api/attachments") {
            if (req.method != "POST") {
                HTTP_RET(405);
            }

            //Get user
            QSqlQuery q = user(token);
            if (!q.next()) {
                HTTP_RET(401);
            }

            int userId = q.value("id").toInt();

            QStringList contentTypeParts = contentType.split(";");
            if (contentTypeParts.count() < 2) {
                HTTP_RET(400);
            }
            if (contentTypeParts.first().trimmed() != "multipart/form-data") {
                HTTP_RET(400);
            }
            if (!contentTypeParts.at(1).startsWith(" boundary=")) {
                HTTP_RET(400);
            }

            QString boundary = contentTypeParts.at(1).mid(10);
            MultiPartFormData formData(req.body, boundary);

            //Perform a sanity check to make sure all these files are okay
            for (MultiPartPart p : formData.parts()) {
                QStringList dispositionParts = p.headers.value("Content-Disposition").split(";");

                if (dispositionParts.count() < 2) {
                    HTTP_RET(400);
                }

                bool haveFilename = false;
                for (QString part : dispositionParts) {
                    if (part.startsWith(" filename=")) haveFilename = true;
                }

                if (!haveFilename) {
                    HTTP_RET(400);
                }
            }

            //Save all the files
            QJsonArray filesArray;
            for (MultiPartPart p : formData.parts()) {
                QStringList dispositionParts = p.headers.value("Content-Disposition").split(";");

                QString displayFile;
                for (QString part : dispositionParts) {
                    if (part.startsWith(" filename=")) {
                        displayFile = part.mid(10);

                        if (displayFile.startsWith("\"")) {
                            displayFile.remove(0, 1);
                        }
                        if (displayFile.endsWith("\"")) {
                            displayFile.chop(1);
                        }
                    }
                }

                //Find a temporary filename
                QString filename;
                do {
                    filename.clear();
                    QString selection = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890";
                    for (int i = 0; i < 64; i++) {
                        filename.append(selection.at(QRandomGenerator::global()->bounded(selection.length())));
                    }

                    if (displayFile.contains(".")) {
                        filename.append(displayFile.mid(displayFile.indexOf(".")));
                    }

                    q.prepare("SELECT * FROM \"files\" WHERE filename=:FILENAME");
                    q.bindValue(":FILENAME", filename);
                    q.exec();
                } while (q.next());

                //Generate ID
                int id;
                do {
                    id = QRandomGenerator::global()->bounded(RAND_MAX);
                    q.prepare("SELECT * FROM \"files\" WHERE id=:ID");
                    q.bindValue(":ID", id);
                    q.exec();
                } while (q.next());

                //Save the file
                QDir attachmentDirectory(QDir(settings.value("www/path").toString()).absoluteFilePath("attachments"));
                if (!attachmentDirectory.exists()) {
                    QDir::root().mkpath(attachmentDirectory.absolutePath());
                }

                QFile file(attachmentDirectory.absoluteFilePath(filename));
                file.open(QFile::WriteOnly);
                file.write(p.data);
                file.close();

                q.prepare("INSERT INTO \"files\" (id, userid, filename, displayfilename) VALUES (:ID, :USERID, :FILENAME, :DISPLAYFILENAME)");
                q.bindValue(":ID", id);
                q.bindValue(":USERID", userId);
                q.bindValue(":FILENAME", filename);
                q.bindValue(":DISPLAYFILENAME", displayFile);
                if (!q.exec()) {
                    SQL_CHECK
                    HTTP_RET(500);
                }

                QJsonObject o;
                o.insert("id", id);
                o.insert("displayfilename", displayFile);
                o.insert("filename", filename);

                filesArray.append(o);
            }


            rsp.statusCode = 200;
            rsp.contents = QJsonDocument(filesArray).toJson();
            rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
            return rsp;
        } else if (req.path == "/api/users/create") {
            if (req.method != "POST") {
                HTTP_RET(405);
            }

            QJsonDocument doc = QJsonDocument::fromJson(req.body);
            if (doc.isNull() || !doc.isObject()) {
                HTTP_RET(400);
            }

            QJsonObject obj = doc.object();
            if (!obj.contains("email") || !obj.contains("username") || !obj.contains("password")) {
                rsp.statusCode = 400;
                return rsp;
            }

            QString email = obj.value("email").toString();
            QString username = obj.value("username").toString();
            QString password = obj.value("password").toString();

            if (email == "" || username == "" || password == "") {
                HTTP_RET(400);
            }

            //Check username is not already used
            QSqlQuery q;
            q.prepare("SELECT * FROM \"users\" WHERE username=:USERNAME");
            q.bindValue(":USERNAME", username);
            q.exec();

            if (q.next()) {
                rsp.contents = "Username Already Used";
                HTTP_RET(409);
            }

            //Check email is not already used
            q.prepare("SELECT * FROM \"users\" WHERE email=:EMAIL");
            q.bindValue(":EMAIL", email);
            q.exec();

            if (q.next()) {
                rsp.contents = "Email Already Used";
                HTTP_RET(409);
            }

            //Check email is actually valid
            QRegExp emailRegex("\\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,4}\\b");
            emailRegex.setCaseSensitivity(Qt::CaseInsensitive);
            emailRegex.setPatternSyntax(QRegExp::RegExp);
            if (!emailRegex.exactMatch(email)) {
                rsp.contents = "Invalid Email";
                HTTP_RET(400);
            }

            //Add to database
            QString salt = getRandomString(64);
            QByteArray saltedPassword = QCryptographicHash::hash(QString(password + salt).toUtf8(), QCryptographicHash::Sha256);

            //Generate ID
            int id;
            do {
                id = qrand();
                q.prepare("SELECT * FROM \"users\" WHERE id=:ID");
                q.bindValue(":ID", id);
                q.exec();
            } while (q.next());

            QString token = getValidToken();

            q.prepare("INSERT INTO \"users\" (id, username, password, \"passwordSalt\", email, \"loginToken\") VALUES (:ID, :USERNAME, :PASSWORD, :SALT, :EMAIL, :TOKEN)");
            q.bindValue(":ID", id);
            q.bindValue(":USERNAME", username);
            q.bindValue(":PASSWORD", QString(saltedPassword.toHex()));
            q.bindValue(":SALT", salt);
            q.bindValue(":EMAIL", email);
            q.bindValue(":TOKEN", token);
            if (!q.exec()) {
                SQL_CHECK
                rsp.statusCode = 500;
                return rsp;
            }

            //Success! Compose an email welcoming them
            QString activationToken = getRandomAlphanumericString(128);
            QString activationUrl = QUrl("http://" + settings.value("www/host").toString() + "/api/activate?token=" + activationToken).toEncoded();
            MailMessage msg("welcome", QStringList() << username << activationUrl);
            msg.subject = "Welcome to the vicr123 Bug Reporting System!";

            QMap<QString, QString> to;
            to.insert(email, username);
            msg.to = to;

            mail->send(msg);

            q.prepare("INSERT INTO \"activations\" (\"user\", token) VALUES (:USER, :TOKEN)");
            q.bindValue(":USER", id);
            q.bindValue(":TOKEN", activationToken);
            if (!q.exec()) {
                SQL_CHECK;
                warn("Couldn't insert into table \"activations\". User " + QString::number(id) + " won't be able to activate.");
            }

            {
                QJsonObject obj;
                obj.insert("token", token);
                obj.insert("username", username);
                obj.insert("email", email);

                rsp.statusCode = 200;
                rsp.contents = QJsonDocument(obj).toJson();
                rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                return rsp;
            }
        } else if (req.path == "/api/users/getToken") {
            if (req.method != "POST") {
                rsp.statusCode = 405;
                return rsp;
            }

            QJsonDocument doc = QJsonDocument::fromJson(req.body);
            if (doc.isNull() || !doc.isObject()) {
                rsp.statusCode = 400;
                rsp.delay = 1000;
                return rsp;
            }

            QJsonObject obj = doc.object();
            if (!obj.contains("username") || !obj.contains("password")) {
                rsp.statusCode = 400;
                rsp.delay = 1000;
                return rsp;
            }

            QString username = obj.value("username").toString();
            QString password = obj.value("password").toString();
            QString totpCode = obj.value("totpCode").toString();

            if (username == "" || password == "") {
                rsp.statusCode = 400;
                rsp.delay = 1000;
                return rsp;
            }

            //Get user
            QSqlQuery q;
            q.prepare("SELECT * FROM \"users\" WHERE username=:USERNAME");
            q.bindValue(":USERNAME", username);
            q.exec();

            if (!q.next()) {
                rsp.statusCode = 400;
                rsp.contents = "No user";
                rsp.delay = 1000;
                return rsp;
            }

            int id = q.value("id").toInt();
            QString hashedPassword = q.value("password").toString();
            QString salt = q.value("passwordSalt").toString();
            QString email = q.value("email").toString();
            QString totp = q.value("2faKey").toString();

            if (!passwordMatch(password, hashedPassword.toUtf8(), salt)) {
                rsp.statusCode = 401;
                rsp.contents = "Incorrect Password";
                rsp.delay = 1000;
                return rsp;
            }

            if (totp != "") {
                QByteArray key = QByteArray::fromBase64(totp.toUtf8());
                QStringList totpCodes = getTotp(key);

                if (!totpCodes.contains(totpCode)) {
                    rsp.statusCode = 401;
                    rsp.contents = "TOTP Token Required";
                    return rsp;
                }
            }

            //Provide token
            QString token = getValidToken();
            q.prepare("UPDATE \"users\" SET \"loginToken\"=:TOKEN WHERE id=:ID");
            q.bindValue(":TOKEN", token);
            q.bindValue(":ID", id);
            if (!q.exec()) {
                SQL_CHECK
                rsp.statusCode = 500;
                return rsp;
            }

            {
                //Success!
                QJsonObject obj;
                obj.insert("token", token);
                obj.insert("username", username);
                obj.insert("email", email);

                rsp.statusCode = 200;
                rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                rsp.contents = QJsonDocument(obj).toJson();
                return rsp;
            }
        } else if (req.path == "/api/users/me") { //Get current user
            if (req.method == "GET") {
                //Get user
                QSqlQuery q = user(token);
                if (!q.next()) {
                    rsp.statusCode = 401;
                    return rsp;
                }

                QJsonObject obj;
                obj.insert("id", q.value("id").toInt());
                obj.insert("username", q.value("username").toString());
                obj.insert("email", q.value("email").toString());
                obj.insert("picture", getPictureUrl(q.value("email").toString()));
                obj.insert("isAdmin", q.value("isAdmin").toBool());
                obj.insert("isVerified", q.value("verified").toBool());

                rsp.statusCode = 200;
                rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                rsp.contents = QJsonDocument(obj).toJson();
                return rsp;
            } else if (req.method == "DELETE") {
                //Get user
                QSqlQuery q = user(token);
                if (!q.next()) {
                    rsp.statusCode = 401;
                    return rsp;
                }

                int id = q.value("id").toInt();
                QString hashedPassword = q.value("password").toString();
                QString salt = q.value("passwordSalt").toString();

                if (!passwordMatch(headerPassword, hashedPassword.toUtf8(), salt)) {
                    rsp.statusCode = 401;
                    rsp.contents = "Incorrect Password";
                    rsp.delay = 1000;
                    return rsp;
                }

                //Delete User
                q.prepare("DELETE FROM \"users\" WHERE id=:ID");
                q.bindValue(":ID", id);
                if (!q.exec()) {
                    rsp.statusCode = 500;
                    return rsp;
                } else {
                    rsp.statusCode = 200;
                    return rsp;
                }
            } else if (req.method == "PATCH") {
                //Get user
                QSqlQuery q = user(token);
                if (!q.next()) {
                    rsp.statusCode = 401;
                    return rsp;
                }

                int id = q.value("id").toInt();
                QString passwordHash;
                QString passwordSalt;
                QString email = q.value("email").toString();
                QString username = q.value("username").toString();

                QString hashedPassword = q.value("password").toString();
                QString salt = q.value("passwordSalt").toString();

                if (!passwordMatch(headerPassword, hashedPassword.toUtf8(), salt)) {
                    rsp.statusCode = 401;
                    rsp.contents = "Incorrect Password";
                    rsp.delay = 1000;
                    return rsp;
                }

                QJsonDocument doc = QJsonDocument::fromJson(req.body);
                QJsonObject obj = doc.object();

                if (obj.contains("username") && username != obj.value("username").toString()) {
                    username = obj.value("username").toString();

                    //Check username is not already used
                    QSqlQuery q;
                    q.prepare("SELECT * FROM \"users\" WHERE username=:USERNAME");
                    q.bindValue(":USERNAME", username);
                    q.exec();

                    if (q.next()) {
                        rsp.statusCode = 409;
                        rsp.contents = "Username Already Used";
                        return rsp;
                    }
                }

                if (obj.contains("email") && email != obj.value("email").toString()) {
                    email = obj.value("email").toString();

                    //Check email is not already used
                    q.prepare("SELECT * FROM \"users\" WHERE email=:EMAIL");
                    q.bindValue(":EMAIL", email);
                    q.exec();

                    if (q.next()) {
                        rsp.statusCode = 409;
                        rsp.contents = "Email Already Used";
                        return rsp;
                    }
                }

                if (obj.contains("password")) {
                    QString password = obj.value("password").toString();

                    //Add to database
                    passwordSalt = getRandomString(64);
                    passwordHash = QString(QCryptographicHash::hash(QString(password + passwordSalt).toUtf8(), QCryptographicHash::Sha256).toHex());
                }

                //Modify User
                q.prepare("UPDATE \"users\" SET \"username\"=:USERNAME, \"password\"=:PASSWORD, \"passwordSalt\"=:PASSWORDSALT, \"email\"=:EMAIL WHERE id=:ID");
                q.bindValue(":ID", id);
                q.bindValue(":USERNAME", username);
                q.bindValue(":PASSWORD", passwordHash);
                q.bindValue(":PASSWORDSALT", passwordSalt);
                q.bindValue(":EMAIL", email);
                if (!q.exec()) {
                    SQL_CHECK
                    rsp.statusCode = 500;
                    return rsp;
                } else {
                    rsp.statusCode = 204;
                    return rsp;
                }
            } else {
                rsp.statusCode = 405;
                return rsp;
            }
        } else if (req.path == "/api/users/me/checkpassword") { //Check password
            //Get user
            QSqlQuery q = user(token);
            if (!q.next()) {
                rsp.statusCode = 401;
                return rsp;
            }

            QString hashedPassword = q.value("password").toString();
            QString salt = q.value("passwordSalt").toString();

            if (!passwordMatch(req.headers.value("Password"), hashedPassword.toUtf8(), salt)) {
                rsp.statusCode = 401;
                rsp.contents = "Incorrect Password";
                return rsp;
            }

            rsp.statusCode = 204;
            rsp.allowEmptyContents = true;
            return rsp;
        } else if (req.path == "/api/users/me/2fa") { //2FA setup
            //Get user
            QSqlQuery q = user(token);
            if (!q.next()) {
                rsp.statusCode = 401;
                return rsp;
            }

            QString id = q.value("id").toString();
            QString totpCode = q.value("2faKey").toString();
            QString hashedPassword = q.value("password").toString();
            QString salt = q.value("passwordSalt").toString();

            if (!passwordMatch(req.headers.value("Password"), hashedPassword.toUtf8(), salt)) {
                rsp.statusCode = 401;
                rsp.contents = "Incorrect Password";
                rsp.delay = 1000;
                return rsp;
            }

            if (req.method == "GET") { //Get if using 2FA
                QJsonObject obj;
                if (totpCode == "") {
                    obj.insert("setupRequired", true);
                } else {
                    obj.insert("setupRequired", false);
                }

                rsp.statusCode = 200;
                rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                rsp.contents = QJsonDocument(obj).toJson();
                return rsp;
            } else if (req.method == "POST") { //Add 2FA key
                QJsonDocument doc = QJsonDocument::fromJson(req.body);
                QJsonObject obj = doc.object();
                QString code = obj.value("code").toString();
                QJsonArray totpKeyArray = obj.value("key").toArray();
                QByteArray key;
                for (QJsonValue v : totpKeyArray) {
                    key.append(v.toInt());
                }

                QStringList totpCodes = getTotp(key);
                if (!totpCodes.contains(code)) {
                    rsp.statusCode = 400;
                    rsp.contents = "Invalid TOTP Code";
                    return rsp;
                }

                q.prepare("UPDATE \"users\" SET \"2faKey\"=:KEY WHERE id=:ID");
                q.bindValue(":KEY", QString(key.toBase64()));
                q.bindValue(":ID", id);

                if (!q.exec()) {
                    rsp.statusCode = 500;
                    return rsp;
                }

                {
                    //Success!
                    QJsonObject obj;
                    obj.insert("2faEnabled", true);

                    rsp.statusCode = 200;
                    rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                    rsp.contents = QJsonDocument(obj).toJson();
                    return rsp;
                }
            } else if (req.method == "DELETE") { //Remove 2FA
                q.prepare("UPDATE \"users\" SET \"2faKey\"=NULL WHERE id=:ID");
                q.bindValue(":ID", id);
                if (q.exec()) {
                    rsp.statusCode = 500;
                    return rsp;
                }

                //Success!
                QJsonObject obj;
                obj.insert("2faEnabled", false);

                rsp.statusCode = 200;
                rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                rsp.contents = QJsonDocument(obj).toJson();
                return rsp;
            } else {
                rsp.statusCode = 405;
                return rsp;
            }
        } else if (req.path.startsWith("/api/users/")) { //Get User
            if (req.method != "GET") {
                rsp.statusCode = 405;
                return rsp;
            }

            QStringList parts = req.path.split("/");
            parts.removeAll("");
            if (parts.count() == 3) { //Get user data
                QString user = parts.at(2);

                QSqlQuery q;
                q.prepare("SELECT * FROM \"users\" WHERE id=:ID");
                q.bindValue(":ID", user);
                q.exec();

                if (!q.next()) {
                    rsp.statusCode = 404;
                    return rsp;
                }

                QJsonObject obj;
                obj.insert("id", q.value("id").toInt());
                obj.insert("username", q.value("username").toString());
                obj.insert("picture", getPictureUrl(q.value("email").toString()));
                obj.insert("isAdmin", q.value("isAdmin").toBool());

                rsp.statusCode = 200;
                rsp.headers.insert("Content-Type", "text/json; charset=utf-8");
                rsp.contents = QJsonDocument(obj).toJson();
                return rsp;
            } else {
                rsp.statusCode = 404;
                return rsp;
            }
        } else if (req.path == "/api/files/upload") { //Upload a file

            rsp.statusCode = 500;
            return rsp;
        } else if (req.path.startsWith("/api/activate")) {
            QString query = req.path.mid(req.path.indexOf("?") + 1);
            QStringList queries = query.split("&");
            for (QString query : queries) {
                if (query.startsWith("token=")) {
                    QString token = query.mid(query.indexOf("=") + 1);
                    //Get user to activate

                    QSqlQuery q;
                    q.prepare("SELECT * FROM \"activations\" WHERE token=:TOKEN");
                    q.bindValue(":TOKEN", token);
                    q.exec();

                    if (!q.next()) {
                        rsp.statusCode = 400;
                        return rsp;
                    }

                    QString userId = q.value("user").toString();
                    q.prepare("UPDATE \"users\" SET verified=true WHERE id=:USER");
                    q.bindValue(":USER", userId);
                    if (!q.exec()) {
                        rsp.statusCode = 500;
                        return rsp;
                    }

                    q.prepare("DELETE FROM \"activations\" WHERE token=:TOKEN");
                    q.bindValue(":TOKEN", token);
                    q.exec();

                    rsp.statusCode = 303;
                    rsp.headers.insert("Location", "/app/index.html?validated=true");
                    rsp.allowEmptyContents = true;
                    return rsp;
                }
            }

            rsp.statusCode = 400;
            return rsp;
        } else { //Not implemented
            rsp.statusCode = 404;
            return rsp;
        }
    }
}

bool Api::projectExists(QString table) {
    QSqlQuery q("SELECT * FROM projects");
    while (q.next()) {
        if (q.value(0) == table) {
            return true;
        }
    }
    return false;
}

bool Api::userExists(QString id) {
    QSqlQuery q;
    q.prepare("SELECT * FROM \"users\" WHERE id=:ID");
    q.bindValue(":ID", id);
    q.exec();

    if (q.next()) {
        return true;
    } else {
        return false;
    }
}

bool Api::passwordMatch(QString password, QByteArray hashed, QString salt) {
    QByteArray saltedPasswordChallenge = QCryptographicHash::hash(QString(password + salt).toUtf8(), QCryptographicHash::Sha256);
    if (saltedPasswordChallenge.toHex() == hashed) {
        return true;
    } else {
        return false;
    }
}

QString Api::getValidToken() {
    QString token;
    QSqlQuery q;
    do {
        token = getRandomString(512);
        q = user(token);
    } while (q.next());
    return token;
}

QJsonObject Api::extractBug(QSqlQuery q) {
    QJsonObject obj;

    int bugId = q.value("id").toInt();

    obj.insert("id", q.value("projectnum").toString());
    obj.insert("title", q.value("title").toString());
    obj.insert("body", q.value("body").toString());
    obj.insert("timestamp", q.value("timestamp").toString());
    obj.insert("comments", q.value("comments").toString());
    obj.insert("author", q.value("author").toString());
    obj.insert("isOpen", q.value("isopen").toBool());
    obj.insert("importance", q.value("importance").toInt());
    obj.insert("private", q.value("isprivate").toBool());

    //Find all the attachments for this bug
    QJsonArray attachments;
    QMimeDatabase mimeDatabase;

    QSqlQuery query;
    query.prepare("SELECT * FROM \"bugsattachments\" WHERE \"bugid\"=:BUGID");
    query.bindValue(":BUGID", bugId);
    query.exec();

    while (query.next()) {
        int attachmentId = query.value("attachmentid").toInt();
        QSqlQuery atQuery;
        atQuery.prepare("SELECT * FROM \"files\" WHERE \"id\"=:ID");
        atQuery.bindValue(":ID", attachmentId);
        atQuery.exec();
        if (atQuery.next()) {
            QFileInfo info(QDir(settings.value("www/path").toString()).absoluteFilePath("attachments/" + atQuery.value("filename").toString()));
            QJsonObject attachmentDetails;
            attachmentDetails.insert("link", "/attachments/" + atQuery.value("filename").toString());
            attachmentDetails.insert("displayName", atQuery.value("displayfilename").toString());
            attachmentDetails.insert("size", info.size());
            attachmentDetails.insert("type", mimeDatabase.mimeTypeForFile(info).name());
            attachments.append(attachmentDetails);
        }
    }

    obj.insert("attachments", attachments);

    return obj;
}

QSqlQuery Api::user(QString token) {
    QSqlQuery q;
    q.prepare("SELECT * FROM \"users\" WHERE \"loginToken\"=:TOKEN");
    q.bindValue(":TOKEN", token);
    q.exec();
    return q;
}

int Api::getProjectId(QString name) {
    QSqlQuery q;
    q.prepare("SELECT * FROM \"projects\" WHERE \"name\"=:NAME");
    q.bindValue(":NAME", name);
    q.exec();
    if (q.next()) {
        return q.value("id").toInt();
    } else {
        return -1;
    }
}

int Api::getNumberOfBugs(int id) {
    QSqlQuery q;
    q.prepare("SELECT COUNT(*) FROM \"bugs\" WHERE project=:ID");
    q.bindValue(":ID", id);
    q.exec();
    if (q.next()) {
        return q.value("count").toInt();
    } else {
        return 0;
    }
}

QJsonArray Api::comments(int table, QString id) {
    QJsonArray arr;

    QSqlQuery q = getBug(table, id);
    if (q.next()) {
        QString rawComments = q.value("comments").toString();
        QStringList comments = rawComments.split(";");
        for (QString comment : comments) {
            if (comment.toInt() < 0) {
                QJsonObject obj;
                obj.insert("timestamp", "");
                obj.insert("author", "System");
                obj.insert("system", true);
                obj.insert("id", comment);
                if (comment == "-1") {
                    obj.insert("body", "Closed Bug");
                } else if (comment == "-2") {
                    obj.insert("body", "Re-opened Bug");
                }
                arr.append(obj);
            } else {
                q.prepare("SELECT * FROM \"comments\" WHERE id=:ID");
                q.bindValue(":ID", comment);
                q.exec();
                if (q.next()) {
                    QJsonObject obj;
                    obj.insert("body", q.value("text").toString());
                    obj.insert("timestamp", q.value("timestamp").toString());
                    obj.insert("id", comment);
                    obj.insert("author", q.value("author").toString());
                    obj.insert("system", false);

                    //Find all the attachments for this comment
                    QJsonArray attachments;
                    QMimeDatabase mimeDatabase;

                    QSqlQuery query;
                    query.prepare("SELECT * FROM \"commentsattachments\" WHERE \"commentid\"=:COMMENTID");
                    query.bindValue(":COMMENTID", comment);
                    query.exec();

                    while (query.next()) {
                        int attachmentId = query.value("attachmentid").toInt();
                        QSqlQuery atQuery;
                        atQuery.prepare("SELECT * FROM \"files\" WHERE \"id\"=:ID");
                        atQuery.bindValue(":ID", attachmentId);
                        atQuery.exec();
                        if (atQuery.next()) {
                            QFileInfo info(QDir(settings.value("www/path").toString()).absoluteFilePath("attachments/" + atQuery.value("filename").toString()));
                            QJsonObject attachmentDetails;
                            attachmentDetails.insert("link", "/attachments/" + atQuery.value("filename").toString());
                            attachmentDetails.insert("displayName", atQuery.value("displayfilename").toString());
                            attachmentDetails.insert("size", info.size());
                            attachmentDetails.insert("type", mimeDatabase.mimeTypeForFile(info).name());
                            attachments.append(attachmentDetails);
                        }
                    }

                    obj.insert("attachments", attachments);

                    arr.append(obj);
                }
            }
        }
    }

    return arr;
}

void Api::setComments(int table, QString id, QStringList commentIDs) {
    QSqlQuery q;
    q.prepare("UPDATE \"bugs\" SET comments=:COMMENTS WHERE projectNum=:ID AND project=:TABLE");

    QString comments = commentIDs.join(";");

    q.bindValue(":COMMENTS", comments);
    q.bindValue(":ID", id);
    q.bindValue(":TABLE", table);
    q.exec();
}

void Api::appendCommentId(int table, QString id, QString commentID) {
    //Append change to comments
    QJsonArray coms = comments(table, id);
    QStringList commentIds;
    for (QJsonValue v : coms) {
        commentIds.append(v.toObject().value("id").toString());
    }
    commentIds.append(commentID);

    setComments(table, id, commentIds);
}

QSqlQuery Api::getBug(int projectId, QString bugId) {
    QSqlQuery q;
    q.prepare("SELECT * FROM \"bugs\" WHERE projectNum=:ID AND project=:TABLE");
    q.bindValue(":ID", bugId);
    q.bindValue(":TABLE", projectId);
    q.exec();
    return q;
}

QString Api::getPictureUrl(QString email) {
    QString processedEmail = email.trimmed().toLower();
    QString md5hash = QCryptographicHash::hash(processedEmail.toUtf8(), QCryptographicHash::Md5).toHex();
    return "https://www.gravatar.com/avatar/" + md5hash + "?d=https%3A%2F%2Fvicr123.github.io%2Fimages%2FunknownApp.png";
}

QList<SocketApi*> Api::listeningSockets(int projectId, QString bugId, int user) {
    QList<SocketApi*> sockets;
    for (SocketApi* s : this->sockets) {
        if (s->project() == projectId && s->bug() == bugId) {
            if (user != -1 && s->user() != user) sockets.append(s);
        }
    }
    return sockets;
}

QStringList Api::getTotp(QByteArray key) {
    //qint64 t = QDateTime::currentSecsSinceEpoch() / 30;
    quint64 time = QDateTime::currentDateTime().toTime_t();

    QStringList validCodes;
    validCodes.append(getOneTotp(key, qToBigEndian((time + 30) / 30)));
    validCodes.append(getOneTotp(key, qToBigEndian(time / 30)));
    validCodes.append(getOneTotp(key, qToBigEndian((time - 30) / 30)));
    return validCodes;
}

QString Api::getOneTotp(QByteArray key, qint64 t) {
    QByteArray hmac = hmacSha1(key, QByteArray((char*) &t, sizeof(t)));

    int offset = (hmac[hmac.length() - 1] & 0xf);
    int binary = ((hmac[offset] & 0x7f) << 24)
            | ((hmac[offset + 1] & 0xff) << 16)
            | ((hmac[offset + 2] & 0xff) << 8)
            | (hmac[offset + 3] & 0xff);

    int password = binary % 1000000;
    return QString("%1").arg(password, 6, 10, QChar('0'));
}

QByteArray Api::hmacSha1(QByteArray key, QByteArray baseString)
{
    int blockSize = 64; // HMAC-SHA-1 block size, defined in SHA-1 standard
    if (key.length() > blockSize) { // if key is longer than block size (64), reduce key length with SHA-1 compression
        key = QCryptographicHash::hash(key, QCryptographicHash::Sha1);
    }

    QByteArray innerPadding(blockSize, char(0x36)); // initialize inner padding with char "6"
    QByteArray outerPadding(blockSize, char(0x5c)); // initialize outer padding with char "quot;
    // ascii characters 0x36 ("6") and 0x5c ("quot;) are selected because they have large
    // Hamming distance (http://en.wikipedia.org/wiki/Hamming_distance)

    for (int i = 0; i < key.length(); i++) {
        innerPadding[i] = innerPadding[i] ^ key.at(i); // XOR operation between every byte in key and innerpadding, of key length
        outerPadding[i] = outerPadding[i] ^ key.at(i); // XOR operation between every byte in key and outerpadding, of key length
    }

    // result = hash ( outerPadding CONCAT hash ( innerPadding CONCAT baseString ) ).toBase64
    QByteArray total = outerPadding;
    QByteArray part = innerPadding;
    part.append(baseString);
    total.append(QCryptographicHash::hash(part, QCryptographicHash::Sha1));
    QByteArray hashed = QCryptographicHash::hash(total, QCryptographicHash::Sha1);
    return hashed;
}
