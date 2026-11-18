#include "mainwindow.h"
#include "codeeditor.h"
#include "tab.h"
#include "theme.h"
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QTextBlock>
#include <QTextCursor>

/**
 * @file editcommands.cpp
 * Notepad++ style editing commands (line operations, case, blanks, sort) and view switches
 * (whitespace, word wrap, zoom, text direction RTL / LTR).
 */

static CodeEditor *editorOf(QTabWidget *tabs)
{
    if (!tabs || tabs->count() == 0)
        return nullptr;
    Tab *t = static_cast<Tab *>(tabs->currentWidget());
    return t ? t->code : nullptr;
}

//! The lines touched by the selection (or the cursor line): [first, last] block numbers.
static void selectedLines(const QTextCursor &c, int &first, int &last)
{
    QTextCursor a(c);
    first = c.document()->findBlock(c.selectionStart()).blockNumber();
    int end = c.selectionEnd();
    last = c.document()->findBlock(end).blockNumber();
    //! a selection that ends at the very start of a line does not include that line
    if (c.hasSelection() && last > first && c.document()->findBlock(end).position() == end)
        --last;
}

static void transformLines(CodeEditor *ed, const std::function<QStringList(const QStringList &)> &fn)
{
    QTextCursor c = ed->textCursor();
    int first, last;
    selectedLines(c, first, last);
    QTextDocument *doc = ed->document();
    QStringList lines;
    for (int i = first; i <= last; ++i)
        lines << doc->findBlockByNumber(i).text();
    const QStringList result = fn(lines);
    if (result == lines)
        return;
    QTextCursor w(doc->findBlockByNumber(first));
    w.beginEditBlock();
    w.setPosition(doc->findBlockByNumber(last).position() + doc->findBlockByNumber(last).length() - 1, QTextCursor::KeepAnchor);
    w.insertText(result.join('\n'));
    w.endEditBlock();
}

void MainWindow::createEditCommands()
{
    auto add = [this](QMenu *menu, const QString &text, const QString &keys, std::function<void(CodeEditor *)> fn) {
        QAction *a = menu->addAction(text);
        if (!keys.isEmpty()) {
            a->setShortcut(QKeySequence(keys));
            a->setShortcutContext(Qt::WindowShortcut);
        }
        connect(a, &QAction::triggered, this, [this, fn]() {
            if (CodeEditor *ed = editorOf(tabs))
                fn(ed);
        });
        return a;
    };

    // ---- Edit menu
    editMenu->addSeparator();
    QMenu *lineMenu = editMenu->addMenu(tr("Line operations"));
    add(lineMenu, tr("Duplicate line"), "Ctrl+D", [](CodeEditor *ed) {
        QTextCursor c = ed->textCursor();
        int first, last;
        selectedLines(c, first, last);
        QTextDocument *doc = ed->document();
        QStringList lines;
        for (int i = first; i <= last; ++i)
            lines << doc->findBlockByNumber(i).text();
        QTextCursor w(doc->findBlockByNumber(last));
        w.beginEditBlock();
        w.movePosition(QTextCursor::EndOfBlock);
        w.insertText("\n" + lines.join('\n'));
        w.endEditBlock();
    });
    add(lineMenu, tr("Delete line"), "Ctrl+Shift+K", [](CodeEditor *ed) {
        QTextCursor c = ed->textCursor();
        int first, last;
        selectedLines(c, first, last);
        QTextDocument *doc = ed->document();
        QTextCursor w(doc->findBlockByNumber(first));
        w.beginEditBlock();
        w.setPosition(doc->findBlockByNumber(last).position() + doc->findBlockByNumber(last).length() - 1,
                      QTextCursor::KeepAnchor);
        w.removeSelectedText();
        if (!w.atEnd())
            w.deleteChar();           // the line break
        else if (w.blockNumber() > 0)
            w.deletePreviousChar();   // last line: remove the break before it
        w.endEditBlock();
    });
    add(lineMenu, tr("Move line up"), "Alt+Up", [](CodeEditor *ed) {
        QTextCursor c = ed->textCursor();
        int first, last;
        selectedLines(c, first, last);
        if (first == 0)
            return;
        QTextDocument *doc = ed->document();
        const QString above = doc->findBlockByNumber(first - 1).text();
        QStringList lines;
        for (int i = first; i <= last; ++i)
            lines << doc->findBlockByNumber(i).text();
        QTextCursor w(doc->findBlockByNumber(first - 1));
        w.beginEditBlock();
        w.setPosition(doc->findBlockByNumber(last).position() + doc->findBlockByNumber(last).length() - 1, QTextCursor::KeepAnchor);
        w.insertText(lines.join('\n') + "\n" + above);
        w.endEditBlock();
        QTextCursor sel(doc->findBlockByNumber(first - 1));
        sel.setPosition(doc->findBlockByNumber(last - 1).position() + doc->findBlockByNumber(last - 1).length() - 1, QTextCursor::KeepAnchor);
        ed->setTextCursor(sel.hasSelection() && c.hasSelection() ? sel : QTextCursor(doc->findBlockByNumber(first - 1)));
    });
    add(lineMenu, tr("Move line down"), "Alt+Down", [](CodeEditor *ed) {
        QTextCursor c = ed->textCursor();
        int first, last;
        selectedLines(c, first, last);
        QTextDocument *doc = ed->document();
        if (last >= doc->blockCount() - 1)
            return;
        const QString below = doc->findBlockByNumber(last + 1).text();
        QStringList lines;
        for (int i = first; i <= last; ++i)
            lines << doc->findBlockByNumber(i).text();
        QTextCursor w(doc->findBlockByNumber(first));
        w.beginEditBlock();
        w.setPosition(doc->findBlockByNumber(last + 1).position() + doc->findBlockByNumber(last + 1).length() - 1, QTextCursor::KeepAnchor);
        w.insertText(below + "\n" + lines.join('\n'));
        w.endEditBlock();
        QTextCursor sel(doc->findBlockByNumber(first + 1));
        sel.setPosition(doc->findBlockByNumber(last + 1).position() + doc->findBlockByNumber(last + 1).length() - 1, QTextCursor::KeepAnchor);
        ed->setTextCursor(c.hasSelection() ? sel : QTextCursor(doc->findBlockByNumber(first + 1)));
    });
    add(lineMenu, tr("Join lines"), "Ctrl+Alt+J", [](CodeEditor *ed) {
        transformLines(ed, [](const QStringList &l) {
            if (l.size() < 2)
                return l;
            QString s = l.first();
            for (int i = 1; i < l.size(); ++i)
                s += " " + l.at(i).trimmed();
            return QStringList{ s };
        });
    });
    add(lineMenu, tr("Sort lines ascending"), "", [](CodeEditor *ed) {
        transformLines(ed, [](QStringList l) { l.sort(Qt::CaseSensitive); return l; });
    });
    add(lineMenu, tr("Sort lines descending"), "", [](CodeEditor *ed) {
        transformLines(ed, [](QStringList l) { l.sort(Qt::CaseSensitive); std::reverse(l.begin(), l.end()); return l; });
    });
    add(lineMenu, tr("Remove duplicate lines"), "", [](CodeEditor *ed) {
        transformLines(ed, [](const QStringList &l) {
            QStringList r;
            for (const QString &s : l)
                if (!r.contains(s))
                    r << s;
            return r;
        });
    });

    QMenu *caseMenu = editMenu->addMenu(tr("Convert case"));
    add(caseMenu, tr("UPPERCASE"), "Ctrl+Shift+U", [](CodeEditor *ed) {
        QTextCursor c = ed->textCursor();
        if (!c.hasSelection())
            c.select(QTextCursor::WordUnderCursor);
        c.insertText(c.selectedText().toUpper());
    });
    add(caseMenu, tr("lowercase"), "Ctrl+U", [](CodeEditor *ed) {
        QTextCursor c = ed->textCursor();
        if (!c.hasSelection())
            c.select(QTextCursor::WordUnderCursor);
        c.insertText(c.selectedText().toLower());
    });

    QMenu *blankMenu = editMenu->addMenu(tr("Blank operations"));
    auto allOrSelection = [](CodeEditor *ed, const std::function<QStringList(const QStringList &)> &fn) {
        if (ed->textCursor().hasSelection()) {
            transformLines(ed, fn);
        } else {
            QTextCursor keep = ed->textCursor();
            ed->selectAll();
            transformLines(ed, fn);
            ed->setTextCursor(keep);
        }
    };
    add(blankMenu, tr("Trim trailing spaces"), "", [allOrSelection](CodeEditor *ed) {
        allOrSelection(ed, [](const QStringList &l) {
            QStringList r;
            for (QString s : l) {
                while (!s.isEmpty() && (s.endsWith(' ') || s.endsWith('\t')))
                    s.chop(1);
                r << s;
            }
            return r;
        });
    });
    add(blankMenu, tr("Tabs to spaces"), "", [allOrSelection](CodeEditor *ed) {
        allOrSelection(ed, [](const QStringList &l) {
            QStringList r;
            for (const QString &s : l)
                r << QString(s).replace('\t', "    ");
            return r;
        });
    });
    add(blankMenu, tr("Leading spaces to tabs"), "", [allOrSelection](CodeEditor *ed) {
        allOrSelection(ed, [](const QStringList &l) {
            QStringList r;
            for (const QString &s : l) {
                int n = 0;
                while (n < s.size() && s.at(n) == ' ')
                    ++n;
                r << QString(n / 4, '\t') + QString(n % 4, ' ') + s.mid(n);
            }
            return r;
        });
    });

    // ---- View menu
    viewMenu->addSeparator();
    QAction *wsAction = viewMenu->addAction(tr("Show spaces and tabs"));
    wsAction->setCheckable(true);
    connect(wsAction, &QAction::toggled, this, [this](bool on) {
        settings.setValue("showwhitespace", on);
        for (int i = 0; i < tabs->count(); ++i)
            static_cast<Tab *>(tabs->widget(i))->code->setShowWhitespace(on);
    });
    QAction *wrapAction = viewMenu->addAction(tr("Word wrap"));
    wrapAction->setCheckable(true);
    connect(wrapAction, &QAction::toggled, this, [this](bool on) {
        settings.setValue("wordwrap", on);
        for (int i = 0; i < tabs->count(); ++i)
            static_cast<Tab *>(tabs->widget(i))->code->setWordWrap(on);
    });
    //! new tabs get the saved switches
    connect(tabs, &QTabWidget::currentChanged, this, [this, wsAction, wrapAction](int) {
        if (CodeEditor *ed = editorOf(tabs)) {
            ed->setShowWhitespace(wsAction->isChecked());
            ed->setWordWrap(wrapAction->isChecked());
        }
    });
    wsAction->setChecked(settings.value("showwhitespace", false).toBool());
    wrapAction->setChecked(settings.value("wordwrap", false).toBool());

    QMenu *zoomMenu = viewMenu->addMenu(tr("Zoom"));
    add(zoomMenu, tr("Zoom in"), "Ctrl+=", [](CodeEditor *ed) { ed->zoomIn(1); });
    add(zoomMenu, tr("Zoom out"), "Ctrl+-", [](CodeEditor *ed) { ed->zoomOut(1); });
    add(zoomMenu, tr("Restore default zoom"), "Ctrl+0", [](CodeEditor *ed) {
        ed->setFont(Theme::codeFont());
    });

    QMenu *dirMenu = viewMenu->addMenu(tr("Text direction"));
    QActionGroup *dirGroup = new QActionGroup(this);
    QAction *ltr = add(dirMenu, tr("Left to right"), "Ctrl+Alt+L", [](CodeEditor *ed) { ed->setTextDirection(Qt::LeftToRight); });
    QAction *rtl = add(dirMenu, tr("Right to left (Hebrew text files)"), "Ctrl+Alt+R", [](CodeEditor *ed) { ed->setTextDirection(Qt::RightToLeft); });
    for (QAction *a : { ltr, rtl }) {
        a->setCheckable(true);
        dirGroup->addAction(a);
    }
    ltr->setChecked(true);
    connect(dirMenu, &QMenu::aboutToShow, this, [this, ltr, rtl]() {
        if (CodeEditor *ed = editorOf(tabs))
            (ed->textDirection() == Qt::RightToLeft ? rtl : ltr)->setChecked(true);
    });
}
