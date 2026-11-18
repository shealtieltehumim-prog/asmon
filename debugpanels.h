/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** DebugPanels: the debugging / reverse-engineering views, shown only while a
** program is being debugged:
**   Registers   — values, changed ones highlighted, flags decoded, editable
**   Memory      — hex + ASCII at any address/label/expression, editable
**   Stack       — qwords (dwords on x86) from rsp, rsp/rbp marked
**   Disassembly — Intel syntax around the current instruction, go to address
**   Watch       — expressions with a type and size
****************************************************************************/
#ifndef ASMON_DEBUGPANELS_H
#define ASMON_DEBUGPANELS_H

#include "ruqplaintextedit.h"

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <functional>

class MiDebugger;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QTabWidget;

class DebugPanels : public QWidget
{
    Q_OBJECT
public:
    explicit DebugPanels(QWidget *parent = nullptr);

    void attach(MiDebugger *debugger);   //!< nullptr when the session ends
    void showTab(int index);
    enum { RegistersTab, MemoryTab, StackTab, DisassemblyTab, WatchTab, CallsTab };

    //! Value formatting used by Watch (static: unit-testable).
    enum WatchType { Auto, Hex, Binary, Signed, Unsigned, Char, Float };
    static QString formatValue(const QByteArray &bytes, int size, int type);
    //! "msg" -> "&msg", "rsp" -> "$rsp", "[rbp-8]" -> "($rbp-8)", "0x40" stays.
    static QString addressExpression(const QString &text, bool is64);
    static QString decodeFlags(quint64 eflags);
    //! name of the function a code line belongs to (for the call stack)
    std::function<QString(int line)> labelForLine;
    //! how a line id is shown (a module's lines include its file name)
    std::function<QString(int line)> lineName;

signals:
    void goToLine(int line);
    void message(const QString &text);

public slots:
    void refresh();
    void addWatch(const RuQPlainTextEdit::Watch &w);

private slots:
    void registerEdited(QTableWidgetItem *item);
    void memoryEdited(QTableWidgetItem *item);
    void goToMemory();
    void goToDisassembly();
    void addWatchFromBar();
    void removeWatch();

private:
    void refreshRegisters();
    void refreshMemory();
    void refreshStack();
    void refreshDisassembly();
    void refreshWatches();
    void refreshCalls();
    QWidget *makeCallsTab();
    QTableWidget *callsTable = nullptr;
    QWidget *makeRegistersTab();
    QWidget *makeMemoryTab();
    QWidget *makeStackTab();
    QWidget *makeDisassemblyTab();
    QWidget *makeWatchTab();
    void evaluateAddress(const QString &text, std::function<void(bool, quint64)> cb);

    QPointer<MiDebugger> dbg;
    QTabWidget *tabs;
    bool updating = false;

    // registers
    QTableWidget *regTable;
    QLabel *flagsLabel;
    QStringList regNames;            //!< gdb's numbering
    QStringList shownRegs;
    QHash<QString, QString> lastRegValues;

    // memory
    QLineEdit *memAddress;
    QTableWidget *memTable;
    quint64 memBase = 0;
    bool memValid = false;

    // stack
    QTableWidget *stackTable;

    // disassembly
    QLineEdit *disasmAddress;
    QPlainTextEdit *disasmView;
    QString disasmFixedExpr; //!< empty: follow the program counter

    // watch
    QLineEdit *watchExpr;
    QComboBox *watchType;
    QComboBox *watchSize;
    QSpinBox *watchCount;
    QTableWidget *watchTable;
    struct WatchItem { QString expr; int type; int size; int count; };
    QList<WatchItem> watches;
};

#endif // ASMON_DEBUGPANELS_H
