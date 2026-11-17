/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "retools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

namespace ReTools {

QList<Tool> all()
{
    return {
        { "ghidra", "Ghidra", false },
        { "cutter", "Cutter", true },
        { "x64dbg", "x64dbg", true },
        { "imhex", "ImHex", true },
    };
}

static QString toolsDir()
{
    return QCoreApplication::applicationDirPath() + "/tools";
}

//! First existing file among candidates (relative to tools/ unless absolute).
static QString firstExisting(const QStringList &candidates)
{
    for (const QString &c : candidates) {
        const QString path = QDir::isAbsolutePath(c) ? c : toolsDir() + "/" + c;
        if (QFileInfo(path).isFile())
            return QDir::toNativeSeparators(path);
    }
    return QString();
}

QString find(const QString &id, bool target32bit)
{
    QSettings settings;
    const QString configured = settings.value("re/" + id).toString();
    if (!configured.isEmpty() && QFileInfo(configured).isFile())
        return configured;
#ifdef Q_OS_WIN32
    if (id == "ghidra")
        return firstExisting({ "ghidra/ghidraRun.bat" });
    if (id == "cutter")
        return firstExisting({ "cutter/cutter.exe", "cutter/Cutter.exe" });
    if (id == "x64dbg")
        return target32bit ? firstExisting({ "x64dbg/release/x32/x32dbg.exe", "x64dbg/x32/x32dbg.exe" })
                           : firstExisting({ "x64dbg/release/x64/x64dbg.exe", "x64dbg/x64/x64dbg.exe" });
    if (id == "imhex")
        return firstExisting({ "imhex/imhex-gui.exe", "imhex/imhex.exe" });
    return QString();
#else
    Q_UNUSED(target32bit);
    QStringList names;
    if (id == "ghidra") names << "ghidraRun" << "ghidra";
    if (id == "cutter") names << "cutter" << "Cutter";
    if (id == "imhex") names << "imhex";
    for (const QString &n : names) {
        const QString p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty())
            return p;
    }
    return QString();
#endif
}

bool launch(const QString &id, const QString &file, bool target32bit, QString *error)
{
    const QString exe = find(id, target32bit);
    if (exe.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("ReTools", "%1 is not installed. Reinstall Asmon with this "
                                                            "component, or set its path in Asmon.ini ([re] %2=...).")
                         .arg(id, id);
        return false;
    }
    QProcess p;
    p.setProgram(exe);
    QStringList args;
    if (!file.isEmpty() && id != "ghidra")
        args << QDir::toNativeSeparators(file);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN32
    if (id == "ghidra") {
        //! Ghidra runs on the JDK bundled next to it (tools/jdk).
        const QString jdk = toolsDir() + "/jdk";
        if (QFileInfo(jdk + "/bin/java.exe").isFile()) {
            env.insert("JAVA_HOME", QDir::toNativeSeparators(jdk));
            env.insert("PATH", QDir::toNativeSeparators(jdk + "/bin") + ";" + env.value("PATH"));
        }
        //! .bat files need cmd.exe
        p.setProgram("cmd.exe");
        args = QStringList() << "/c" << exe;
    }
#endif
    p.setArguments(args);
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(QFileInfo(exe).absolutePath());
    if (!p.startDetached()) {
        if (error)
            *error = QCoreApplication::translate("ReTools", "Could not start %1 (%2).").arg(id, exe);
        return false;
    }
    return true;
}

} // namespace ReTools
