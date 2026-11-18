#include "decompiler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTimer>

namespace {

QString sanitize(const std::string &name)
{
    QString n = QString::fromStdString(name);
    const int bang = n.lastIndexOf('!');
    if (bang >= 0)
        n = n.mid(bang + 1);
    n.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    if (n.isEmpty() || n.at(0).isDigit())
        n.prepend('f');
    return n;
}

} // namespace

Decompiler::Decompiler(QObject *parent) : QObject(parent) {}

Decompiler::~Decompiler()
{
    cancel();
}

QString Decompiler::toolDir()
{
    const QString env = qEnvironmentVariable("ASMON_DECOMP_DIR");
    if (!env.isEmpty())
        return env;
    return QCoreApplication::applicationDirPath() + "/decomp";
}

static QString toolPath()
{
#ifdef Q_OS_WIN
    return Decompiler::toolDir() + "/asmon-decomp.exe";
#else
    return Decompiler::toolDir() + "/bin/asmon-decomp";
#endif
}

bool Decompiler::available()
{
    return QFile::exists(toolPath());
}

void Decompiler::cancel()
{
    hasPending_ = false;
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(2000);
        process_->deleteLater();
        process_ = nullptr;
    }
}

void Decompiler::request(std::shared_ptr<ra::Image> image, std::shared_ptr<ra::Result> result, quint64 va, int revision)
{
    Job job { std::move(image), std::move(result), va, revision };
    if (process_) {
        pending_ = job;
        hasPending_ = true;
        return;
    }
    start(job);
}

void Decompiler::start(const Job &job)
{
    running_ = job;
    if (!available()) {
        emit finished(job.va, job.revision, false, tr("The decompiler program was not found (%1).").arg(toolPath()));
        return;
    }
    if (!dir_.isValid()) {
        emit finished(job.va, job.revision, false, tr("Could not create a temporary folder."));
        return;
    }
    // the memory image: written again only when the bytes changed
    if (imageOf_ != job.image.get() || imageRevision_ != job.revision || imagePath_.isEmpty()) {
        imagePath_ = dir_.filePath("image.bin");
        QFile f(imagePath_);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit finished(job.va, job.revision, false, tr("Could not write the temporary image."));
            return;
        }
        const std::vector<uint8_t> mem = job.image->mapped();
        f.write(reinterpret_cast<const char *>(mem.data()), qint64(mem.size()));
        imageOf_ = job.image.get();
        imageRevision_ = job.revision;
    }

    const bool is64 = job.image->is64();
    QByteArray script;
    auto line = [&script](const QString &s) { script += s.toUtf8() + '\n'; };
    // the file name is relative to the working directory: the console splits its commands at spaces
    line(QString("load file %1 image.bin").arg(is64 ? "x86:LE:64:default:windows" : "x86:LE:32:default:windows"));
    line(QString("adjust vma 0x%1").arg(job.image->base(), 0, 16));
    // names of the other functions, so that calls read as calls to a name
    QSet<QString> used;
    const ra::Function *target = job.result->functionAt(job.va);
    const quint64 targetVa = target ? target->va : job.va;
    QString targetName = "func";
    for (const ra::Function &f : job.result->functions) {
        QString n = sanitize(job.result->functionName(f));
        if (used.contains(n))
            n += QString("_%1").arg(f.va, 0, 16);
        used.insert(n);
        if (f.va == targetVa) {
            targetName = n;
            continue;
        }
        line(QString("map function 0x%1 %2 nocode").arg(f.va, 0, 16).arg(n));
    }
    line(QString("map function 0x%1 %2").arg(targetVa, 0, 16).arg(targetName));
    line(QString("load function %1").arg(targetName));
    line("decompile");
    line("echo @@BEGIN@@");
    line("print C");
    line("echo @@END@@");
    line("quit");

    output_.clear();
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->setWorkingDirectory(dir_.path());
    connect(process_, &QProcess::readyReadStandardOutput, this, [this]() { output_ += process_->readAllStandardOutput(); });
    connect(process_, &QProcess::finished, this, &Decompiler::processDone);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            processDone(-1, QProcess::CrashExit);
    });
    QTimer::singleShot(60000, process_, [p = process_]() { if (p->state() != QProcess::NotRunning) p->kill(); });
    process_->start(toolPath(), { "-s", toolDir() + "/Ghidra/Processors/x86/data/languages" });
    process_->write(script);
    process_->closeWriteChannel();
}

void Decompiler::processDone(int exitCode, QProcess::ExitStatus status)
{
    QProcess *p = process_;
    process_ = nullptr;
    if (p) {
        output_ += p->readAllStandardOutput();
        p->deleteLater();
    }
    const QString all = QString::fromUtf8(output_);
    bool ok = false;
    QString text;
    const int e = all.lastIndexOf("@@END@@");
    const int b = e > 0 ? all.lastIndexOf("@@BEGIN@@", e) : -1;   // the first ones are the echo of the commands
    if (b >= 0 && e > b) {
        // the console echoes the commands: skip the echo line of "print C"
        text = all.mid(b + 9, e - b - 9);
        text.remove(QRegularExpression("^[^\\n]*\\n"));           // rest of the echo line
        text.remove(QRegularExpression("^\\[decomp\\]> print C\\n"));
        const int endPrompt = text.lastIndexOf("[decomp]>");
        if (endPrompt >= 0)
            text.truncate(endPrompt);
        text = text.trimmed();
        ok = !text.isEmpty();
    }
    if (!ok) {
        // the reason: the last "Execution error" or "Unable" line, else the tail of the output
        QString reason;
        for (const QString &l : all.split('\n'))
            if (l.contains("rror") || l.contains("Unable") || l.contains("Could not") || l.contains("No sleigh"))
                reason = l.trimmed();
        if (reason.isEmpty())
            reason = status == QProcess::CrashExit ? tr("The decompiler stopped unexpectedly.")
                                                   : tr("The decompiler returned nothing (exit code %1).").arg(exitCode);
        text = reason;
    }
    emit finished(running_.va, running_.revision, ok, text);
    if (hasPending_) {
        hasPending_ = false;
        const Job next = pending_;
        QTimer::singleShot(0, this, [this, next]() { if (!process_) start(next); });
    }
}
