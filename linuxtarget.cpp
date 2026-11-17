/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "linuxtarget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>

namespace LinuxTarget {

static QString runTool(const QString &program, const QStringList &args, const QString &cwd, bool *started)
{
    QProcess p;
    if (!cwd.isEmpty())
        p.setWorkingDirectory(cwd);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    *started = p.waitForStarted(10000);
    if (!*started)
        return QString();
    p.waitForFinished(60000);
    return QString::fromLocal8Bit(p.readAll());
}

BuildResult build(const QString &nasm, const QString &lld, const QString &source,
                  const QString &listing, const QString &object, const QString &executable,
                  const QString &includeDir, const QString &sourceDir)
{
    BuildResult r;
    QFile::remove(object);
    QFile::remove(executable);

    if (!QFileInfo::exists(lld)) {
        r.log = QObject::tr("ld.lld was not found (%1). Install the LLVM component of Asmon "
                            "to build Linux programs on Windows.").arg(QDir::toNativeSeparators(lld));
        return r;
    }

    QStringList nasmArgs;
    if (!sourceDir.isEmpty())
        nasmArgs << "-I" << QDir::toNativeSeparators(sourceDir + "/");
    nasmArgs << "-f" << "elf64" << "-g" << "-F" << "dwarf"
             << source << "-l" << listing << "-o" << object;
    bool started = false;
    r.log += runTool(nasm, nasmArgs, includeDir, &started);
    if (!started) {
        r.log += QObject::tr("Unable to start assembler. Check your settings.") + '\n';
        return r;
    }
    if (!QFileInfo::exists(object))
        return r; // assembler errors are in the log

    //! Static executable, no C library: entry point is main.
    const QStringList lldArgs = { "-e", "main", "-o", executable, object };
    r.log += runTool(lld, lldArgs, QString(), &started);
    if (!started) {
        r.log += QObject::tr("Unable to start linker. Check your settings.") + '\n';
        return r;
    }
    r.ok = QFileInfo::exists(executable);
    r.executable = executable;
    return r;
}

bool wslAvailable(QString *details)
{
    QProcess p;
    p.start("wsl.exe", QStringList() << "--list" << "--quiet");
    if (!p.waitForStarted(5000)) {
        if (details)
            *details = QObject::tr("WSL is not installed (wsl.exe was not found).");
        return false;
    }
    p.waitForFinished(10000);
    //! wsl.exe prints UTF-16LE; drop the zero bytes to get plain ASCII names.
    QByteArray raw = p.readAllStandardOutput();
    raw.replace('\0', "");
    const QString names = QString::fromLatin1(raw).trimmed();
    if (p.exitCode() != 0 || names.isEmpty()) {
        if (details)
            *details = QObject::tr("WSL is installed but has no Linux distribution yet.");
        return false;
    }
    return true;
}

QStringList wslRunCommand(const QString &executable)
{
    const QFileInfo fi(executable);
    return QStringList() << "wsl.exe" << "--cd" << QDir::toNativeSeparators(fi.absolutePath())
                         << "--exec" << "./" + fi.fileName();
}

bool startWslInstall()
{
    return QProcess::startDetached("powershell.exe", QStringList()
        << "-NoProfile" << "-Command"
        << "Start-Process -Verb RunAs -FilePath wsl.exe -ArgumentList '--install'");
}

} // namespace LinuxTarget
