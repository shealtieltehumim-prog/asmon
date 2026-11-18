/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "searchpanel.h"
#include "theme.h"

#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

static const int kMaxFiles = 3000;
static const int kMaxResults = 5000;

SearchPanel::SearchPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 0);
    layout->setSpacing(4);

    auto *bar = new QHBoxLayout;
    query = new QLineEdit;
    query->setPlaceholderText(tr("Search in all the files of the folder (Enter)"));
    query->setClearButtonEnabled(true);
    caseBox = new QCheckBox(tr("Match case"));
    wordBox = new QCheckBox(tr("Whole word"));
    regexBox = new QCheckBox(tr("Regex"));
    summary = new QLabel;
    summary->setForegroundRole(QPalette::PlaceholderText);
    bar->addWidget(query, 1);
    bar->addWidget(caseBox);
    bar->addWidget(wordBox);
    bar->addWidget(regexBox);
    layout->addLayout(bar);
    layout->addWidget(summary);

    tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    tree->setFont(Theme::codeFont(10));
    tree->setLayoutDirection(Qt::LeftToRight); // results are code: left to right
    layout->addWidget(tree, 1);

    connect(query, &QLineEdit::returnPressed, this, &SearchPanel::runSearch);
    for (QCheckBox *b : { caseBox, wordBox, regexBox })
        connect(b, &QCheckBox::toggled, this, [this]() {
            if (!query->text().isEmpty())
                runSearch();
        });
    connect(tree, &QTreeWidget::itemActivated, this, &SearchPanel::activate);
    connect(tree, &QTreeWidget::itemDoubleClicked, this, &SearchPanel::activate);
}

void SearchPanel::setRoot(const QString &folder)
{
    root = folder;
}

void SearchPanel::focusSearch(const QString &text)
{
    if (!text.isEmpty())
        query->setText(text);
    query->setFocus();
    query->selectAll();
}

QTreeWidgetItem *SearchPanel::fileItem(const QString &file)
{
    if (fileItems.contains(file))
        return fileItems.value(file);
    auto *item = new QTreeWidgetItem(tree);
    const QFileInfo fi(file);
    QString where = root.isEmpty() ? fi.absolutePath() : QDir(root).relativeFilePath(fi.absolutePath());
    if (where == ".")
        where.clear();
    item->setText(0, file.isEmpty() ? tr("(unsaved file)")
                                    : fi.fileName() + (where.isEmpty() ? QString() : "   " + QDir::toNativeSeparators(where)));
    item->setData(0, Qt::UserRole, file);
    item->setData(0, Qt::UserRole + 1, -1);
    QFont f = Theme::uiFont();
    f.setBold(true);
    item->setFont(0, f);
    item->setExpanded(true);
    fileItems.insert(file, item);
    return item;
}

void SearchPanel::addResult(const QString &file, int line, int column, const QString &text, int length)
{
    Q_UNUSED(length);
    auto *item = new QTreeWidgetItem(fileItem(file));
    QString shown = text;
    shown.replace('\t', "    ");
    item->setText(0, QString("%1:  %2").arg(line, 4).arg(shown.trimmed()));
    item->setData(0, Qt::UserRole, file);
    item->setData(0, Qt::UserRole + 1, line);
    item->setData(0, Qt::UserRole + 2, column);
    item->setToolTip(0, QDir::toNativeSeparators(file) + QString(":%1").arg(line));
}

void SearchPanel::runSearch()
{
    tree->clear();
    fileItems.clear();
    const QString text = query->text();
    if (text.isEmpty()) {
        summary->clear();
        return;
    }
    QString pattern = regexBox->isChecked() ? text : QRegularExpression::escape(text);
    if (wordBox->isChecked())
        pattern = "(?<![\\w.?$@])" + pattern + "(?![\\w?$@])";
    QRegularExpression re(pattern, caseBox->isChecked() ? QRegularExpression::NoPatternOption
                                                        : QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid()) {
        summary->setText(tr("Invalid regular expression: %1").arg(re.errorString()));
        return;
    }
    if (root.isEmpty() || !QDir(root).exists()) {
        summary->setText(tr("Save the file first: the search looks in its folder."));
        return;
    }
    int files = 0, hits = 0, filesWithHits = 0;
    QDirIterator it(root, QStringList() << "*.asm" << "*.inc" << "*.s" << "*.S" << "*.mac" << "*.nasm"
                                        << "*.project" << "*.txt" << "*.md",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && files < kMaxFiles && hits < kMaxResults) {
        const QString path = QFileInfo(it.next()).absoluteFilePath();
        if (path.contains("/.git/"))
            continue;
        ++files;
        QString content;
        if (openBuffers.contains(path)) {
            content = openBuffers.value(path);
        } else {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024)
                continue;
            content = QString::fromUtf8(f.readAll());
        }
        const QStringList lines = content.split('\n');
        bool any = false;
        for (int i = 0; i < lines.size() && hits < kMaxResults; ++i) {
            QString l = lines.at(i);
            if (l.endsWith('\r'))
                l.chop(1);
            const QRegularExpressionMatch m = re.match(l);
            if (m.hasMatch()) {
                addResult(path, i + 1, int(m.capturedStart()), l, int(m.capturedLength()));
                ++hits;
                any = true;
            }
        }
        if (any)
            ++filesWithHits;
    }
    summary->setText(hits == 0 ? tr("No results in %1 files.").arg(files)
                               : tr("%1 results in %2 files (searched %3 files).")
                                     .arg(hits).arg(filesWithHits).arg(files));
}

void SearchPanel::showReferences(const QString &name, const QList<Symbols::Reference> &refs)
{
    tree->clear();
    fileItems.clear();
    for (const Symbols::Reference &r : refs)
        addResult(r.file, r.line, r.column, r.text, name.size());
    summary->setText(refs.isEmpty() ? tr("No uses of \"%1\" were found.").arg(name)
                                    : tr("%1 uses of \"%2\".").arg(refs.size()).arg(name));
    if (!refs.isEmpty() && tree->topLevelItemCount() > 0 && tree->topLevelItem(0)->childCount() > 0)
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
    tree->setFocus();
}

void SearchPanel::activate(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const int line = item->data(0, Qt::UserRole + 1).toInt();
    if (line <= 0)
        return; // a file header row
    emit openLocation(item->data(0, Qt::UserRole).toString(), line, item->data(0, Qt::UserRole + 2).toInt());
}
