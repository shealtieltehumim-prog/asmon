/****************************************************************************
** Asmon — GPL-3.0.
**
** Decompiler: runs the Ghidra decompiler engine (the C++ part of Ghidra,
** Apache-2.0, built without Java) as a separate small program, "asmon-decomp",
** and returns the C code of one function. The program is given the memory
** image of the opened file and the names of all known functions.
****************************************************************************/
#ifndef ASMON_DECOMPILER_H
#define ASMON_DECOMPILER_H

#include "native/analysis.h"

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>

class Decompiler : public QObject
{
    Q_OBJECT
public:
    explicit Decompiler(QObject *parent = nullptr);
    ~Decompiler() override;

    //! Folder with asmon-decomp and the processor descriptions (ASMON_DECOMP_DIR overrides it).
    static QString toolDir();
    static bool available();

    //! Asks for the C code of the function at va. Only the latest request is kept while one runs.
    //! revision: changes when bytes or names change, so that old answers are not reused.
    void request(std::shared_ptr<ra::Image> image, std::shared_ptr<ra::Result> result, quint64 va, int revision);
    void cancel();

signals:
    //! ok: text is the code; otherwise text is the reason.
    void finished(quint64 va, int revision, bool ok, const QString &text);

private:
    struct Job {
        std::shared_ptr<ra::Image> image;
        std::shared_ptr<ra::Result> result;
        quint64 va = 0;
        int revision = 0;
    };
    void start(const Job &job);
    void processDone(int exitCode, QProcess::ExitStatus status);

    QProcess *process_ = nullptr;
    QTemporaryDir dir_;
    Job running_, pending_;
    bool hasPending_ = false;
    int imageRevision_ = -1;
    const ra::Image *imageOf_ = nullptr;
    QString imagePath_;
    QByteArray output_;
};

#endif // ASMON_DECOMPILER_H
