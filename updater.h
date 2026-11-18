#ifndef UPDATER_H
#define UPDATER_H

#include <QObject>
#include <QString>
#include <functional>

class QProcess;

/**
 * @file updater.h
 * Asmon auto-update. Asks the update server for the newest build; when there is one the user
 * can download it (HTTPS, SHA-256 checked) and Asmon replaces its own asmon.exe and restarts.
 * Uses the system curl (Windows 10+ and Linux ship it), so no extra Qt module is needed.
 */
class Updater : public QObject
{
    Q_OBJECT
public:
    struct Info {
        int build = 0;
        QString version;
        QString notes;
        QString url;      // absolute
        QString sha256;
        qint64 size = 0;
    };

    explicit Updater(QObject *parent = nullptr);

    //! Build number of this program (0 = built by hand, not by CI).
    static int currentBuild();
    static QString serverUrl();
    static bool canInstall();

    //! Asks the server; emits updateAvailable / upToDate / failed.
    void check();
    //! Downloads and verifies `info`, then emits downloaded(path) or failed.
    void download(const Info &info);
    //! Replaces asmon.exe with the downloaded file after this process exits, then starts it.
    //! Returns false (and sets `error`) when the replacement could not be started.
    static bool installAndRestart(const QString &newExe, QString *error);

signals:
    void updateAvailable(const Updater::Info &info);
    void upToDate();
    void downloaded(const QString &path, const Updater::Info &info);
    void failed(const QString &message);

private:
    void run(const QStringList &args, std::function<void(int, const QByteArray &, const QString &)> done);
};

Q_DECLARE_METATYPE(Updater::Info)

#endif
