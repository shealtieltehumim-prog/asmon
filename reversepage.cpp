#include "reversepage.h"

#include "cardframe.h"
#include "hexview.h"
#include "theme.h"

#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <functional>

namespace {

//! One card of the page: a small muted heading and the content below it.
QWidget *makeCard(const QString &heading, QWidget *body)
{
    QWidget *content = new QWidget;
    QVBoxLayout *l = new QVBoxLayout(content);
    l->setContentsMargins(10, 8, 10, 8);
    l->setSpacing(6);
    QLabel *title = new QLabel(heading);
    title->setObjectName("cardHeading");
    l->addWidget(title);
    l->addWidget(body, 1);
    return new CardFrame(content);
}

QTableWidget *makeTable(const QStringList &headers)
{
    QTableWidget *t = new QTableWidget(0, headers.size());
    t->setLayoutDirection(Qt::LeftToRight);   // addresses and code read left to right
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setShowGrid(false);
    t->setAlternatingRowColors(false);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(QFontMetrics(Theme::codeFont()).height() + 6);
    t->setFont(Theme::codeFont());
    t->horizontalHeader()->setStretchLastSection(true);
    t->setContextMenuPolicy(Qt::CustomContextMenu);
    return t;
}

QString hex(quint64 v) { return QString::number(v, 16); }

bool parseAddress(const QString &textIn, quint64 &out)
{
    QString t = textIn.trimmed();
    if (t.startsWith("0x", Qt::CaseInsensitive))
        t = t.mid(2);
    bool ok = false;
    out = t.toULongLong(&ok, 16);
    return ok && !t.isEmpty();
}

} // namespace

ReversePage::ReversePage(QWidget *parent) : QWidget(parent)
{
    QHBoxLayout *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(8);
    splitter->setChildrenCollapsible(false);
    row->addWidget(splitter);

    // ---- first card (right edge in the Hebrew interface): functions, strings, imports, info
    listTabs_ = new QTabWidget;
    listTabs_->setDocumentMode(true);
    listTabs_->setLayoutDirection(Qt::LeftToRight);
    {
        QWidget *page = new QWidget;
        QVBoxLayout *l = new QVBoxLayout(page);
        l->setContentsMargins(0, 4, 0, 0);
        functionFilter_ = new QLineEdit;
        functionFilter_->setPlaceholderText(tr("Search functions"));
        functionFilter_->setClearButtonEnabled(true);
        functionList_ = new QListWidget;
        functionList_->setFont(Theme::codeFont());
        functionList_->setLayoutDirection(Qt::LeftToRight);
        l->addWidget(functionFilter_);
        l->addWidget(functionList_, 1);
        listTabs_->addTab(page, tr("Functions"));
    }
    {
        QWidget *page = new QWidget;
        QVBoxLayout *l = new QVBoxLayout(page);
        l->setContentsMargins(0, 4, 0, 0);
        stringFilter_ = new QLineEdit;
        stringFilter_->setPlaceholderText(tr("Search strings"));
        stringFilter_->setClearButtonEnabled(true);
        stringTable_ = makeTable({ tr("Address"), tr("Type"), tr("String") });
        l->addWidget(stringFilter_);
        l->addWidget(stringTable_, 1);
        listTabs_->addTab(page, tr("Strings"));
    }
    {
        QWidget *page = new QWidget;
        QVBoxLayout *l = new QVBoxLayout(page);
        l->setContentsMargins(0, 4, 0, 0);
        importFilter_ = new QLineEdit;
        importFilter_->setPlaceholderText(tr("Search imports and exports"));
        importFilter_->setClearButtonEnabled(true);
        importTable_ = makeTable({ tr("Address"), tr("Name") });
        l->addWidget(importFilter_);
        l->addWidget(importTable_, 1);
        listTabs_->addTab(page, tr("Imports"));
    }
    info_ = new QPlainTextEdit;
    info_->setReadOnly(true);
    info_->setLayoutDirection(Qt::LeftToRight);
    info_->setFont(Theme::codeFont());
    info_->setLineWrapMode(QPlainTextEdit::NoWrap);
    listTabs_->addTab(info_, tr("Info"));
    listTabs_->tabBar()->setUsesScrollButtons(false);
    listTabs_->tabBar()->setExpanding(true);
    QWidget *first = makeCard(tr("Program"), listTabs_);
    fileLabel_ = first->findChild<QLabel *>("cardHeading");
    splitter->addWidget(first);

    // ---- second card: disassembly and hex
    viewTabs_ = new QTabWidget;
    viewTabs_->setDocumentMode(true);
    viewTabs_->setLayoutDirection(Qt::LeftToRight);
    {
        QWidget *page = new QWidget;
        QVBoxLayout *l = new QVBoxLayout(page);
        l->setContentsMargins(0, 4, 0, 0);
        titleLabel_ = new QLabel(tr("Open a program with the Open file button."));
        titleLabel_->setObjectName("cardHint");
        titleLabel_->setLayoutDirection(Qt::LeftToRight);
        titleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        disassembly_ = makeTable({ tr("Address"), tr("Bytes"), tr("Instruction"), tr("Comment") });
        disassembly_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        disassembly_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
        disassembly_->setColumnWidth(1, 150);
        disassembly_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
        disassembly_->setColumnWidth(2, 250);
        l->addWidget(titleLabel_);
        l->addWidget(disassembly_, 1);
        viewTabs_->addTab(page, tr("Disassembly"));
    }
    {
        QWidget *page = new QWidget;
        QVBoxLayout *l = new QVBoxLayout(page);
        l->setContentsMargins(0, 4, 0, 0);
        hexGoto_ = new QLineEdit;
        hexGoto_->setPlaceholderText(tr("Go to an address (hex) or a file offset (prefix: @)"));
        hexGoto_->setLayoutDirection(Qt::LeftToRight);
        hex_ = new HexView;
        l->addWidget(hexGoto_);
        l->addWidget(hex_, 1);
        viewTabs_->addTab(page, tr("Hex"));
    }
    splitter->addWidget(makeCard(tr("Disassembly"), viewTabs_));

    // ---- third card: decompiled code (a later stage)
    QLabel *hint = new QLabel(tr("The decompiled code of the selected function will appear here."));
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    hint->setObjectName("cardHint");
    splitter->addWidget(makeCard(tr("Decompiled code"), hint));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({ 280, 700, 280 });

    // ---- connections
    connect(functionFilter_, &QLineEdit::textChanged, this, [this]() { fillFunctions(); });
    connect(stringFilter_, &QLineEdit::textChanged, this, [this]() { fillStrings(); });
    connect(importFilter_, &QLineEdit::textChanged, this, [this]() { fillImports(); });
    connect(functionList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && !filling_)
            goTo(item->data(Qt::UserRole).toULongLong());
    });
    connect(functionList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        goTo(item->data(Qt::UserRole).toULongLong());
        viewTabs_->setCurrentIndex(0);
    });
    functionList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(functionList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = functionList_->itemAt(pos);
        if (!item)
            return;
        const quint64 va = item->data(Qt::UserRole).toULongLong();
        QMenu menu;
        menu.addAction(tr("Rename..."), this, [this, va]() { renameAt(va); });
        menu.addAction(tr("Cross-references..."), this, [this, va]() { showXrefs(va); });
        menu.addAction(tr("Copy address"), this, [va]() { QGuiApplication::clipboard()->setText("0x" + hex(va)); });
        menu.exec(functionList_->viewport()->mapToGlobal(pos));
    });
    connect(stringTable_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        const quint64 va = stringTable_->item(r, 0)->data(Qt::UserRole).toULongLong();
        const auto refs = result_ ? result_->xrefsTo(va) : std::vector<ra::Xref>();
        if (!refs.empty()) {
            goTo(refs.front().from);
            viewTabs_->setCurrentIndex(0);
        } else if (image_ && image_->vaToOffset(va) >= 0) {
            hex_->gotoOffset(quint64(image_->vaToOffset(va)));
            viewTabs_->setCurrentIndex(1);
        }
    });
    stringTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(stringTable_, &QTableWidget::customContextMenuRequested, this, &ReversePage::stringsMenu);
    connect(importTable_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        const quint64 va = importTable_->item(r, 0)->data(Qt::UserRole).toULongLong();
        if (result_ && result_->imports.count(va))
            showXrefs(va);
        else
            goTo(va);
    });
    connect(disassembly_, &QTableWidget::customContextMenuRequested, this, &ReversePage::disassemblyMenu);
    connect(disassembly_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { followBranch(); });
    connect(disassembly_, &QTableWidget::currentCellChanged, this, [this](int r, int, int, int) {
        if (filling_ || r < 0 || r >= int(lines_.size()) || !image_)
            return;
        const int64_t off = image_->vaToOffset(lines_[size_t(r)].va);
        if (off >= 0)
            hex_->gotoOffset(quint64(off));
    });
    auto shortcut = [this](const QKeySequence &key, std::function<void()> fn) {
        QShortcut *s = new QShortcut(key, disassembly_);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, [fn]() { fn(); });
    };
    shortcut(QKeySequence(Qt::Key_N), [this]() { if (currentVa()) renameAt(currentVa()); });
    shortcut(QKeySequence(Qt::Key_Semicolon), [this]() { if (currentVa()) commentAt(currentVa()); });
    shortcut(QKeySequence(Qt::Key_X), [this]() { if (currentVa()) showXrefs(currentVa()); });
    shortcut(QKeySequence(Qt::Key_Return), [this]() { followBranch(); });
    shortcut(QKeySequence(Qt::Key_Backspace), [this]() { goBack(); });
    shortcut(QKeySequence(Qt::ALT | Qt::Key_Left), [this]() { goBack(); });
    connect(hexGoto_, &QLineEdit::returnPressed, this, [this]() {
        if (!image_)
            return;
        QString t = hexGoto_->text().trimmed();
        const bool asOffset = t.startsWith('@');
        if (asOffset)
            t = t.mid(1);
        quint64 v = 0;
        if (!parseAddress(t, v))
            return;
        if (asOffset) {
            hex_->gotoOffset(v);
        } else {
            const int64_t off = image_->vaToOffset(v);
            hex_->gotoOffset(off >= 0 ? quint64(off) : v);
        }
    });
    connect(hex_, &HexView::patched, this, [this]() {
        refreshDisassembly();
        saveProject();
        emit stateChanged();
    });
}

ReversePage::~ReversePage() = default;

QString ReversePage::programPath() const { return path_; }
bool ReversePage::hasPatches() const { return image_ && !image_->patches().empty(); }

QString ReversePage::addressText(quint64 va) const
{
    int width = 8;
    if (image_)
        width = std::max(8, int(QString::number(image_->base() + image_->info().sizeOfImage, 16).size()));
    return QString::number(va, 16).rightJustified(width, '0');
}

// ---------------------------------------------------------------- opening

void ReversePage::openProgram(const QString &path)
{
    path_ = path;
    image_.reset();
    result_.reset();
    lines_.clear();
    targets_.clear();
    history_.clear();
    currentFunction_ = 0;
    disassembly_->setRowCount(0);
    functionList_->clear();
    stringTable_->setRowCount(0);
    importTable_->setRowCount(0);
    info_->clear();
    hex_->setImage(nullptr);
    fileLabel_->setText(QFileInfo(path).fileName());
    titleLabel_->setText(tr("Analyzing %1 ...").arg(QFileInfo(path).fileName()));
    emit message(tr("Analyzing %1 ...").arg(QFileInfo(path).fileName()));
    emit stateChanged();
    auto img = std::make_shared<ra::Image>();
    auto res = std::make_shared<ra::Result>();
    const std::string p = path.toStdString();
    QThread *t = QThread::create([this, img, res, p]() {
        std::string err;
        bool ok = img->load(p, err) && ra::analyze(*img, *res, err);
        const QString e = QString::fromStdString(err);
        QMetaObject::invokeMethod(this, [this, ok, e, img, res]() { analysisDone(ok, e, img, res); },
                                  Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void ReversePage::analysisDone(bool ok, const QString &error, std::shared_ptr<ra::Image> image,
                               std::shared_ptr<ra::Result> result)
{
    if (QFileInfo(path_).absoluteFilePath() != QFileInfo(QString::fromStdString(image->path())).absoluteFilePath()
            && !image->path().empty())
        return;     // another file was opened meanwhile
    if (!ok) {
        titleLabel_->setText(tr("Could not open the file: %1").arg(error));
        emit message(tr("Could not open the file: %1").arg(error));
        return;
    }
    image_ = image;
    result_ = result;
    loadProject();
    hex_->setImage(image_);
    fillLists();
    emit message(tr("%1: %2 functions, %3 strings").arg(QFileInfo(path_).fileName())
                     .arg(result_->functions.size()).arg(result_->strings.size()));
    // the first thing to look at: main if the program has it, else the entry point
    quint64 start = 0;
    for (const ra::Function &f : result_->functions)
        if (result_->functionName(f) == "main") { start = f.va; break; }
    if (!start)
        for (const ra::Function &f : result_->functions)
            if (f.source == ra::Function::Entry) { start = f.va; break; }
    if (!start && !result_->functions.empty())
        start = result_->functions.front().va;
    if (start)
        goTo(start, false);
    emit stateChanged();
}

void ReversePage::fillLists()
{
    fillFunctions();
    fillStrings();
    fillImports();
    fillInfo();
}

void ReversePage::fillFunctions()
{
    if (!result_)
        return;
    filling_ = true;
    const QString filter = functionFilter_->text().trimmed().toLower();
    functionList_->clear();
    int shown = 0;
    for (const ra::Function &f : result_->functions) {
        const QString name = QString::fromStdString(result_->functionName(f));
        if (!filter.isEmpty() && !name.toLower().contains(filter) && !hex(f.va).contains(filter))
            continue;
        QListWidgetItem *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, qulonglong(f.va));
        item->setToolTip(QString("0x%1, %2 bytes").arg(hex(f.va)).arg(f.size));
        if (f.thunk) {
            QFont font = item->font();
            font.setItalic(true);
            item->setFont(font);
        }
        functionList_->addItem(item);
        ++shown;
    }
    functionFilter_->setPlaceholderText(tr("Search among %1 functions").arg(result_->functions.size()));
    filling_ = false;
}

void ReversePage::fillStrings()
{
    if (!result_)
        return;
    const QString filter = stringFilter_->text().trimmed().toLower();
    stringTable_->setUpdatesEnabled(false);
    stringTable_->setRowCount(0);
    int row = 0;
    const int kMaxRows = 30000;
    for (const ra::String &s : result_->strings) {
        const QString text = QString::fromStdString(s.text);
        if (!filter.isEmpty() && !text.toLower().contains(filter))
            continue;
        if (row >= kMaxRows)
            break;
        stringTable_->setRowCount(row + 1);
        QTableWidgetItem *a = new QTableWidgetItem(hex(s.va));
        a->setData(Qt::UserRole, qulonglong(s.va));
        stringTable_->setItem(row, 0, a);
        stringTable_->setItem(row, 1, new QTableWidgetItem(s.wide ? "U" : "A"));
        stringTable_->setItem(row, 2, new QTableWidgetItem(text));
        ++row;
    }
    stringTable_->resizeColumnToContents(0);
    stringTable_->resizeColumnToContents(1);
    stringTable_->setUpdatesEnabled(true);
    stringFilter_->setPlaceholderText(tr("Search among %1 strings").arg(result_->strings.size()));
}

void ReversePage::fillImports()
{
    if (!result_)
        return;
    const QString filter = importFilter_->text().trimmed().toLower();
    importTable_->setRowCount(0);
    int row = 0;
    auto add = [&](quint64 va, const QString &name) {
        if (!filter.isEmpty() && !name.toLower().contains(filter))
            return;
        importTable_->setRowCount(row + 1);
        QTableWidgetItem *a = new QTableWidgetItem(hex(va));
        a->setData(Qt::UserRole, qulonglong(va));
        importTable_->setItem(row, 0, a);
        importTable_->setItem(row, 1, new QTableWidgetItem(name));
        ++row;
    };
    for (const auto &im : result_->imports)
        add(im.first, QString::fromStdString(im.second));
    for (const pe::Export &e : image_->info().exports)
        add(image_->base() + e.rva, tr("export: %1").arg(QString::fromStdString(e.name)));
    importTable_->resizeColumnToContents(0);
    importFilter_->setPlaceholderText(tr("Search among %1 imports and exports").arg(result_->imports.size() + image_->info().exports.size()));
}

void ReversePage::fillInfo()
{
    const pe::Info &p = image_->info();
    QString t;
    t += QString("File:        %1\n").arg(path_);
    t += QString("Size:        %1 bytes\n").arg(image_->fileSize());
    t += QString("Type:        %1 %2\n").arg(p.is64 ? "64-bit" : "32-bit", p.dll ? "DLL" : "program");
    t += QString("Machine:     0x%1\n").arg(hex(p.machine));
    t += QString("Subsystem:   %1\n").arg(p.subsystem == 2 ? "Windows GUI" : p.subsystem == 3 ? "Windows console"
                                                                                           : QString::number(p.subsystem));
    t += QString("Image base:  0x%1\n").arg(hex(p.imageBase));
    t += QString("Entry point: 0x%1 (RVA 0x%2)\n").arg(hex(p.imageBase + p.entryRva), hex(p.entryRva));
    t += QString("Symbols:     %1\n").arg(p.symbols.size());
    t += QString("Imports:     %1, exports: %2\n").arg(p.imports.size()).arg(p.exports.size());
    t += QString("Functions:   %1\n\n").arg(result_->functions.size());
    t += QString("Sections:\n");
    t += QString("  %1 %2 %3 %4 %5 %6\n").arg(QString("name"), -8).arg(QString("RVA"), 10).arg(QString("vsize"), 10)
             .arg(QString("raw at"), 10).arg(QString("raw size"), 10).arg(QString("flags"));
    for (const pe::Section &s : p.sections)
        t += QString("  %1 %2 %3 %4 %5 %6%7%8\n").arg(QString::fromStdString(s.name), -8).arg(hex(s.rva), 10)
                 .arg(hex(s.virtualSize), 10).arg(hex(s.rawOffset), 10).arg(hex(s.rawSize), 10)
                 .arg(s.readable() ? "R" : "-").arg(s.writable() ? "W" : "-").arg(s.executable() ? "X" : "-");
    info_->setPlainText(t);
}

// ---------------------------------------------------------------- showing code

quint64 ReversePage::currentVa() const
{
    const int r = disassembly_->currentRow();
    return (r >= 0 && r < int(lines_.size())) ? lines_[size_t(r)].va : 0;
}

void ReversePage::goTo(quint64 va, bool remember)
{
    if (!result_ || !image_)
        return;
    if (remember) {
        const quint64 here = currentVa();
        if (here && (history_.isEmpty() || history_.last() != here))
            history_.append(here);
        if (history_.size() > 200)
            history_.removeFirst();
    }
    const ra::Function *f = result_->functionAt(va);
    if (f) {
        if (f->va != currentFunction_)
            showFunction(*f);
        selectRow(va);
        viewTabs_->setCurrentIndex(0);
        // follow in the function list without triggering another goTo
        filling_ = true;
        for (int i = 0; i < functionList_->count(); ++i)
            if (functionList_->item(i)->data(Qt::UserRole).toULongLong() == f->va) {
                functionList_->setCurrentRow(i);
                break;
            }
        filling_ = false;
    } else if (image_->isCode(va)) {
        showRegion(va);
        selectRow(va);
        viewTabs_->setCurrentIndex(0);
    } else {
        const int64_t off = image_->vaToOffset(va);
        if (off >= 0) {
            hex_->gotoOffset(quint64(off));
            viewTabs_->setCurrentIndex(1);
            emit message(tr("0x%1 is data: shown in the hex view").arg(hex(va)));
        }
    }
}

bool ReversePage::goToAddress(const QString &text)
{
    quint64 va = 0;
    if (!parseAddress(text, va) || !result_)
        return false;
    goTo(va);
    return true;
}

void ReversePage::showFunction(const ra::Function &f)
{
    currentFunction_ = f.va;
    std::vector<ra::Line> lines = ra::disassemble(*image_, *result_, f.va, std::min<size_t>(f.size, 40000));
    while (!lines.empty() && lines.back().va >= f.va + f.size && lines.size() > 1)
        lines.pop_back();
    const size_t callers = result_->xrefsTo(f.va).size();
    showLines(lines, tr("%1   0x%2   %3 bytes   %4 references")
                         .arg(QString::fromStdString(result_->functionName(f)), hex(f.va)).arg(f.size).arg(callers));
}

void ReversePage::showRegion(quint64 va)
{
    currentFunction_ = 0;
    showLines(ra::disassemble(*image_, *result_, va, 400), tr("(no function here)   0x%1").arg(hex(va)));
}

void ReversePage::showLines(const std::vector<ra::Line> &lines, const QString &title)
{
    filling_ = true;
    lines_ = lines;
    targets_.assign(lines_.size(), 0);
    titleLabel_->setText(title);
    disassembly_->setUpdatesEnabled(false);
    disassembly_->setRowCount(int(lines_.size()));
    const QColor keyword = Theme::color("keywordscolor");
    const QColor patched = Theme::isDark() ? QColor("#f9c74f") : QColor("#b5590e");
    const QColor muted = Theme::color("fontcolor").darker(130);
    for (size_t i = 0; i < lines_.size(); ++i) {
        const ra::Line &l = lines_[i];
        uint8_t buf[16];
        const size_t n = image_->read(l.va, buf, sizeof buf);
        dis::Insn in;
        if (n)
            dis::decode(buf, n, l.va, image_->is64(), in);
        if (in.hasTarget)
            targets_[i] = in.target;
        QTableWidgetItem *a = new QTableWidgetItem(addressText(l.va));
        QTableWidgetItem *b = new QTableWidgetItem(QString::fromStdString(l.bytes));
        QTableWidgetItem *t = new QTableWidgetItem(QString::fromStdString(l.text));
        QTableWidgetItem *c = new QTableWidgetItem(QString::fromStdString(l.comment));
        b->setToolTip(QString::fromStdString(l.bytes));
        a->setForeground(muted);
        b->setForeground(l.patched ? patched : muted);
        if (l.patched)
            t->setForeground(patched);
        else if (in.call || in.jump || in.ret)
            t->setForeground(keyword);
        c->setForeground(muted);
        if (l.functionStart) {
            QFont f = t->font();
            f.setBold(true);
            t->setFont(f);
        }
        disassembly_->setItem(int(i), 0, a);
        disassembly_->setItem(int(i), 1, b);
        disassembly_->setItem(int(i), 2, t);
        disassembly_->setItem(int(i), 3, c);
    }
    disassembly_->setUpdatesEnabled(true);
    filling_ = false;
}

void ReversePage::selectRow(quint64 va)
{
    int lo = 0, hi = int(lines_.size()) - 1, found = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (lines_[size_t(mid)].va <= va) { found = mid; lo = mid + 1; } else { hi = mid - 1; }
    }
    if (found < 0)
        return;
    disassembly_->setCurrentCell(found, 2);
    disassembly_->selectRow(found);
    disassembly_->scrollToItem(disassembly_->item(found, 0), QAbstractItemView::PositionAtCenter);
}

void ReversePage::refreshDisassembly()
{
    if (!result_ || lines_.empty())
        return;
    const quint64 keep = currentVa();
    if (const ra::Function *f = result_->functionStartingAt(currentFunction_))
        showFunction(*f);
    else
        showRegion(lines_.front().va);
    if (keep) {
        filling_ = true;          // re-selecting the row must not move the hex cursor
        selectRow(keep);
        filling_ = false;
    }
}

void ReversePage::followBranch()
{
    const int r = disassembly_->currentRow();
    if (r < 0 || r >= int(targets_.size()))
        return;
    if (targets_[size_t(r)]) {
        goTo(targets_[size_t(r)]);
        return;
    }
    // through an import: "call [rip+x]" -> where the import is used
    emit message(tr("This instruction has no direct target. Press X for references."));
}

void ReversePage::goBack()
{
    if (history_.isEmpty())
        return;
    const quint64 va = history_.takeLast();
    goTo(va, false);
}

// ---------------------------------------------------------------- references, names, patches

void ReversePage::showXrefs(quint64 va)
{
    if (!result_)
        return;
    const std::vector<ra::Xref> refs = result_->xrefsTo(va);
    if (refs.empty()) {
        emit message(tr("No references to 0x%1 were found.").arg(hex(va)));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("References to %1").arg(QString::fromStdString(result_->nameAt(va)).isEmpty()
                                                      ? "0x" + hex(va) : QString::fromStdString(result_->nameAt(va))));
    dlg.resize(560, 360);
    QVBoxLayout *l = new QVBoxLayout(&dlg);
    QListWidget *list = new QListWidget;
    list->setLayoutDirection(Qt::LeftToRight);
    list->setFont(Theme::codeFont());
    for (const ra::Xref &x : refs) {
        const char *kind = x.kind == ra::Xref::Call ? "call" : x.kind == ra::Xref::Jump ? "jump" : "data";
        const QString where = QString::fromStdString(result_->nameAt(x.from));
        QListWidgetItem *item = new QListWidgetItem(QString("%1  %2  %3").arg(addressText(x.from), -18).arg(kind, -5).arg(where));
        item->setData(Qt::UserRole, qulonglong(x.from));
        list->addItem(item);
    }
    l->addWidget(list);
    QLabel *hintLabel = new QLabel(tr("Double-click goes to the place."));
    hintLabel->setObjectName("cardHint");
    l->addWidget(hintLabel);
    quint64 chosen = 0;
    connect(list, &QListWidget::itemDoubleClicked, &dlg, [&](QListWidgetItem *item) {
        chosen = item->data(Qt::UserRole).toULongLong();
        dlg.accept();
    });
    dlg.exec();
    if (chosen)
        goTo(chosen);
}

void ReversePage::renameAt(quint64 va)
{
    if (!result_)
        return;
    QString current = QString::fromStdString(result_->nameAt(va));
    if (current.contains('+'))
        current.clear();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("Name for 0x%1 (empty: the original name):").arg(hex(va)),
                                               QLineEdit::Normal, current, &ok).trimmed();
    if (!ok)
        return;
    if (name.isEmpty())
        result_->userNames.erase(va);
    else
        result_->userNames[va] = name.toStdString();
    saveProject();
    fillFunctions();
    refreshDisassembly();
}

void ReversePage::commentAt(quint64 va)
{
    if (!result_)
        return;
    const auto old = result_->comments.find(va);
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Comment"), tr("Comment for 0x%1 (empty: remove it):").arg(hex(va)),
                                               QLineEdit::Normal,
                                               old == result_->comments.end() ? QString() : QString::fromStdString(old->second),
                                               &ok).trimmed();
    if (!ok)
        return;
    if (text.isEmpty())
        result_->comments.erase(va);
    else
        result_->comments[va] = text.toStdString();
    saveProject();
    refreshDisassembly();
}

void ReversePage::patchAt(quint64 va)
{
    if (!image_)
        return;
    const int r = disassembly_->currentRow();
    const QString old = (r >= 0 && r < int(lines_.size()) && lines_[size_t(r)].va == va)
                            ? QString::fromStdString(lines_[size_t(r)].bytes) : QString();
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Patch bytes"),
                                               tr("New bytes at 0x%1 (hex, for example 90 90). The program file itself "
                                                  "is not changed: use Save a patched copy.").arg(hex(va)),
                                               QLineEdit::Normal, old, &ok);
    if (!ok)
        return;
    QString digits = text;
    digits.remove(QRegularExpression("[\\s,]|0x"));
    if (digits.isEmpty() || digits.size() % 2 || !QRegularExpression("^[0-9a-fA-F]+$").match(digits).hasMatch()) {
        emit message(tr("The bytes must be hex digits, two for each byte."));
        return;
    }
    std::vector<uint8_t> bytes;
    for (int i = 0; i < digits.size(); i += 2)
        bytes.push_back(uint8_t(digits.mid(i, 2).toUInt(nullptr, 16)));
    if (!image_->patchVa(va, bytes)) {
        emit message(tr("These bytes are not in the file (code that exists only in memory cannot be patched)."));
        return;
    }
    hex_->refresh();
    refreshDisassembly();
    saveProject();
    emit stateChanged();
}

void ReversePage::nopAt(quint64 va)
{
    const int r = disassembly_->currentRow();
    if (!image_ || r < 0 || r >= int(lines_.size()) || lines_[size_t(r)].va != va)
        return;
    if (!image_->patchVa(va, std::vector<uint8_t>(size_t(lines_[size_t(r)].length), 0x90))) {
        emit message(tr("These bytes are not in the file."));
        return;
    }
    hex_->refresh();
    refreshDisassembly();
    saveProject();
    emit stateChanged();
}

void ReversePage::revertAt(quint64 va)
{
    const int r = disassembly_->currentRow();
    if (!image_ || r < 0 || r >= int(lines_.size()) || lines_[size_t(r)].va != va)
        return;
    const int64_t off = image_->vaToOffset(va);
    if (off < 0)
        return;
    for (int i = 0; i < lines_[size_t(r)].length; ++i)
        image_->revertPatch(quint64(off) + quint64(i));
    hex_->refresh();
    refreshDisassembly();
    saveProject();
    emit stateChanged();
}

void ReversePage::disassemblyMenu(const QPoint &pos)
{
    const int r = disassembly_->rowAt(pos.y());
    if (r < 0 || r >= int(lines_.size()))
        return;
    disassembly_->selectRow(r);
    const quint64 va = lines_[size_t(r)].va;
    QMenu menu;
    if (targets_[size_t(r)])
        menu.addAction(tr("Go to the target\tEnter"), this, [this]() { followBranch(); });
    menu.addAction(tr("Rename...\tN"), this, [this, va]() { renameAt(va); });
    menu.addAction(tr("Comment...\t;"), this, [this, va]() { commentAt(va); });
    menu.addAction(tr("Cross-references...\tX"), this, [this, va]() { showXrefs(va); });
    menu.addSeparator();
    menu.addAction(tr("Patch bytes..."), this, [this, va]() { patchAt(va); });
    menu.addAction(tr("Fill the instruction with NOP"), this, [this, va]() { nopAt(va); });
    menu.addAction(tr("Undo the patch of this instruction"), this, [this, va]() { revertAt(va); });
    menu.addSeparator();
    menu.addAction(tr("Show in the hex view"), this, [this, va]() {
        const int64_t off = image_->vaToOffset(va);
        if (off >= 0) {
            hex_->gotoOffset(quint64(off));
            viewTabs_->setCurrentIndex(1);
        }
    });
    menu.addAction(tr("Copy address"), this, [va]() { QGuiApplication::clipboard()->setText("0x" + hex(va)); });
    menu.exec(disassembly_->viewport()->mapToGlobal(pos));
}

void ReversePage::stringsMenu(const QPoint &pos)
{
    const int r = stringTable_->rowAt(pos.y());
    if (r < 0)
        return;
    const quint64 va = stringTable_->item(r, 0)->data(Qt::UserRole).toULongLong();
    QMenu menu;
    menu.addAction(tr("Cross-references..."), this, [this, va]() { showXrefs(va); });
    menu.addAction(tr("Show in the hex view"), this, [this, va]() {
        const int64_t off = image_ ? image_->vaToOffset(va) : -1;
        if (off >= 0) {
            hex_->gotoOffset(quint64(off));
            viewTabs_->setCurrentIndex(1);
        }
    });
    menu.addAction(tr("Copy the string"), this, [this, r]() { QGuiApplication::clipboard()->setText(stringTable_->item(r, 2)->text()); });
    menu.exec(stringTable_->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------- project file and saving

QString ReversePage::projectPath() const { return path_ + ".asmon-re.json"; }

void ReversePage::saveProject()
{
    if (!image_ || !result_)
        return;
    QJsonObject names, comments, patches;
    for (const auto &n : result_->userNames)
        names[hex(n.first)] = QString::fromStdString(n.second);
    for (const auto &c : result_->comments)
        comments[hex(c.first)] = QString::fromStdString(c.second);
    for (const auto &p : image_->patches())
        patches[hex(p.first)] = int(p.second);
    QJsonObject root;
    root["version"] = 1;
    root["size"] = qint64(image_->fileSize());
    root["names"] = names;
    root["comments"] = comments;
    root["patches"] = patches;
    const bool empty = names.isEmpty() && comments.isEmpty() && patches.isEmpty();
    QFile f(projectPath());
    if (empty) {
        f.remove();
        return;
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit message(tr("Could not save the names and comments next to the program (%1).").arg(projectPath()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ReversePage::loadProject()
{
    QFile f(projectPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root["size"].toInteger() != qint64(image_->fileSize())) {
        emit message(tr("The saved names belong to a different version of the file and were not loaded."));
        return;
    }
    const QJsonObject names = root["names"].toObject(), comments = root["comments"].toObject(),
                      patches = root["patches"].toObject();
    for (auto it = names.begin(); it != names.end(); ++it)
        result_->userNames[it.key().toULongLong(nullptr, 16)] = it.value().toString().toStdString();
    for (auto it = comments.begin(); it != comments.end(); ++it)
        result_->comments[it.key().toULongLong(nullptr, 16)] = it.value().toString().toStdString();
    for (auto it = patches.begin(); it != patches.end(); ++it)
        image_->patch(it.key().toULongLong(nullptr, 16), { uint8_t(it.value().toInt()) });
}

bool ReversePage::saveCopy()
{
    if (!image_)
        return false;
    const QFileInfo fi(path_);
    const QString suggested = fi.absolutePath() + "/" + fi.completeBaseName() + ".patched." + fi.suffix();
    const QString target = QFileDialog::getSaveFileName(this, tr("Save a patched copy"), suggested,
                                                        tr("Programs (*.exe *.dll *.sys);;All files (*)"));
    if (target.isEmpty())
        return false;
    std::string err;
    if (!image_->saveAs(target.toStdString(), err)) {
        QMessageBox::warning(this, tr("Save a patched copy"), QString::fromStdString(err));
        return false;
    }
    emit message(tr("Saved %1 (%2 bytes changed). The original file was not touched.")
                     .arg(target).arg(image_->patches().size()));
    return true;
}
