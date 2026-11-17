/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** BuildOutputView: what the assembler produced, in three tabs:
**   Listing      — NASM's .lst (address, bytes, source line)
**   Disassembly  — objdump -d, Intel syntax
**   Hex          — offset | hex | ASCII
** Disassembly and hex can show the object file (only your code) or the
** linked executable.
****************************************************************************/
#ifndef ASMON_BUILDOUTPUTVIEW_H
#define ASMON_BUILDOUTPUTVIEW_H

#include <QWidget>

class QPlainTextEdit;
class QTabWidget;
class QComboBox;
class QLabel;
class QCheckBox;

class BuildOutputView : public QWidget
{
    Q_OBJECT
public:
    explicit BuildOutputView(QWidget *parent = nullptr);

    //! Paths of the last build's products and the objdump to use.
    void setPaths(const QString &listing, const QString &object,
                  const QString &executable, const QString &objdump);

    //! Hex dump helper (also used by tests): at most maxBytes bytes.
    static QString hexDump(const QByteArray &data, qint64 baseOffset = 0);

    //! Switch to the Headers tab (objdump -x).
    void showHeaders();

public slots:
    void refresh();

private:
    QPlainTextEdit *makeView();
    QString selectedBinary() const;

    QTabWidget *tabs;
    QPlainTextEdit *listingView;
    QPlainTextEdit *disasmView;
    QPlainTextEdit *hexView;
    QPlainTextEdit *headersView;
    QComboBox *binaryChoice;
    QLabel *status;
    QCheckBox *hideIncluded;
    QString listingPath, objectPath, exePath, objdumpPath;
};

#endif // ASMON_BUILDOUTPUTVIEW_H
