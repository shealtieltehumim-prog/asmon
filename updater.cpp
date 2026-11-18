#include "updater.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <functional>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

#ifndef ASMON_BUILD
#define ASMON_BUILD 0
#endif

static const char *kDefaultServer = "https://updates-production-522a5b29.up.railway.app";

Updater::Updater(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<Updater::Info>();
}

int Updater::currentBuild() { return ASMON_BUILD; }

QString Updater::serverUrl()
{
    QSettings s;
    QString u = s.value("updateurl", QString::fromLatin1(kDefaultServer)).toString().trimmed();
    while (u.endsWith('/'))
        u.chop(1);
    return u;
}

bool Updater::canInstall()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void Updater::run(const QStringList &args, std::function<void(int, const QByteArray &, const QString &)> done)
{
    QProcess *p = new QProcess(this);
#ifdef Q_OS_WIN
    const QString curl = QStringLiteral("curl.exe"); // C:\Windows\System32 (Windows 10 1803+)
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NO_WINDOW;
    });
#else
    const QString curl = QStringLiteral("curl");
#endif
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            done(-1, QByteArray(), QStringLiteral("curl"));
            p->deleteLater();
        }
    });
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus) {
        done(code, p->readAllStandardOutput(), QString::fromUtf8(p->readAllStandardError()).trimmed());
        p->deleteLater();
    });
    p->start(curl, args);
}

void Updater::check()
{
    const QString base = serverUrl();
    run({ "-sS", "-L", "--max-time", "20", "--fail", base + "/api/latest" },
        [this, base](int code, const QByteArray &out, const QString &err) {
            if (code != 0) {
                emit failed(err.isEmpty() ? tr("No answer from the update server.") : err);
                return;
            }
            const QJsonObject o = QJsonDocument::fromJson(out).object();
            Info i;
            i.build = o.value("build").toInt();
            if (i.build <= 0) {
                emit failed(tr("The update server returned an unexpected answer."));
                return;
            }
            i.version = o.value("version").toString();
            i.notes = o.value("notes").toString();
            i.sha256 = o.value("sha256").toString().toLower();
            i.size = qint64(o.value("size").toDouble());
            const QString u = o.value("url").toString();
            i.url = u.startsWith("http") ? u : base + u;
            if (i.build > currentBuild())
                emit updateAvailable(i);
            else
                emit upToDate();
        });
}

void Updater::download(const Info &info)
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("asmon-update");
    QDir().mkpath(dir);
    const QString target = QDir(dir).filePath(QString("asmon-build-%1.exe").arg(info.build));
    QFile::remove(target);
    run({ "-sS", "-L", "--max-time", "600", "--fail", "-o", target, info.url },
        [this, info, target](int code, const QByteArray &, const QString &err) {
            if (code != 0) {
                QFile::remove(target);
                emit failed(err.isEmpty() ? tr("The download failed.") : err);
                return;
            }
            QFile f(target);
            if (!f.open(QIODevice::ReadOnly)) {
                emit failed(tr("The downloaded file could not be read."));
                return;
            }
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            f.close();
            if (h.result().toHex().toLower() != info.sha256.toUtf8() || (info.size > 0 && QFileInfo(target).size() != info.size)) {
                QFile::remove(target);
                emit failed(tr("The downloaded file does not match its checksum; the update was not installed."));
                return;
            }
            emit downloaded(target, info);
        });
}

bool Updater::installAndRestart(const QString &newExe, QString *error)
{
#ifdef Q_OS_WIN
    const QString dst = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString src = QDir::toNativeSeparators(newExe);
    const QString script = QDir::toNativeSeparators(QDir(QFileInfo(newExe).absolutePath()).filePath("asmon-update.cmd"));
    const QString log = QDir::toNativeSeparators(QDir(QFileInfo(newExe).absolutePath()).filePath("asmon-update.log"));

    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QObject::tr("Could not write the update script.");
        return false;
    }
    QString s;
    s += "@echo off\r\n";
    s += QString("set PID=%1\r\n").arg(QCoreApplication::applicationPid());
    s += QString("set SRC=%1\r\nset DST=%2\r\nset LOG=%3\r\n").arg(src, dst, log);
    s += "set /a n=0\r\n:wait\r\n";
    s += "tasklist /FI \"PID eq %PID%\" 2>nul | find \"%PID%\" >nul\r\n";
    s += "if errorlevel 1 goto copy\r\n";
    s += "set /a n+=1\r\nif %n% gtr 90 goto fail\r\nping -n 2 127.0.0.1 >nul\r\ngoto wait\r\n";
    s += ":copy\r\ncopy /y \"%DST%\" \"%DST%.bak\" >nul 2>nul\r\nset /a n=0\r\n:retry\r\n";
    s += "copy /y \"%SRC%\" \"%DST%\" >nul 2>>\"%LOG%\"\r\nif not errorlevel 1 goto ok\r\n";
    s += "set /a n+=1\r\nif %n% gtr 30 goto fail\r\nping -n 2 127.0.0.1 >nul\r\ngoto retry\r\n";
    s += ":ok\r\necho updated >> \"%LOG%\"\r\nexplorer.exe \"%DST%\"\r\nexit /b 0\r\n";
    s += ":fail\r\necho update failed >> \"%LOG%\"\r\nexplorer.exe \"%DST%\"\r\nexit /b 1\r\n";
    f.write(s.toLocal8Bit());
    f.close();

    //! Can the program folder be written without elevation? (Program Files needs the admin prompt.)
    bool needElevation = false;
    {
        QFile probe(QDir(QCoreApplication::applicationDirPath()).filePath(".asmon-write-test"));
        if (probe.open(QIODevice::WriteOnly)) {
            probe.close();
            probe.remove();
        } else {
            needElevation = true;
        }
    }
    const std::wstring params = QString("/c \"\"%1\"\"").arg(script).toStdWString();
    HINSTANCE r = ShellExecuteW(nullptr, needElevation ? L"runas" : L"open", L"cmd.exe", params.c_str(),
                                nullptr, SW_HIDE);
    if (quintptr(r) <= 32) {
        if (error) *error = needElevation ? QObject::tr("The update was not started (administrator permission was refused).")
                                          : QObject::tr("The update could not be started.");
        return false;
    }
    return true;
#else
    Q_UNUSED(newExe);
    if (error) *error = QObject::tr("Automatic installation is available on Windows only.");
    return false;
#endif
}
