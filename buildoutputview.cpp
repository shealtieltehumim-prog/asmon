/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "buildoutputview.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
const qint64 kMaxHexBytes = 256 * 1024;   // keep the view responsive
const int kMaxDisasmLines = 20000;
}

BuildOutputView::BuildOutputView(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    binaryChoice = new QComboBox;
    binaryChoice->addItem(tr("Object file (your code only)"));
    binaryChoice->addItem(tr("Executable (after linking)"));
    hideIncluded = new QCheckBox(tr("Hide included files and macro expansions"));
    hideIncluded->setChecked(true);
    status = new QLabel;
    status->setForegroundRole(QPalette::PlaceholderText);
    bar->addWidget(new QLabel(tr("Show:")));
    bar->addWidget(binaryChoice);
    bar->addSpacing(12);
    bar->addWidget(hideIncluded);
    bar->addStretch(1);
    bar->addWidget(status);
    layout->addLayout(bar);

    tabs = new QTabWidget;
    listingView = makeView();
    disasmView = makeView();
    hexView = makeView();
    tabs->addTab(listingView, tr("Listing"));
    tabs->addTab(disasmView, tr("Disassembly"));
    tabs->addTab(hexView, tr("Hex"));
    headersView = makeView();
    tabs->addTab(headersView, tr("Headers"));
    layout->addWidget(tabs);

    connect(binaryChoice, SIGNAL(currentIndexChanged(int)), this, SLOT(refresh()));
    connect(hideIncluded, SIGNAL(toggled(bool)), this, SLOT(refresh()));
}

QPlainTextEdit *BuildOutputView::makeView()
{
    auto *v = new QPlainTextEdit;
    v->setReadOnly(true);
    v->setWordWrapMode(QTextOption::NoWrap);
    v->setLayoutDirection(Qt::LeftToRight);
    QFont f(QSettings().value("fontfamily", Theme::defaultCodeFont()).toString());
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(QSettings().value("fontsize", 11).toInt());
    v->setFont(f);
    return v;
}

void BuildOutputView::setPaths(const QString &listing, const QString &object,
                               const QString &executable, const QString &objdump)
{
    listingPath = listing;
    objectPath = object;
    exePath = executable;
    objdumpPath = objdump;
}

QString BuildOutputView::selectedBinary() const
{
    return binaryChoice->currentIndex() == 0 ? objectPath : exePath;
}

QString BuildOutputView::hexDump(const QByteArray &data, qint64 baseOffset)
{
    QString out;
    out.reserve(data.size() * 4 + 64);
    for (qint64 i = 0; i < data.size(); i += 16) {
        out += QString("%1  ").arg(baseOffset + i, 8, 16, QChar('0'));
        QString ascii;
        for (int j = 0; j < 16; ++j) {
            if (i + j < data.size()) {
                const unsigned char c = static_cast<unsigned char>(data[int(i + j)]);
                out += QString("%1 ").arg(c, 2, 16, QChar('0'));
                ascii += (c >= 0x20 && c < 0x7f) ? QChar(c) : QChar('.');
            } else {
                out += "   ";
            }
            if (j == 7)
                out += ' ';
        }
        out += " |" + ascii + "|\n";
    }
    return out;
}

void BuildOutputView::refresh()
{
    //! Listing
    QFile lst(listingPath);
    if (lst.open(QIODevice::ReadOnly)) {
        QString text = QString::fromUtf8(lst.readAll());
        if (hideIncluded->isChecked()) {
            //! NASM marks lines from %include files and macro expansions with
            //! a nesting level "<1>", "<2>" ... before the source text.
            static const QRegularExpression nested("^\\s*\\d+\\s+(?:[0-9A-Fa-f]{8,16}\\s+)?(?:\\S+\\s+)?<\\d+>");
            QStringList kept;
            for (const QString &line : text.split('\n'))
                if (!nested.match(line).hasMatch())
                    kept << line;
            text = kept.join('\n');
        }
        listingView->setPlainText(text);
    }
    else
        listingView->setPlainText(tr("No listing yet: build the program first."));

    const QString bin = selectedBinary();
    if (!QFileInfo::exists(bin)) {
        disasmView->setPlainText(tr("Nothing built yet."));
        hexView->clear();
        status->clear();
        return;
    }

    //! Disassembly (Intel syntax)
    QProcess objdump;
    QStringList args;
    args << "-d" << "-M" << "intel" << "-w";
    if (binaryChoice->currentIndex() == 0)
        args << "-r"; // show relocations for calls into the C library
    args << bin;
    objdump.start(objdumpPath, args);
    if (!objdump.waitForFinished(15000) || objdump.exitStatus() != QProcess::NormalExit) {
        disasmView->setPlainText(tr("Could not run objdump (%1).").arg(objdumpPath));
    } else {
        QString text = QString::fromLocal8Bit(objdump.readAllStandardOutput());
        const QStringList lines = text.split('\n');
        if (lines.size() > kMaxDisasmLines)
            text = lines.mid(0, kMaxDisasmLines).join('\n')
                   + "\n" + tr("... (cut after %1 lines)").arg(kMaxDisasmLines);
        if (text.trimmed().isEmpty())
            text = QString::fromLocal8Bit(objdump.readAllStandardError());
        disasmView->setPlainText(text);
    }

    //! Headers and sections (binutils/llvm objdump -x)
    QProcess headers;
    headers.start(objdumpPath, QStringList() << "-x" << bin);
    if (headers.waitForFinished(15000) && headers.exitStatus() == QProcess::NormalExit)
        headersView->setPlainText(QString::fromLocal8Bit(headers.readAllStandardOutput()
                                                          + headers.readAllStandardError()));
    else
        headersView->setPlainText(tr("Could not run objdump (%1).").arg(objdumpPath));

    //! Hex
    QFile f(bin);
    if (f.open(QIODevice::ReadOnly)) {
        const qint64 size = f.size();
        const QByteArray data = f.read(kMaxHexBytes);
        QString text = hexDump(data);
        if (size > kMaxHexBytes)
            text += tr("... (showing the first %1 KB of %2 KB)").arg(kMaxHexBytes / 1024).arg(size / 1024);
        hexView->setPlainText(text);
        status->setText(QString("%1 — %2 B").arg(QFileInfo(bin).fileName()).arg(size));
    }
}

void BuildOutputView::showHeaders()
{
    tabs->setCurrentWidget(headersView);
}
