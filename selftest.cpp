/****************************************************************************
** Asmon — fork of SASM (GPL-3.0).
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "selftest.h"

#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QScrollBar>
#include <QSettings>
#include <QTextBlock>
#include <QTextStream>
#include <QThread>
#include <QWidget>
#include <cstdio>

#include "codeeditor.h"
#include "folding.h"
#include "minimap.h"
#include "theme.h"

/**
 * @file selftest.cpp
 * "asmon --selftest": drives a real code editor (no main window) through the editor features and
 * prints one line per check; the exit code is the number of failed checks. CI runs it on Windows at
 * several display scales; ASMON_TEST_LOG gets the same lines.
 */

namespace {

QStringList g_warnings;
QtMessageHandler g_previous = nullptr;

void collect(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (type == QtWarningMsg || type == QtCriticalMsg)
        g_warnings << msg;
    if (g_previous)
        g_previous(type, ctx, msg);
}

struct Runner {
    int failed = 0, passed = 0;
    QStringList log;
    QString prefix;
    void check(bool ok, const QString &name, const QString &detail = QString())
    {
        const QString line = QString("%1 %2%3").arg(ok ? "PASS" : "FAIL", prefix + name,
                                                   detail.isEmpty() ? QString() : "  (" + detail + ")");
        log << line;
        std::printf("%s\n", line.toLocal8Bit().constData());
        std::fflush(stdout);
        (ok ? passed : failed)++;
    }
};

void pump()
{
    for (int i = 0; i < 6; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

void key(CodeEditor *ed, int k, const QString &text = QString(), Qt::KeyboardModifiers m = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, k, m, text);
    QApplication::sendEvent(ed, &press);
    QKeyEvent release(QEvent::KeyRelease, k, m, text);
    QApplication::sendEvent(ed, &release);
    pump();
}

void type(CodeEditor *ed, const QString &s)
{
    for (const QChar c : s)
        key(ed, c.unicode() < 128 ? int(QKeySequence(QString(c))[0].key()) : 0, QString(c));
}

void setText(CodeEditor *ed, const QString &text, int line = 0, int col = 0)
{
    ed->unfoldAll();
    ed->setPlainText(text);
    pump();
    QTextCursor c(ed->document()->findBlockByNumber(line));
    c.setPosition(c.position() + col);
    ed->setTextCursor(c);
    pump();
}

QString lineText(CodeEditor *ed, int n) { return ed->document()->findBlockByNumber(n).text(); }

const char *kSample =
    "; demo for folding\n"                 // 0  comment block 0-1
    "; second comment line\n"              // 1
    "%macro PRINT 1\n"                     // 2  macro 2-4
    "    mov rax, %1\n"                    // 3
    "%endmacro\n"                          // 4
    "section .data\n"                      // 5  section 5-7
    "    msg db \"hi\", 10\n"              // 6
    "    len equ $ - msg\n"                // 7
    "\n"                                   // 8
    "section .text\n"                      // 9  section 9-22
    "global _start\n"                      // 10
    "_start:\n"                            // 11 label 11-16
    "    mov rax, 1\n"                     // 12
    "    %ifdef DEBUG\n"                   // 13 if 13-15
    "    nop\n"                            // 14
    "    %endif\n"                         // 15
    "    call helper\n"                    // 16
    "\n"                                   // 17
    "; helper prints\n"                    // 18
    "helper:\n"                            // 19 label 19-22
    "    mov rdi, 1\n"                     // 20
    "    syscall\n"                        // 21
    "    ret\n";                           // 22

} // namespace

int runSelfTest(int argc, char **argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    Runner t;

    //! the application style sheet must have every placeholder filled and parse cleanly
    t.check(!g_warnings.join('\n').contains("QString::arg"), "stylesheet: all placeholders filled", g_warnings.join(" | ").left(200));

    //! everything twice: in a left-to-right interface and in the Hebrew right-to-left one
    for (const Qt::LayoutDirection dir : { Qt::LeftToRight, Qt::RightToLeft }) {
    QApplication::setLayoutDirection(dir);
    t.prefix = dir == Qt::RightToLeft ? "[rtl] " : "";
    QWidget host;
    host.resize(1100, 700);
    CodeEditor *ed = new CodeEditor(&host);
    ed->setFont(Theme::codeFont());
    ed->setGeometry(0, 0, 1100, 700);
    host.show();
    pump();

    // ------------------------------------------------------------ folding regions
    {
        const QStringList lines = QString(kSample).split('\n');
        const auto ranges = Folding::compute(lines);
        auto has = [&](int s, int e) {
            for (const auto &r : ranges)
                if (r.start == s && r.end == e)
                    return true;
            return false;
        };
        QStringList got;
        for (const auto &r : ranges)
            got << QString("%1-%2").arg(r.start).arg(r.end);
        t.check(has(0, 1), "fold: comment block", got.join(','));
        t.check(has(2, 4), "fold: %macro ... %endmacro", got.join(','));
        t.check(has(5, 7), "fold: section .data ends before the blank line", got.join(','));
        t.check(has(9, 22), "fold: section .text to the end", got.join(','));
        t.check(has(11, 16), "fold: label _start ends before the next label's comment", got.join(','));
        t.check(has(13, 15), "fold: %ifdef ... %endif", got.join(','));
        t.check(has(19, 22), "fold: label helper", got.join(','));

        const auto masm = Folding::compute({ "main PROC", "  mov eax, 1", "  ret", "main ENDP",
                                             "point STRUCT", "  x DWORD ?", "point ENDS" });
        t.check(masm.size() >= 2 && masm[0].start == 0 && masm[0].end == 3, "fold: MASM proc/endp");
        const auto gas = Folding::compute({ ".macro m", "  nop", ".endm", ".text", "_start:", "  ret" });
        bool gm = false;
        for (const auto &r : gas)
            gm = gm || (r.start == 0 && r.end == 2);
        t.check(gm, "fold: GAS .macro/.endm");
        const auto fasm = Folding::compute({ "macro m {", "  nop", "}", "label: ; not a function?", "  ret" });
        bool fb = false;
        for (const auto &r : fasm)
            fb = fb || (r.start == 0 && r.end == 2);
        t.check(fb, "fold: FASM { } braces");
        t.check(Folding::compute({}).isEmpty() && Folding::compute({ "" }).isEmpty(), "fold: empty text");
        const auto local = Folding::compute({ "_a:", "  nop", ".loop:", "  dec rcx", "  jnz .loop", "_b:", "  ret" });
        bool localNotStart = true;
        for (const auto &r : local)
            localNotStart = localNotStart && r.start != 2;
        t.check(localNotStart, "fold: a local label (.loop) does not start a region");
    }

    // ------------------------------------------------------------ folding in the editor
    {
        setText(ed, kSample, 12, 4);
        ed->setFolded(11, true);
        pump();
        bool hidden = true;
        for (int i = 12; i <= 16; ++i)
            hidden = hidden && !ed->document()->findBlockByNumber(i).isVisible();
        t.check(hidden && ed->document()->findBlockByNumber(11).isVisible(), "editor: fold hides the body, keeps the start line");
        t.check(ed->textCursor().blockNumber() == 11, "editor: the cursor leaves the folded lines",
                QString::number(ed->textCursor().blockNumber()));
        //! arrow down from the folded line steps over it
        QTextCursor c(ed->document()->findBlockByNumber(11));
        ed->setTextCursor(c);
        pump();
        key(ed, Qt::Key_Down);
        t.check(ed->textCursor().blockNumber() == 17 && ed->isFoldedAt(11),
                "editor: arrow down steps over a fold", QString::number(ed->textCursor().blockNumber()));
        key(ed, Qt::Key_Up);
        t.check(ed->textCursor().blockNumber() == 11, "editor: arrow up lands on the fold line",
                QString::number(ed->textCursor().blockNumber()));
        //! go to a hidden line (search, debugger) opens the fold
        QTextCursor inside(ed->document()->findBlockByNumber(14));
        ed->setTextCursor(inside);
        pump();
        t.check(!ed->isFoldedAt(11) && ed->document()->findBlockByNumber(14).isVisible(),
                "editor: jumping into a fold opens it");
        //! fold all / unfold all
        ed->foldAll();
        pump();
        int visible = 0;
        for (QTextBlock b = ed->document()->begin(); b.isValid(); b = b.next())
            visible += b.isVisible() ? 1 : 0;
        t.check(visible <= 6, "editor: fold all leaves only the top level", QString::number(visible));
        ed->unfoldAll();
        pump();
        visible = 0;
        for (QTextBlock b = ed->document()->begin(); b.isValid(); b = b.next())
            visible += b.isVisible() ? 1 : 0;
        t.check(visible == ed->document()->blockCount(), "editor: unfold all shows every line");
        //! editing the start line so it no longer starts a region unfolds it
        ed->setFolded(2, true);
        pump();
        QTextCursor e(ed->document()->findBlockByNumber(2));
        e.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        e.insertText("; was a macro");
        pump();
        t.check(ed->document()->findBlockByNumber(3).isVisible(), "editor: a region that disappears unfolds");
        //! folded scroll range is shorter
        setText(ed, kSample, 0);
        const int before = ed->verticalScrollBar()->maximum() + ed->verticalScrollBar()->pageStep();
        ed->setFolded(9, true);
        pump();
        const int after = ed->verticalScrollBar()->maximum() + ed->verticalScrollBar()->pageStep();
        t.check(after <= before, "editor: folding shortens the document", QString("%1 -> %2").arg(before).arg(after));
        ed->unfoldAll();
    }

    // ------------------------------------------------------------ auto-close
    {
        QSettings().setValue("autoclose", true);
        setText(ed, "mov rax, ", 0, 9);
        type(ed, "[");
        t.check(lineText(ed, 0) == "mov rax, []" && ed->textCursor().positionInBlock() == 10, "autoclose: [ inserts ]", lineText(ed, 0));
        type(ed, "rbx");
        type(ed, "]");
        t.check(lineText(ed, 0) == "mov rax, [rbx]" && ed->textCursor().positionInBlock() == 14, "autoclose: typing ] steps over", lineText(ed, 0));
        setText(ed, "x ", 0, 2);
        type(ed, "(");
        key(ed, Qt::Key_Backspace);
        t.check(lineText(ed, 0) == "x ", "autoclose: backspace removes an empty pair", lineText(ed, 0));
        setText(ed, "msg db ", 0, 7);
        type(ed, "\"");
        t.check(lineText(ed, 0) == "msg db \"\"", "autoclose: quote pair", lineText(ed, 0));
        type(ed, "hi\"");
        t.check(lineText(ed, 0) == "msg db \"hi\"", "autoclose: closing quote steps over", lineText(ed, 0));
        setText(ed, "; it", 0, 4);
        type(ed, "'");
        t.check(lineText(ed, 0) == "; it'", "autoclose: no pairs in a comment", lineText(ed, 0));
        setText(ed, "don", 0, 3);
        type(ed, "'");
        t.check(lineText(ed, 0) == "don'", "autoclose: no quote pair after a letter", lineText(ed, 0));
        setText(ed, "mov rax, rbx", 0, 9);
        type(ed, "(");
        t.check(lineText(ed, 0) == "mov rax, (rbx", "autoclose: no pair before a word", lineText(ed, 0));
        setText(ed, "lea rsi, msg", 0, 9);
        QTextCursor sc = ed->textCursor();
        sc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        ed->setTextCursor(sc);
        type(ed, "[");
        t.check(lineText(ed, 0) == "lea rsi, [msg]" && ed->textCursor().selectedText() == "msg",
                "autoclose: [ wraps the selection", lineText(ed, 0));
        QSettings().setValue("autoclose", false);
        setText(ed, "a ", 0, 2);
        type(ed, "(");
        t.check(lineText(ed, 0) == "a (", "autoclose: the setting turns it off", lineText(ed, 0));
        QSettings().setValue("autoclose", true);
    }

    // ------------------------------------------------------------ bracket match
    {
        setText(ed, "mov rax, [rbx + 4]", 0, 10);
        pump();
        const auto sels = ed->extraSelections();
        QList<int> pos;
        for (const auto &s : sels)
            if (s.cursor.hasSelection())
                pos << s.cursor.selectionStart();
        t.check(pos.contains(9) && pos.contains(17), "brackets: [ and its ] are marked",
                QString::number(pos.size()));
        setText(ed, "mov rax, [rbx ; ]", 0, 10);
        pump();
        int unmatched = 0;
        for (const auto &s : ed->extraSelections())
            if (s.format.hasProperty(QTextFormat::ForegroundBrush))
                ++unmatched;
        t.check(unmatched == 1, "brackets: a ] inside a comment does not match");
    }

    // ------------------------------------------------------------ Enter keeps the indentation
    {
        setText(ed, "_start:", 0, 7);
        key(ed, Qt::Key_Return, "\r");
        t.check(lineText(ed, 1) == "    ", "enter: indent after a label", "'" + lineText(ed, 1) + "'");
        setText(ed, "\tmov rax, 1", 0, 11);
        key(ed, Qt::Key_Return, "\r");
        t.check(lineText(ed, 1) == "\t", "enter: keeps a tab indent", "'" + lineText(ed, 1) + "'");
        setText(ed, "    ", 0, 4);
        key(ed, Qt::Key_Return, "\r");
        t.check(lineText(ed, 1) == "    ", "enter: an all-blank line (read past the end before)");
    }

    // ------------------------------------------------------------ row height and hit testing
    {
        QStringList many;
        for (int i = 0; i < 200; ++i)
            many << QString("    mov rax, %1").arg(i);
        setText(ed, many.join('\n'), 0);
        const QRect r0 = ed->cursorRect(QTextCursor(ed->document()->findBlockByNumber(0)));
        const QRect r1 = ed->cursorRect(QTextCursor(ed->document()->findBlockByNumber(1)));
        const int row = r1.top() - r0.top();
        const int expect = qRound(QFontInfo(ed->font()).pixelSize() * 1.35);
        t.check(qAbs(row - qMax(expect, QFontMetrics(ed->font()).height())) <= 1, "rows: VS Code height (font x 1.35)",
                QString("row %1, expected %2").arg(row).arg(expect));
        bool hits = true;
        for (int k : { 0, 3, 7, 12 }) {
            const QRect rk = ed->cursorRect(QTextCursor(ed->document()->findBlockByNumber(k)));
            for (int dy : { 1, rk.height() / 2, row - 2 })
                if (ed->cursorForPosition(QPoint(rk.left() + 2, rk.top() - (row - rk.height()) / 2 + dy)).blockNumber() != k)
                    hits = false;
        }
        t.check(hits, "rows: a click anywhere in a row lands on that line");
        //! scrolled
        ed->verticalScrollBar()->setValue(50);
        pump();
        const QTextBlock first = ed->document()->findBlockByNumber(50);
        const QRect rf = ed->cursorRect(QTextCursor(first));
        t.check(ed->cursorForPosition(QPoint(rf.left() + 2, rf.center().y())).blockNumber() == 50, "rows: hit test after scrolling");
    }

    // ------------------------------------------------------------ minimap
    {
        Minimap *mm = ed->minimapWidget();
        ed->setMinimapEnabled(true);
        pump();
        t.check(mm && mm->isVisible() && mm->width() == Minimap::kWidth, "minimap: shown in a wide editor");
        if (mm) {
            const int a = mm->scrollValueForY(0), b = mm->scrollValueForY(mm->height() / 2), c = mm->scrollValueForY(mm->height() - 1);
            t.check(a <= b && b <= c && c <= ed->verticalScrollBar()->maximum() && a >= 0, "minimap: click positions map in order",
                    QString("%1 %2 %3").arg(a).arg(b).arg(c));
        }
        ed->setGeometry(0, 0, 420, 600);
        pump();
        t.check(mm && !mm->isVisible(), "minimap: hidden in a narrow editor");
        ed->setGeometry(0, 0, 1100, 700);
        ed->setMinimapEnabled(false);
        pump();
        t.check(mm && !mm->isVisible(), "minimap: the setting hides it");
        ed->setMinimapEnabled(true);
    }

    // ------------------------------------------------------------ painting does not crash
    {
        setText(ed, kSample, 12);
        ed->setFolded(2, true);
        ed->viewport()->repaint();
        ed->repaint();
        pump();
        const QImage img = ed->grab().toImage();
        t.check(!img.isNull() && img.width() > 100, "paint: the editor draws with folds, guides and minimap");
    }

    }   // the two directions
    t.prefix.clear();
    t.check(!g_warnings.join('\n').contains("Could not parse"), "stylesheet: parses without errors");

    const QString logPath = qEnvironmentVariable("ASMON_TEST_LOG");
    if (!logPath.isEmpty()) {
        QFile f(logPath);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            for (const QString &l : t.log)
                out << "selftest " << l << "\n";
            out << QString("selftest done passed=%1 failed=%2\n").arg(t.passed).arg(t.failed);
        }
    }
    std::printf("selftest done passed=%d failed=%d\n", t.passed, t.failed);
    return t.failed;
}

void installSelfTestMessageHandler()
{
    g_previous = qInstallMessageHandler(collect);
}
