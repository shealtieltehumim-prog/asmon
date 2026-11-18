/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "debugpanels.h"
#include "debugbackend.h"
#include "theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QVBoxLayout>
#include <cstring>

namespace {

QFont codeFont()
{
    QFont f = Theme::codeFont();
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(QSettings().value("fontsize", 11).toInt());
    return f;
}

QTableWidget *makeTable(int columns, const QStringList &headers)
{
    auto *t = new QTableWidget(0, columns);
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setLayoutDirection(Qt::LeftToRight); // numbers and addresses read LTR
    t->setFont(codeFont());
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->verticalHeader()->setDefaultSectionSize(QFontMetrics(t->font()).height() + 6);
    return t;
}

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *i = new QTableWidgetItem(text);
    i->setFlags(i->flags() & ~Qt::ItemIsEditable);
    return i;
}

const QStringList kRegs64 = { "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
                              "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
                              "rip", "eflags", "cs", "ss", "ds", "es", "fs", "gs" };
const QStringList kRegs32 = { "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
                              "eip", "eflags", "cs", "ss", "ds", "es", "fs", "gs" };

} // namespace

DebugPanels::DebugPanels(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs = new QTabWidget;
    tabs->addTab(makeRegistersTab(), tr("Registers"));
    tabs->addTab(makeMemoryTab(), tr("Memory"));
    tabs->addTab(makeStackTab(), tr("Stack"));
    tabs->addTab(makeDisassemblyTab(), tr("Disassembly"));
    tabs->addTab(makeWatchTab(), tr("Watch"));
    tabs->addTab(makeCallsTab(), tr("Calls"));
    layout->addWidget(tabs);
    connect(tabs, &QTabWidget::currentChanged, this, &DebugPanels::refresh);
}

void DebugPanels::showTab(int index)
{
    tabs->setCurrentIndex(index);
}

void DebugPanels::attach(DebugBackend *debugger)
{
    dbg = debugger;
    lastRegValues.clear();
    disasmFixedExpr.clear();
    if (!dbg)
        return;
    connect(dbg, &DebugBackend::stopped, this, [this]() { refresh(); });
    connect(dbg, &DebugBackend::crashed, this, [this]() { refresh(); });
}

void DebugPanels::refresh()
{
    if (!dbg || !dbg->isStopped())
        return;
    //! Registers feed the flags line and change markers: always refresh them.
    refreshRegisters();
    switch (tabs->currentIndex()) {
    case MemoryTab: refreshMemory(); break;
    case StackTab: refreshStack(); break;
    case DisassemblyTab: refreshDisassembly(); break;
    case WatchTab: refreshWatches(); break;
    case CallsTab: refreshCalls(); break;
    default: break;
    }
}

// ------------------------------------------------------------------ helpers

void DebugPanels::evaluateAddress(const QString &text, std::function<void(bool, quint64)> cb)
{
    if (!dbg) {
        cb(false, 0);
        return;
    }
    dbg->evaluateAddress(text, cb);
}

QString DebugPanels::decodeFlags(quint64 f)
{
    struct Flag { int bit; const char *name; };
    static const Flag flags[] = { {0, "CF"}, {2, "PF"}, {4, "AF"}, {6, "ZF"}, {7, "SF"},
                                  {8, "TF"}, {9, "IF"}, {10, "DF"}, {11, "OF"} };
    QStringList parts;
    for (const Flag &fl : flags)
        parts << QString("%1=%2").arg(fl.name).arg((f >> fl.bit) & 1);
    return parts.join("  ");
}

QString DebugPanels::formatValue(const QByteArray &bytes, int size, int type)
{
    if (bytes.size() < size || size <= 0)
        return QString();
    quint64 u = 0;
    std::memcpy(&u, bytes.constData(), size_t(size)); // x86 is little-endian
    qint64 s = 0;
    switch (size) {
    case 1: s = qint8(u); break;
    case 2: s = qint16(u); break;
    case 4: s = qint32(u); break;
    default: s = qint64(u); break;
    }
    switch (type) {
    case Hex: return QString("0x%1").arg(u, size * 2, 16, QChar('0'));
    case Binary: return QString("%1").arg(u, size * 8, 2, QChar('0'));
    case Signed: return QString::number(s);
    case Unsigned: return QString::number(u);
    case Char: {
        QString out;
        for (int i = 0; i < bytes.size(); ++i) {
            const unsigned char c = uchar(bytes[i]);
            if (c == 0)
                break;
            out += (c >= 0x20 && c < 0x7f) ? QChar(c) : QChar('.');
        }
        return "'" + out + "'";
    }
    case Float:
        if (size == 4) { float f; std::memcpy(&f, bytes.constData(), 4); return QString::number(double(f), 'g', 9); }
        if (size == 8) { double d; std::memcpy(&d, bytes.constData(), 8); return QString::number(d, 'g', 17); }
        return QString("-");
    default: // Auto
        return QString("%1  (0x%2)").arg(s).arg(u, size * 2, 16, QChar('0'));
    }
}

// ------------------------------------------------------------------ registers

QWidget *DebugPanels::makeRegistersTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    regTable = makeTable(3, { tr("Register"), tr("Hex (double-click to edit)"), tr("Decimal") });
    connect(regTable, &QTableWidget::itemChanged, this, &DebugPanels::registerEdited);
    flagsLabel = new QLabel;
    flagsLabel->setFont(codeFont());
    flagsLabel->setLayoutDirection(Qt::LeftToRight);
    flagsLabel->setContentsMargins(6, 4, 6, 4);
    flagsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    //! wrap instead of forcing the whole debugger panel to be wide
    flagsLabel->setWordWrap(true);
    flagsLabel->setMinimumWidth(1);
    l->addWidget(flagsLabel);
    l->addWidget(regTable);
    return w;
}

void DebugPanels::refreshRegisters()
{
    if (!dbg)
        return;
    const bool x64 = dbg->is64Bit();
    const QStringList &wanted = x64 ? kRegs64 : kRegs32;
    dbg->readRegisters(wanted, [this, x64](const QList<DebugBackend::Register> &regs) {
        updating = true;
        regTable->setRowCount(regs.size());
        const QColor changed = Theme::isDark() ? QColor("#f9c74f") : QColor("#b5590e");
        for (int row = 0; row < regs.size(); ++row) {
            const QString name = regs[row].name;
            const quint64 u = regs[row].value;
            const QString value = QString("0x%1").arg(u, 0, 16);
            auto *nameItem = readOnlyItem(name);
            auto *hexItem = new QTableWidgetItem(value);
            QString dec;
            if (name == "eflags")
                dec = decodeFlags(u);
            else if (name.size() == 3 && (name.startsWith('e') || name.startsWith('r')) && name != "rip" && name != "eip")
                dec = x64 ? QString::number(qint64(u)) : QString::number(qint32(u));
            else
                dec = QString::number(u);
            auto *decItem = readOnlyItem(dec);
            if (lastRegValues.contains(name) && lastRegValues.value(name) != value) {
                QFont bold = hexItem->font();
                bold.setBold(true);
                for (QTableWidgetItem *it : { nameItem, hexItem, decItem }) {
                    it->setForeground(changed);
                    it->setFont(bold);
                }
            }
            lastRegValues.insert(name, value);
            regTable->setItem(row, 0, nameItem);
            regTable->setItem(row, 1, hexItem);
            regTable->setItem(row, 2, decItem);
            if (name == "eflags")
                flagsLabel->setText(tr("Flags: ") + decodeFlags(u));
        }
        regTable->resizeColumnToContents(0);
        regTable->resizeColumnToContents(1);
        updating = false;
    });
}

void DebugPanels::registerEdited(QTableWidgetItem *item)
{
    if (updating || !dbg || item->column() != 1)
        return;
    const QString name = regTable->item(item->row(), 0)->text();
    const QString value = item->text().trimmed();
    dbg->writeRegister(name, value, [this](bool ok, const QString &msg) {
        if (!ok)
            flagsLabel->setText(msg);
        refresh();
    });
}

// ------------------------------------------------------------------ memory

QWidget *DebugPanels::makeMemoryTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    memAddress = new QLineEdit;
    memAddress->setLayoutDirection(Qt::LeftToRight);
    memAddress->setPlaceholderText(tr("address, label, register or [rbp-8]"));
    auto *go = new QPushButton(tr("Go"));
    connect(memAddress, &QLineEdit::returnPressed, this, &DebugPanels::goToMemory);
    connect(go, &QPushButton::clicked, this, &DebugPanels::goToMemory);
    bar->addWidget(new QLabel(tr("Address:")));
    bar->addWidget(memAddress, 1);
    bar->addWidget(go);
    l->addLayout(bar);
    QStringList headers;
    headers << tr("Address");
    for (int i = 0; i < 16; ++i)
        headers << QString("%1").arg(i, 2, 16, QChar('0')).toUpper();
    headers << "ASCII";
    memTable = makeTable(18, headers);
    for (int c = 1; c <= 16; ++c)
        memTable->setColumnWidth(c, QFontMetrics(memTable->font()).horizontalAdvance("000"));
    connect(memTable, &QTableWidget::itemChanged, this, &DebugPanels::memoryEdited);
    l->addWidget(memTable);
    return w;
}

void DebugPanels::goToMemory()
{
    evaluateAddress(memAddress->text(), [this](bool ok, quint64 a) {
        if (!ok) {
            memTable->setRowCount(0);
            memValid = false;
            return;
        }
        memBase = a;
        memValid = true;
        refreshMemory();
    });
}

void DebugPanels::refreshMemory()
{
    if (!dbg)
        return;
    if (!memValid) {
        if (memAddress->text().isEmpty())
            memAddress->setText(dbg->is64Bit() ? "rsp" : "esp");
        goToMemory();
        return;
    }
    const int rows = 16;
    dbg->readMemory(memBase, rows * 16, [this, rows](bool ok, const QByteArray &bytes) {
        updating = true;
        memTable->setRowCount(rows);
        const QByteArray data = ok ? bytes : QByteArray();
        for (int row = 0; row < rows; ++row) {
            const quint64 a = memBase + quint64(row) * 16;
            memTable->setItem(row, 0, readOnlyItem(QString("%1").arg(a, 16, 16, QChar('0'))));
            QString ascii;
            for (int c = 0; c < 16; ++c) {
                const int i = row * 16 + c;
                QTableWidgetItem *it;
                if (i < data.size()) {
                    const uchar b = uchar(data[i]);
                    it = new QTableWidgetItem(QString("%1").arg(b, 2, 16, QChar('0')).toUpper());
                    ascii += (b >= 0x20 && b < 0x7f) ? QChar(b) : QChar('.');
                } else {
                    it = readOnlyItem("??");
                    ascii += ' ';
                }
                memTable->setItem(row, c + 1, it);
            }
            memTable->setItem(row, 17, readOnlyItem(ascii));
        }
        memTable->resizeColumnToContents(0);
        updating = false;
    });
}

void DebugPanels::memoryEdited(QTableWidgetItem *item)
{
    if (updating || !dbg || item->column() < 1 || item->column() > 16)
        return;
    bool ok = false;
    const uint b = item->text().trimmed().toUInt(&ok, 16);
    if (!ok || b > 0xff) {
        refreshMemory();
        return;
    }
    const quint64 a = memBase + quint64(item->row()) * 16 + quint64(item->column() - 1);
    dbg->writeMemory(a, QByteArray(1, char(b)), [this](bool) { refresh(); });
}

// ------------------------------------------------------------------ stack

QWidget *DebugPanels::makeStackTab()
{
    stackTable = makeTable(3, { tr("Address"), tr("Value"), QString() });
    return stackTable;
}

void DebugPanels::refreshStack()
{
    if (!dbg)
        return;
    const bool x64 = dbg->is64Bit();
    const int width = x64 ? 8 : 4;
    const int count = 24;
    const QString sp = x64 ? "rsp" : "esp";
    const QString bp = x64 ? "rbp" : "ebp";
    dbg->readRegisters({ sp, bp }, [=](const QList<DebugBackend::Register> &regs) {
        quint64 spValue = 0, bpValue = 0;
        for (const DebugBackend::Register &r : regs)
            (r.name == sp ? spValue : bpValue) = r.value;
        dbg->readMemory(spValue, width * count, [=](bool ok, const QByteArray &data) {
            if (!ok)
                return;
            const quint64 base = spValue;
            stackTable->setRowCount(count);
            for (int i = 0; i < count; ++i) {
                const quint64 a = base + quint64(i * width);
                quint64 v = 0;
                if ((i + 1) * width <= data.size())
                    std::memcpy(&v, data.constData() + i * width, size_t(width));
                QString note;
                if (i == 0)
                    note = QString("← %1").arg(sp);
                if (a == bpValue)
                    note += (note.isEmpty() ? "" : "  ") + QString("← %1").arg(bp);
                const int line = dbg->lineForAddress(v);
                if (line > 0)
                    note += (note.isEmpty() ? "" : "  ") + tr("→ code, line %1").arg(line);
                stackTable->setItem(i, 0, readOnlyItem(QString("%1").arg(a, width * 2, 16, QChar('0'))));
                stackTable->setItem(i, 1, readOnlyItem(QString("%1").arg(v, width * 2, 16, QChar('0'))));
                stackTable->setItem(i, 2, readOnlyItem(note));
            }
            stackTable->resizeColumnToContents(0);
            stackTable->resizeColumnToContents(1);
        });
    });
}

// ------------------------------------------------------------------ disassembly

QWidget *DebugPanels::makeDisassemblyTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    disasmAddress = new QLineEdit;
    disasmAddress->setLayoutDirection(Qt::LeftToRight);
    disasmAddress->setPlaceholderText(tr("empty = follow the current instruction"));
    auto *go = new QPushButton(tr("Go"));
    connect(disasmAddress, &QLineEdit::returnPressed, this, &DebugPanels::goToDisassembly);
    connect(go, &QPushButton::clicked, this, &DebugPanels::goToDisassembly);
    bar->addWidget(new QLabel(tr("Address:")));
    bar->addWidget(disasmAddress, 1);
    bar->addWidget(go);
    l->addLayout(bar);
    disasmView = new QPlainTextEdit;
    disasmView->setReadOnly(true);
    disasmView->setWordWrapMode(QTextOption::NoWrap);
    disasmView->setLayoutDirection(Qt::LeftToRight);
    disasmView->setFont(codeFont());
    l->addWidget(disasmView);
    return w;
}

void DebugPanels::goToDisassembly()
{
    disasmFixedExpr = disasmAddress->text().trimmed();
    refreshDisassembly();
}

void DebugPanels::refreshDisassembly()
{
    if (!dbg)
        return;
    auto show = [this](quint64 start) {
        const quint64 pc = dbg->currentPc();
        dbg->disassemble(start, 192, [this, pc](const QList<DebugBackend::Instruction> &insns) {
            QStringList out;
            int currentRow = -1;
            for (const DebugBackend::Instruction &i : insns) {
                if (i.address == 0 && i.bytes.isEmpty()) {      // an error message
                    out << i.text;
                    continue;
                }
                const int line = dbg->lineForAddress(i.address);
                if (i.address == pc)
                    currentRow = out.size();
                const QString bytes = i.bytes.isEmpty() ? QString()
                                                        : QString::fromLatin1(i.bytes.toHex(' ')) + "  ";
                out << QString("%1 0x%2  %3  %4%5%6")
                           .arg(i.address == pc ? "=>" : "  ")
                           .arg(i.address, 0, 16)
                           .arg(i.symbol, -18)
                           .arg(bytes, -(i.bytes.isEmpty() ? 0 : 26))
                           .arg(i.text, -36)
                           .arg(line > 0 ? QString(" ; line %1").arg(line) : QString());
            }
            disasmView->setPlainText(out.join('\n'));
            if (currentRow >= 0) {
                QTextCursor c(disasmView->document()->findBlockByNumber(currentRow));
                disasmView->setTextCursor(c);
                QTextEdit::ExtraSelection sel;
                sel.cursor = c;
                sel.format.setBackground(Theme::color("debuglinecolor"));
                sel.format.setProperty(QTextFormat::FullWidthSelection, true);
                disasmView->setExtraSelections({ sel });
                disasmView->centerCursor();
            } else {
                disasmView->setExtraSelections({});
            }
        });
    };
    if (disasmFixedExpr.isEmpty()) {
        //! Start a little before pc at a known instruction boundary when possible.
        const quint64 pc = dbg->currentPc();
        quint64 start = pc;
        for (quint64 back = 1; back <= 48 && pc >= back; ++back)
            if (dbg->lineForAddress(pc - back) > 0)
                start = pc - back;
        show(start);
    } else {
        evaluateAddress(disasmFixedExpr, [this, show](bool ok, quint64 a) {
            if (ok)
                show(a);
            else
                disasmView->setPlainText(tr("Unknown address: %1").arg(disasmFixedExpr));
        });
    }
}

// ------------------------------------------------------------------ calls

QWidget *DebugPanels::makeCallsTab()
{
    callsTable = makeTable(3, { "#", tr("Function"), tr("Line") });
    connect(callsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const int line = callsTable->item(row, 2) ? callsTable->item(row, 2)->data(Qt::UserRole).toInt() : 0;
        if (line > 0)
            emit goToLine(line);
    });
    return callsTable;
}

void DebugPanels::refreshCalls()
{
    if (!dbg)
        return;
    dbg->callStack([this](const QList<DebugBackend::Frame> &frames) {
        callsTable->setRowCount(frames.size());
        for (int i = 0; i < frames.size(); ++i) {
            const DebugBackend::Frame &f = frames.at(i);
            const QString name = (f.line > 0 && labelForLine) ? labelForLine(f.line) : QString();
            callsTable->setItem(i, 0, readOnlyItem(QString::number(i)));
            callsTable->setItem(i, 1, readOnlyItem(name.isEmpty()
                ? QString("0x%1").arg(f.address, 0, 16) : name));
            auto *lineItem = readOnlyItem(f.line > 0 ? (lineName ? lineName(f.line) : QString::number(f.line))
                                                      : tr("outside your code"));
            lineItem->setData(Qt::UserRole, f.line);
            callsTable->setItem(i, 2, lineItem);
        }
        callsTable->resizeColumnToContents(0);
        callsTable->resizeColumnToContents(1);
    });
}

// ------------------------------------------------------------------ watch

QWidget *DebugPanels::makeWatchTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    watchExpr = new QLineEdit;
    watchExpr->setLayoutDirection(Qt::LeftToRight);
    watchExpr->setPlaceholderText(tr("label, register or [rbp-8]"));
    watchType = new QComboBox;
    watchType->addItems({ tr("Auto"), tr("Hex"), tr("Binary"), tr("Signed"), tr("Unsigned"),
                          tr("Characters"), tr("Float") });
    watchSize = new QComboBox;
    watchSize->addItems({ "1", "2", "4", "8" });
    watchSize->setCurrentIndex(3);
    watchCount = new QSpinBox;
    watchCount->setRange(1, 64);
    watchCount->setPrefix("× ");
    auto *add = new QPushButton(tr("Add"));
    auto *remove = new QPushButton(tr("Remove"));
    connect(watchExpr, &QLineEdit::returnPressed, this, &DebugPanels::addWatchFromBar);
    connect(add, &QPushButton::clicked, this, &DebugPanels::addWatchFromBar);
    connect(remove, &QPushButton::clicked, this, &DebugPanels::removeWatch);
    bar->addWidget(watchExpr, 1);
    bar->addWidget(watchType);
    bar->addWidget(new QLabel(tr("bytes:")));
    bar->addWidget(watchSize);
    bar->addWidget(watchCount);
    bar->addWidget(add);
    bar->addWidget(remove);
    l->addLayout(bar);
    //! second row: stop when the program writes to the expression's memory
    auto *bar2 = new QHBoxLayout;
    bar2->setContentsMargins(6, 0, 6, 4);
    auto *breakWrite = new QPushButton(tr("Stop when written"));
    breakWrite->setToolTip(tr("Stops the program right after an instruction writes to this memory "
                              "(the address of the expression, the number of bytes chosen)"));
    auto *clearWrites = new QPushButton(tr("Clear memory stops"));
    bar2->addWidget(breakWrite);
    bar2->addWidget(clearWrites);
    bar2->addStretch(1);
    l->addLayout(bar2);
    connect(breakWrite, &QPushButton::clicked, this, [this]() {
        const QString e = watchExpr->text().trimmed();
        if (!dbg || e.isEmpty()) {
            emit message(tr("Type a label, register or [rbp-8] first."));
            return;
        }
        const int size = watchSize->currentText().toInt();
        dbg->watchMemory(e, size, e, [this, e, size](bool ok, const QString &msg) {
            emit message(ok ? tr("Will stop when %1 bytes at %2 are written.").arg(size).arg(e)
                            : tr("Could not watch %1: %2").arg(e, msg));
        });
    });
    connect(clearWrites, &QPushButton::clicked, this, [this]() {
        if (dbg)
            dbg->clearMemoryWatches();
        emit message(tr("Memory stops cleared."));
    });
    watchTable = makeTable(3, { tr("Expression"), tr("Type"), tr("Value") });
    l->addWidget(watchTable);
    return w;
}

void DebugPanels::addWatchFromBar()
{
    const QString e = watchExpr->text().trimmed();
    if (e.isEmpty())
        return;
    watches << WatchItem{ e, watchType->currentIndex(), watchSize->currentText().toInt(), watchCount->value() };
    watchExpr->clear();
    showTab(WatchTab);
    refreshWatches();
}

void DebugPanels::addWatch(const RuQPlainTextEdit::Watch &w)
{
    if (w.name.isEmpty())
        return;
    //! SASM's sizes: 0=d(4) 1=w(2) 2=b(1) 3=q(8); db strings are shown as characters.
    static const int sizes[] = { 4, 2, 1, 8 };
    const int size = (w.size >= 0 && w.size < 4) ? sizes[w.size] : 4;
    const int type = size == 1 ? Char : Auto;
    const int count = size == 1 ? 32 : qMax(1, w.arraySize);
    watches << WatchItem{ w.name, type, size, count };
    showTab(WatchTab);
    refreshWatches();
}

void DebugPanels::removeWatch()
{
    const int row = watchTable->currentRow();
    if (row >= 0 && row < watches.size()) {
        watches.removeAt(row);
        refreshWatches();
    }
}

void DebugPanels::refreshWatches()
{
    watchTable->setRowCount(watches.size());
    for (int row = 0; row < watches.size(); ++row) {
        const WatchItem w = watches[row];
        watchTable->setItem(row, 0, readOnlyItem(w.expr));
        watchTable->setItem(row, 1, readOnlyItem(QString("%1 ×%2 (%3 B)")
                                                     .arg(watchType->itemText(w.type)).arg(w.count).arg(w.size)));
        if (!dbg || !dbg->isStopped()) {
            watchTable->setItem(row, 2, readOnlyItem("-"));
            continue;
        }
        static const QRegularExpression isReg("^\\$?(r[a-d]x|r[sd]i|r[sb]p|r\\d{1,2}|e[a-d]x|e[sd]i|e[sb]p|rip|eip|eflags)$",
                                              QRegularExpression::CaseInsensitiveOption);
        if (isReg.match(w.expr).hasMatch()) {
            //! a register: format its value
            QString r = w.expr.toLower();
            if (r.startsWith('$'))
                r.remove(0, 1);
            dbg->readRegisters({ r }, [this, row, w](const QList<DebugBackend::Register> &regs) {
                const quint64 v = regs.isEmpty() ? 0 : regs.first().value;
                QByteArray b(reinterpret_cast<const char *>(&v), 8);
                if (row < watchTable->rowCount())
                    watchTable->setItem(row, 2, readOnlyItem(regs.isEmpty() ? tr("unknown")
                                                                            : formatValue(b, w.size, w.type)));
            });
            continue;
        }
        evaluateAddress(w.expr, [this, row, w](bool ok, quint64 a) {
            if (!ok) {
                if (row < watchTable->rowCount())
                    watchTable->setItem(row, 2, readOnlyItem(tr("unknown")));
                return;
            }
            dbg->readMemory(a, w.size * w.count, [this, row, w](bool readOk, const QByteArray &data) {
                QString text;
                if (readOk) {
                    if (w.type == Char) {
                        text = formatValue(data, 1, Char);
                    } else {
                        QStringList parts;
                        for (int i = 0; i < w.count; ++i)
                            parts << formatValue(data.mid(i * w.size, w.size), w.size, w.type);
                        text = parts.join(", ");
                    }
                } else {
                    text = tr("cannot read memory");
                }
                if (row < watchTable->rowCount())
                    watchTable->setItem(row, 2, readOnlyItem(text));
            });
        });
    }
    watchTable->resizeColumnToContents(0);
}
