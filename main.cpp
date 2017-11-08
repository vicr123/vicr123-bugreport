#include <QCoreApplication>
#include <QTextStream>
#include <QDir>
#include <server.h>
#include <signal.h>

using namespace Functions;
QString rootPath;
QString dataPath;
Api* api;
Ratelimit* ratelimit;
bool useSsl;
QSslKey privateKey;
QString certificatePath;

void sigsegv(int sig) {
    err("SIGSEGV caught. You may want to restart the server.");
    signal(SIGSEGV, &sigsegv);
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGSEGV, &sigsegv);

    QCoreApplication a(argc, argv);

    a.setOrganizationName("theSuite");
    a.setOrganizationDomain("");
    a.setApplicationName("vicr123-bugreport");

    qsrand(QDateTime::currentMSecsSinceEpoch());

    QSettings settings;
    rootPath = settings.value("www/path", QDir::homePath() + "/Documents/Website/BugsSite/").toString();
    dataPath = settings.value("www/data", QDir::homePath() + "/.vicr123-bugreport/data/").toString();
    int port = settings.value("www/port", 80).toInt();
    int sslPort = settings.value("ssl/port", 443).toInt();

    QDir::root().mkpath(dataPath);

    if (rootPath.endsWith("/")) {
        rootPath = rootPath.remove(rootPath.count() - 1, 1);
    }

    log("Welcome to vicr123-bugreport.");
    log("Root path is " + rootPath);

    if (settings.value("ssl/useSsl", false).toBool()) {
        QFile keyFile(settings.value("ssl/keyPath").toString());
        keyFile.open(QFile::ReadOnly);
        privateKey = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey, settings.value("ssl/keyPassword").toString().toUtf8());
        keyFile.close();
        certificatePath = settings.value("ssl/certPath").toString();

        if (privateKey.isNull()) {
            err("SSL couldn't be initialized properly.");
            useSsl = false;
        } else {
            good("SSL details loaded.");
            useSsl = true;
        }
    }

    ratelimit = new Ratelimit;
    api = new Api;
    good("API Initialized");

    if (settings.value("www/allowHttp", true).toBool()) {
        Server* srv = new Server(port, false);
        srv->start();
    }
    if (useSsl) {
        Server* srv = new Server(sslPort, true);
        srv->start();
    }

    int returnVal = a.exec();

    return returnVal;
}
