/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** LinuxTarget: build Linux x64 ELF programs on Windows and run them in WSL.
**
** Building needs no WSL: the bundled NASM writes elf64 and LLVM's ld.lld
** links a static executable with "main" as the entry point. There is no C
** library, so programs use Linux system calls directly (see the
** "Hello world — Linux x64" template); io64.inc is not available here.
** Running needs WSL ("wsl.exe --cd <dir> --exec ./program").
**
** The code is platform independent (only QProcess), so the build step can be
** tested on Linux with native nasm and ld.lld.
****************************************************************************/
#ifndef ASMON_LINUXTARGET_H
#define ASMON_LINUXTARGET_H

#include <QString>
#include <QStringList>

namespace LinuxTarget {

struct BuildResult {
    bool ok = false;
    QString log;        //!< assembler + linker output
    QString executable; //!< path of the ELF file when ok
};

//! Assemble + link. sourceDir (may be empty) is added to NASM's include path.
//! modules: more .asm files (from asmon.project) assembled and linked too.
BuildResult build(const QString &nasm, const QString &lld, const QString &source,
                  const QString &listing, const QString &object, const QString &executable,
                  const QString &includeDir, const QString &sourceDir,
                  const QStringList &modules = QStringList());

//! True when wsl.exe exists and at least one Linux distribution is installed.
//! details receives a short human-readable reason when false.
bool wslAvailable(QString *details = nullptr);

//! Program + arguments that run an ELF file (by Windows path) inside WSL.
QStringList wslRunCommand(const QString &executable);

//! Starts "wsl --install" with administrator rights (UAC prompt).
bool startWslInstall();

} // namespace LinuxTarget

#endif // ASMON_LINUXTARGET_H
