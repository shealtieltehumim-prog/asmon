/****************************************************************************
** SASM - simple IDE for assembler development
** Copyright (C) 2013 Dmitriy Manushin
** Contact: site: http://dman95.github.io/SASM/
**          e-mail: Dman1095@gmail.com
**
** This file is part of SASM.
**
** SASM is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** SASM is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with SASM.  If not, see <http://www.gnu.org/licenses/>.
**
** Этот файл — часть SASM.
**
** SASM - свободная программа: вы можете перераспространять ее и/или
** изменять ее на условиях Стандартной общественной лицензии GNU в том виде,
** в каком она была опубликована Фондом свободного программного обеспечения;
** либо версии 3 лицензии, либо (по вашему выбору) любой более поздней
** версии.
**
** SASM распространяется в надежде, что она будет полезной,
** но БЕЗО ВСЯКИХ ГАРАНТИЙ; даже без неявной гарантии ТОВАРНОГО ВИДА
** или ПРИГОДНОСТИ ДЛЯ ОПРЕДЕЛЕННЫХ ЦЕЛЕЙ. Подробнее см. в Стандартной
** общественной лицензии GNU.
**
** Вы должны были получить копию Стандартной общественной лицензии GNU
** вместе с этой программой. Если это не так, см.
** <http://www.gnu.org/licenses/>.)
**
****************************************************************************/

#include "theme.h"
#include "regexp.h"
#include "codeeditor.h"
#include <QTextLayout>
#include <QCompleter>
#include <QStringListModel>
#include <QAbstractItemView>
#include <QScrollBar>
#include "symbols.h"
#include <QTimer>
#include <QMimeData>

//! Hebrew display marks managed by the editor (see "Hebrew (bidi)" below)
static bool isBidiMark(QChar c)
{
    return c == QChar(0x200E) || c == QChar(0x202B);
}
#include <QPainter>
#include <QTextBlock>
#include <QHelpEvent>
#include <QToolTip>
#include <QMouseEvent>

/**
 * @file codeeditor.cpp
 * Implements the code editing area
 */

CodeEditor::CodeEditor(QWidget *parent, bool withBeakpoints) :
    RuQPlainTextEdit(parent), debugImage(":/images/debugLine.png"),
    breakpointImage(":/images/breakpoint.png"),
    settings()
{
    hasBreakpoints = withBeakpoints;
    prevBlockCount = -1;
    setDebugMode(false);
    debugAreaWidth = 3 + debugImage.width() + 1;
    setWordWrapMode(QTextOption::NoWrap);
    //! Asmon: code is always left-to-right, even in the Hebrew (RTL) interface.
    //! Every line is laid out as an LTR paragraph, so a line such as
    //! "mov rax, 1 ; הערה" keeps its code on the left and the Hebrew comment
    //! is shown as a right-to-left run inside it (Unicode bidi).
    setLayoutDirection(Qt::LeftToRight);
    {
        QTextOption option = document()->defaultTextOption();
        option.setTextDirection(Qt::LeftToRight);
        option.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
        document()->setDefaultTextOption(option);
        //! Arrow keys move visually: inside a Hebrew run "right" really goes
        //! right (Qt's default logical movement jumps back and forth there).
        document()->setDefaultCursorMoveStyle(
            settings.value("visualcursor", true).toBool() ? Qt::VisualMoveStyle : Qt::LogicalMoveStyle);
    }
    firstTopMargin = contentOffset().y();
    lineNumberArea = new LineNumberArea(this);
    lineNumberArea->setLayoutDirection(Qt::LeftToRight);
    lineNumberPanelColor = Theme::color("linenumberpanelcolor");

    connect(this, SIGNAL(blockCountChanged(int)), this, SLOT(updateLineNumberAreaWidth(int)));
    connect(this, SIGNAL(updateRequest(QRect,int)), this, SLOT(updateLineNumberArea(QRect,int)));
    connect(this, SIGNAL(cursorPositionChanged()), this, SLOT(highlightCurrentLine()));
    //! Asmon: Hebrew in strings and comments (see updateBidiHint)
    connect(document(), &QTextDocument::contentsChange, this, &CodeEditor::updateBidiHints);
    //! a removed breakpoint forgets its condition
    connect(this, &CodeEditor::breakpointsChanged, this, [this](quint64 line, bool added) {
        if (!added)
            conditions.remove(int(line));
    });
    connect(this, SIGNAL(blockCountChanged(int)), this, SLOT(shiftBreakpoints(int)));

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
    currentDebugLine = -1;
}

int CodeEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    //! room for at least 3 digits and a margin: the gutter does not jump
    //! between 9 and 10 lines, and numbers do not touch the window edge
    int space = fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(digits, 3) + 14 + debugAreaWidth;

    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
        //! Indirectly invokes CodeEditor::lineNumberAreaPaintEvent() throw virtual LineNumberArea::paintEvent()

    //! Viewport - visible part of widget
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    // !Paint on line number area
    QPainter painter(lineNumberArea);
    painter.setLayoutDirection(Qt::LeftToRight); // Asmon: same gutter in RTL UI
    painter.fillRect(event->rect(), lineNumberPanelColor);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + (int) blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(Theme::color("linenumberfontcolor"));
            painter.drawText(0, top, lineNumberArea->width() - debugAreaWidth, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignAbsolute, number);

            if (blockNumber + 1 == currentDebugLine)
                //! Blocks counting starts with 0, line number is equivalent (block number + 1)
                //if QTextOption::NoWrap is not set, lines count - visible lines, block count - lines divided by '\n'
                painter.drawPixmap(lineNumberArea->width() - debugAreaWidth + 3,
                                   top + fontMetrics().height() / 2 - debugImage.height() / 2,
                                   debugImage.width(), debugImage.height(), debugImage);
            if (diagnostics.contains(blockNumber + 1) && blockNumber + 1 != currentDebugLine
                    && !breakpoints.contains(blockNumber + 1)) {
                //! a small round mark for a build error (red) or warning (amber)
                const int d = qMax(6, fontMetrics().height() / 3);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(Qt::NoPen);
                painter.setBrush(diagnostics[blockNumber + 1].error ? QColor("#f14c4c") : QColor("#cca700"));
                painter.drawEllipse(lineNumberArea->width() - debugAreaWidth + 6,
                                    top + fontMetrics().height() / 2 - d / 2, d, d);
                painter.setBrush(Qt::NoBrush);
            }
            if (breakpoints.contains(blockNumber + 1) && hasBreakpoints) {
                painter.drawPixmap(lineNumberArea->width() - debugAreaWidth + 3,
                                   top + fontMetrics().height() / 2 - breakpointImage.height() / 2,
                                   breakpointImage.width(), breakpointImage.height(), breakpointImage);
                if (conditions.contains(blockNumber + 1)) {
                    //! conditional: an orange ring around the dot
                    painter.setRenderHint(QPainter::Antialiasing);
                    painter.setPen(QPen(QColor("#f0a030"), 2));
                    painter.setBrush(Qt::NoBrush);
                    const int d = breakpointImage.height() + 2;
                    painter.drawEllipse(lineNumberArea->width() - debugAreaWidth + 2,
                                        top + fontMetrics().height() / 2 - d / 2, d, d);
                }
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + (int) blockBoundingRect(block).height();
        ++blockNumber;
    }
}

void CodeEditor::lineNumberAreaMousePressEvent(QMouseEvent *event)
{
    if (hasBreakpoints) {
        //! If mouse click has been made - add breakpoint
        //! Counting line number
        int lineNumber = 0;
        int sumHeight = 0;
        QTextBlock block = firstVisibleBlock();
        while (block.isValid() && event->y() > sumHeight) {
            sumHeight += blockBoundingGeometry(block).height();
            block = block.next();
            lineNumber++;
        }
        lineNumber += verticalScrollBar()->value(); //+ invisible lines

        if (lineNumber <= document()->lineCount()) {
            //blocks counting starts with 0
            QTextBlock targetBlock = document()->findBlockByLineNumber(lineNumber - 1);
            lineNumber = targetBlock.blockNumber() + 1; //line number to paint

            //! Move text cursor to clicked line to prevent view from
            //! scrolling back to old cursor position (e.g. top of file)
            QTextCursor cursor(targetBlock);
            setTextCursor(cursor);

            //add or remove line number in list
            if (breakpoints.contains(lineNumber)) {
                breakpoints.removeOne(lineNumber);
                emit breakpointsChanged(lineNumber, false);
            } else {
                breakpoints.append(lineNumber);
                emit breakpointsChanged(lineNumber, true);
            }

            repaintLineNumberArea();
        }
    }
}

void CodeEditor::setBreakpointOnCurrentLine()
{
    if (hasBreakpoints) {
        int lineNumber = textCursor().blockNumber() + 1;
        if (lineNumber <= document()->lineCount()) {
            //blocks counting starts with 0
            lineNumber = document()->findBlockByLineNumber(lineNumber - 1).blockNumber() + 1; //line number to paint

            //add or remove line number in list
            if (breakpoints.contains(lineNumber)) {
                breakpoints.removeOne(lineNumber);
                emit breakpointsChanged(lineNumber, false);
            } else {
                breakpoints.append(lineNumber);
                emit breakpointsChanged(lineNumber, true);
            }

            repaintLineNumberArea();
        }
    }
}

void CodeEditor::repaintLineNumberArea()
{
    //repaint without triggering viewport margin update (which resets scroll position)
    lineNumberArea->update();
}

QList<int> *CodeEditor::getBreakpoints()
{
    return &breakpoints;
}

void CodeEditor::highlightCurrentLine()
{
    if (!debugMode) {
        if (settings.value("currentlinemode", true).toBool()) {
            QList<QTextEdit::ExtraSelection> extraSelections;

            if (!isReadOnly()) {
                QTextEdit::ExtraSelection selection;

                QColor lineColor = Theme::color("currentlinecolor");

                selection.format.setBackground(lineColor);
                selection.format.setProperty(QTextFormat::FullWidthSelection, true);
                selection.cursor = textCursor();
                selection.cursor.clearSelection();
                extraSelections.append(selection);
            }
            appendDiagnosticSelections(extraSelections);
            setExtraSelections(extraSelections);
        } else {
            QList<QTextEdit::ExtraSelection> extraSelections; //empty
            appendDiagnosticSelections(extraSelections);
            setExtraSelections(extraSelections);
        }
    }
}

void CodeEditor::highlightDebugLine(int lineNumber, bool moveCursor)
{
    if (debugMode) {
        QList<QTextEdit::ExtraSelection> extraSelections;

        if (!isReadOnly() && lineNumber > 0) {
            QTextEdit::ExtraSelection selection;

            QColor lineColor = Theme::color("debuglinecolor");

            selection.format.setBackground(lineColor);
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = QTextCursor(document());
            selection.cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineNumber - 1);
            selection.cursor.clearSelection();
            if (moveCursor) {
                setTextCursor(selection.cursor); //scroll to debugging line
                centerCursor(); //ensure the debug line is visible and centered in viewport
            }
            extraSelections.append(selection);
        }

        setExtraSelections(extraSelections);
    }
}

void CodeEditor::updateDebugLine(int number)
{
    //number > 0 => highlight line
    //number == -1 => exit from debug mode
    //number == -2 => does not highlight any line, but does not exit from debug mode
    //last case for waiting program stops on next instruction or breakpoint
    if (number == -1)
        setDebugMode(false);
    else
        setDebugMode(true);
    if (number != -2)
        currentDebugLine = number;

    //create rectangle of line number area and highlight debug line throw updateRequest()
    QRect lineNumberAreaRect(lineNumberArea->x(), lineNumberArea->y(),
                             lineNumberArea->width(), lineNumberArea->height());
    emit updateRequest(lineNumberAreaRect, 0);

    highlightDebugLine(number, true);
    highlightCurrentLine();
}

void CodeEditor::setDebugMode(bool mode)
{
    debugMode = mode;
}

void CodeEditor::putTab()
{
    QTextCursor cursor = textCursor();
    if (cursor.selectionEnd() - cursor.selectionStart() <= 0) {
        int spacesToAdd = 4 - cursor.columnNumber() % 4;
        QString insertion(spacesToAdd, ' ');
        insertPlainText(insertion);
    } else {
        QString insertion = "    ";
        QTextBlock firstBlock = document()->findBlock(cursor.selectionStart());
        QTextBlock lastBlock = document()->findBlock(cursor.selectionEnd() - 1);

        cursor.setPosition(firstBlock.position());
        cursor.beginEditBlock();
        do {
            cursor.insertText(insertion);
        } while (cursor.movePosition(QTextCursor::NextBlock) && cursor.position() <= lastBlock.position());
        cursor.endEditBlock();
    }
}

void CodeEditor::deleteTab()
{
    QTextCursor cursor = textCursor();
    if (cursor.selectionEnd() - cursor.selectionStart() <= 0) {
        int spacesToDelete = 4 - (4 - cursor.columnNumber() % 4) % 4;
        for (int i = 0; i < spacesToDelete; ++i) {
            if (cursor.columnNumber() != 0 && document()->characterAt(cursor.position() - 1) == ' ') {
                cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
            } else {
                break;
            }
        }
        cursor.removeSelectedText();
    } else {
        QTextBlock firstBlock = document()->findBlock(cursor.selectionStart());
        QTextBlock lastBlock = document()->findBlock(cursor.selectionEnd() - 1);
        cursor.setPosition(firstBlock.position());
        cursor.beginEditBlock();
        do {
            int spacesToDelete = 4;
            for (int i = 0; i < spacesToDelete; ++i) {
                if (document()->characterAt(cursor.position()) == ' ') {
                    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
                } else {
                    break;
                }
            }
            cursor.removeSelectedText();
        } while (cursor.movePosition(QTextCursor::NextBlock) && cursor.position() <= lastBlock.position());
        cursor.endEditBlock();
    }
}

void CodeEditor::keyPressEvent(QKeyEvent *e)
{
    //! auto-completion: while the list is open it takes Enter/Tab/Esc
    if (completer && completer->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Tab:
            if (completer->popup()->currentIndex().isValid()) {
                insertCompletion(completer->popup()->currentIndex().data().toString());
                completer->popup()->hide();
                return;
            }
            completer->popup()->hide();
            break;
        case Qt::Key_Escape: case Qt::Key_Backtab:
            completer->popup()->hide();
            return;
        default:
            break;
        }
    }
    if (e->key() == Qt::Key_Space && (e->modifiers() & Qt::ControlModifier)) {
        updateCompletion(true);
        return;
    }
    const bool typing = !e->text().isEmpty() && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier));
    struct CompletionAfter {
        CodeEditor *ed; bool typing; int key;
        ~CompletionAfter() {
            if (typing || (key == Qt::Key_Backspace && ed->completer && ed->completer->popup()->isVisible()))
                ed->updateCompletion(false);
        }
    } completionAfter{ this, typing, e->key() };
    //! Hebrew display marks are invisible: never stop on them, never make
    //! Backspace/Delete "do nothing" by deleting one.
    if (!textCursor().hasSelection()) {
        const int pos = textCursor().position();
        if (e->key() == Qt::Key_Backspace && pos > 0 && isBidiMark(document()->characterAt(pos - 1))) {
            QTextCursor c = textCursor();
            c.setPosition(pos - 1);
            setTextCursor(c);
        } else if (e->key() == Qt::Key_Delete && isBidiMark(document()->characterAt(pos))) {
            QTextCursor c = textCursor();
            c.setPosition(pos + 1);
            setTextCursor(c);
        }
    }
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        const int before = textCursor().position();
        QPlainTextEdit::keyPressEvent(e);
        const int after = textCursor().position();
        if (after != before) {
            bool onlyMarks = true;
            for (int i = qMin(before, after); i < qMax(before, after); ++i)
                if (!isBidiMark(document()->characterAt(i))) { onlyMarks = false; break; }
            if (onlyMarks)
                QPlainTextEdit::keyPressEvent(e);
        }
        return;
    }
    QString curString = textCursor().block().text();
    int indentWidth = -1;
    switch (e->key()) {
    case (Qt::Key_Tab) :
        if (! (e->modifiers() & Qt::ShiftModifier))
            putTab();
        break;
    case (Qt::Key_Enter) :
    case (Qt::Key_Return) :
        while (curString[++indentWidth] == ' ');
        QPlainTextEdit::keyPressEvent(e);
        for (int i = 0; i < indentWidth; i++)
            insertPlainText(" ");
        break;
    case (Qt::Key_Q) :
        if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::ControlModifier))
            commentSelectedCode();
        else
            QPlainTextEdit::keyPressEvent(e);
        break;
    case (Qt::Key_A) :
        if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::ControlModifier))
            uncommentSelectedCode();
        else
            QPlainTextEdit::keyPressEvent(e);
        break;
    default:
        QPlainTextEdit::keyPressEvent(e);
    }
}

void CodeEditor::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        RuQPlainTextEdit::dragEnterEvent(event);
    }
}

void CodeEditor::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        foreach (const QUrl &url, event->mimeData()->urls())
        {
            const QString &fileName = url.toLocalFile();

            if (fileName.isEmpty())
                continue;

            emit fileOpened(fileName);
        }
        event->acceptProposedAction();
    } else {
        RuQPlainTextEdit::dropEvent(event);
    }
}

void CodeEditor::shiftBreakpoints(int blockCount)
{
    if (prevBlockCount == -1) {
        prevBlockCount = this->blockCount();
        return;
    }
    //remove breakpoints in range
    int changedLine = textCursor().blockNumber() + 1; //blocks starts from 0
    if (blockCount < prevBlockCount) {
        for (int i = changedLine + 1; i <= changedLine + prevBlockCount - blockCount; i++) {
            breakpoints.removeOne(i);
        }
    }

    //shift breakpoints
    if (blockCount > prevBlockCount) {
        for (int i = 0; i < breakpoints.size(); i++) {
            if (breakpoints[i] >= changedLine)
                breakpoints[i] += blockCount - prevBlockCount;
        }
    } else {
        for (int i = 0; i < breakpoints.size(); i++) {
            if (breakpoints[i] > changedLine)
                breakpoints[i] += blockCount - prevBlockCount;
        }
    }
    prevBlockCount = blockCount;
    repaintLineNumberArea();
}

bool CodeEditor::isMacroOnCurrentDebugLine()
{
    if (currentDebugLine > 0) {
        QTextBlock block = document()->findBlockByLineNumber(currentDebugLine - 1);
        QString text = block.text();
        QStringList macrosPatterns;
        macrosPatterns << "\\bPRINT_DEC\\b" << "\\bPRINT_HEX\\b" <<
                          "\\bPRINT_CHAR\\b" << "\\bPRINT_STRING\\b" <<
                          "\\bNEWLINE\\b" << "\\bPRINT_UDEC\\b" <<
                          "\\bGET_UDEC\\b" << "\\bGET_DEC\\b" <<
                          "\\bGET_HEX\\b" << "\\bGET_CHAR\\b" <<
                          "\\bGET_STRING\\b" << "\\bCMAIN\\b" << "\\bCEXTERN\\b";
        foreach (const QString &pattern, macrosPatterns) {
            RegExp regExp(pattern, Qt::CaseSensitive);
            if (text.indexOf(regExp) != -1)
                return true;
        }
    }
    return false;
}

CodeEditor::~CodeEditor()
{
    delete lineNumberArea;
}

void CodeEditor::applyThemeColors()
{
    RuQPlainTextEdit::applyThemeColors();
    lineNumberPanelColor = Theme::color("linenumberpanelcolor");
    repaintLineNumberArea();
}

// ---------------------------------------------------------------- diagnostics

void CodeEditor::setDiagnostics(const QMap<int, Diagnostic> &lineDiagnostics)
{
    diagnostics = lineDiagnostics;
    if (!diagnostics.isEmpty()) {
        //! the first edit makes the marks stale: clear them then
        connect(document(), &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
            if (removed == 0 && added == 0)
                return;
            disconnect(document(), &QTextDocument::contentsChange, this, nullptr);
            diagnostics.clear();
            highlightCurrentLine();
            repaintLineNumberArea();
        }, Qt::UniqueConnection);
    }
    highlightCurrentLine();
    repaintLineNumberArea();
}

void CodeEditor::appendDiagnosticSelections(QList<QTextEdit::ExtraSelection> &list)
{
    for (auto it = diagnostics.constBegin(); it != diagnostics.constEnd(); ++it) {
        const QTextBlock block = document()->findBlockByNumber(it.key() - 1);
        if (!block.isValid())
            continue;
        const QString text = block.text();
        int start = 0;
        while (start < text.size() && text.at(start).isSpace())
            ++start;
        int end = text.indexOf(';');
        if (end < 0)
            end = text.size();
        while (end > start && text.at(end - 1).isSpace())
            --end;
        if (end <= start)
            continue;
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(block);
        sel.cursor.setPosition(block.position() + start);
        sel.cursor.setPosition(block.position() + end, QTextCursor::KeepAnchor);
        sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        sel.format.setUnderlineColor(it.value().error ? QColor("#f14c4c") : QColor("#cca700"));
        list.append(sel);
    }
}

bool CodeEditor::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip && !diagnostics.isEmpty()) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int line = cursorForPosition(viewport()->mapFromGlobal(he->globalPos())).blockNumber() + 1;
        if (diagnostics.contains(line)) {
            QToolTip::showText(he->globalPos(), diagnostics[line].message, this);
            return true;
        }
        QToolTip::hideText();
        e->ignore();
        return true;
    }
    return RuQPlainTextEdit::event(e);
}

// ---------------------------------------------------------------- Hebrew (bidi)

//! The editor is left-to-right. Hebrew in a string and in the comment after
//! it would form one right-to-left run under the Unicode bidi algorithm, so
//! the line is shown scrambled (e.g. db 'שלום', 0 ; הודעה). One invisible
//! direction mark per line fixes the common cases:
//!   - an LRM right after a Hebrew string that has code after it;
//!   - an RLE after "; " of a comment written in Hebrew (its first strong
//!     letter is Hebrew): the comment reads as one right-to-left sentence.
//! The marks are real characters in the editor, managed here only: they are
//! removed when saving/building (plainTextWithoutMarks) and copying, the
//! cursor keys step over them, and the status bar does not count them.

QString CodeEditor::stripBidiMarks(QString text)
{
    text.remove(QChar(0x200E));
    text.remove(QChar(0x202B));
    return text;
}

QString CodeEditor::plainTextWithoutMarks() const
{
    return stripBidiMarks(toPlainText());
}

int CodeEditor::marksBefore(const QTextCursor &c)
{
    const QString t = c.block().text().left(c.positionInBlock());
    return t.count(QChar(0x200E)) + t.count(QChar(0x202B));
}

//! where the mark goes in a line (clean text, no marks): -1 = none
static int bidiMarkPosition(const QString &t, QChar *mark)
{
    auto isRtl = [](QChar c) {
        const QChar::Direction d = c.direction();
        return d == QChar::DirR || d == QChar::DirAL;
    };
    bool anyRtl = false;
    for (QChar c : t)
        if (isRtl(c)) { anyRtl = true; break; }
    if (!anyRtl)
        return -1;
    int commentStart = -1;
    int lastRtlStringEnd = -1;
    QChar quote;
    int stringStart = -1;
    for (int i = 0; i < t.size(); ++i) {
        const QChar c = t.at(i);
        if (!quote.isNull()) {
            if (c == quote) {
                for (int k = stringStart; k < i; ++k)
                    if (isRtl(t.at(k))) { lastRtlStringEnd = i + 1; break; }
                quote = QChar();
            }
        } else if (c == '\'' || c == '"' || c == '`') {
            quote = c;
            stringStart = i + 1;
        } else if (c == ';') {
            commentStart = i;
            break;
        }
    }
    if (lastRtlStringEnd > 0 && !t.mid(lastRtlStringEnd).trimmed().isEmpty()) {
        *mark = QChar(0x200E); // LRM
        return lastRtlStringEnd;
    }
    if (commentStart >= 0) {
        bool commentRtl = false;
        for (int i = commentStart + 1; i < t.size(); ++i) {
            if (isRtl(t.at(i))) { commentRtl = true; break; }
            if (t.at(i).direction() == QChar::DirL) break;
        }
        if (commentRtl) {
            int p = commentStart + 1;
            while (p < t.size() && t.at(p).isSpace())
                ++p;
            *mark = QChar(0x202B); // RLE
            return p;
        }
    }
    return -1;
}

void CodeEditor::updateBidiHints(int position, int charsRemoved, int charsAdded)
{
    Q_UNUSED(charsRemoved);
    if (applyingBidiMarks)
        return;
    //! edit the document after the user's edit has finished (not inside the
    //! signal), merged into the same undo step
    const int from = position, to = position + charsAdded;
    QTimer::singleShot(0, this, [this, from, to]() {
        QTextBlock b = document()->findBlock(from);
        const QTextBlock last = document()->findBlock(qMin(to, document()->characterCount() - 1));
        applyingBidiMarks = true;
        const bool wasModified = document()->isModified();
        QTextCursor c(document());
        bool editing = false;
        while (b.isValid()) {
            const QString text = b.text();
            const QString clean = stripBidiMarks(text);
            QChar mark;
            const int pos = bidiMarkPosition(clean, &mark);
            QString wanted = clean;
            if (pos >= 0)
                wanted.insert(pos, mark);
            if (wanted != text) {
                if (!editing) {
                    c.joinPreviousEditBlock();
                    editing = true;
                }
                //! remove the old marks (from the end), then insert the new one
                for (int i = text.size() - 1; i >= 0; --i) {
                    if (isBidiMark(text.at(i))) {
                        c.setPosition(b.position() + i);
                        c.deleteChar();
                    }
                }
                if (pos >= 0) {
                    c.setPosition(b.position() + pos);
                    c.insertText(QString(mark));
                }
            }
            if (b == last)
                break;
            b = b.next();
        }
        if (editing) {
            c.endEditBlock();
            if (!wasModified) {
                //! just loaded: the marks are not a change of the file
                //! (clear the history first: clearing it marks the document modified again)
                document()->clearUndoRedoStacks();
                document()->setModified(false);
            }
        }
        applyingBidiMarks = false;
    });
}

QMimeData *CodeEditor::createMimeDataFromSelection() const
{
    QMimeData *data = RuQPlainTextEdit::createMimeDataFromSelection();
    if (data && data->hasText())
        data->setText(stripBidiMarks(data->text()));
    return data;
}

// ---------------------------------------------------------------- Ctrl+click

void CodeEditor::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        setTextCursor(cursorForPosition(e->position().toPoint()));
        emit definitionRequested();
        e->accept();
        return;
    }
    RuQPlainTextEdit::mousePressEvent(e);
}

void CodeEditor::mouseMoveEvent(QMouseEvent *e)
{
    //! a hand over words while Ctrl is held: they are links to definitions
    viewport()->setCursor((e->modifiers() & Qt::ControlModifier) ? Qt::PointingHandCursor : Qt::IBeamCursor);
    RuQPlainTextEdit::mouseMoveEvent(e);
}

// ---------------------------------------------------------------- completion

static QStringList baseCompletionWords()
{
    static QStringList words;
    if (!words.isEmpty())
        return words;
    const char *instructions =
        "mov movzx movsx movsxd lea push pop xchg cmpxchg xadd add sub adc sbb inc dec neg mul imul div idiv "
        "and or xor not test cmp shl shr sal sar rol ror rcl rcr shld shrd bt bts btr btc bsf bsr lzcnt tzcnt popcnt "
        "jmp je jne jz jnz jg jge jl jle ja jae jb jbe jc jnc jo jno js jns jp jnp jcxz jecxz jrcxz loop loope loopne "
        "call ret enter leave nop int syscall sysenter hlt cpuid rdtsc pause ud2 "
        "cmove cmovne cmovz cmovnz cmovg cmovge cmovl cmovle cmova cmovae cmovb cmovbe cmovs cmovns "
        "sete setne setz setnz setg setge setl setle seta setae setb setbe sets setns setc setnc "
        "cbw cwde cdqe cwd cdq cqo clc stc cmc cld std cli sti pushf popf pushfq popfq lahf sahf "
        "movsb movsw movsd movsq stosb stosw stosd stosq lodsb lodsw lodsd lodsq scasb scasw scasd scasq "
        "cmpsb cmpsw cmpsd cmpsq rep repe repne repz repnz lock "
        "movd movq movdqa movdqu movaps movups movapd movupd movss movsd addss addsd subss subsd mulss mulsd "
        "divss divsd sqrtss sqrtsd comiss comisd ucomiss ucomisd cvtsi2ss cvtsi2sd cvttss2si cvttsd2si cvtss2sd "
        "cvtsd2ss pxor por pand pandn paddb paddw paddd paddq psubb psubw psubd psubq pcmpeqb pcmpeqd pshufb "
        "pshufd punpcklbw xorps xorpd andps andpd orps orpd maxss minss maxsd minsd "
        "vmovaps vmovups vaddps vmulps vxorps vpxor vzeroupper "
        "fld fst fstp fild fistp fadd fsub fmul fdiv fsqrt fcomi fcomip fxch finit fldz fld1";
    const char *registers =
        "rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11 r12 r13 r14 r15 rip "
        "eax ebx ecx edx esi edi ebp esp r8d r9d r10d r11d r12d r13d r14d r15d "
        "ax bx cx dx si di bp sp r8w r9w r10w r11w r12w r13w r14w r15w "
        "al bl cl dl sil dil bpl spl ah bh ch dh r8b r9b r10b r11b r12b r13b r14b r15b "
        "xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 xmm8 xmm9 xmm10 xmm11 xmm12 xmm13 xmm14 xmm15 "
        "ymm0 ymm1 ymm2 ymm3 cs ds es fs gs ss";
    const char *directives =
        "section segment global extern default rel bits db dw dd dq dt resb resw resd resq times equ "
        "byte word dword qword tword ptr align alignb incbin struc endstruc istruc iend at "
        ".text .data .bss .rodata %include %define %undef %assign %macro %endmacro %if %elif %else %endif "
        "%ifdef %ifndef %rep %endrep %local %push %pop "
        "PRINT_STRING PRINT_DEC PRINT_UDEC PRINT_HEX PRINT_CHAR NEWLINE GET_DEC GET_UDEC GET_HEX GET_CHAR "
        "GET_STRING CMAIN CEXTERN";
    for (const char *list : { instructions, registers, directives })
        words << QString::fromLatin1(list).split(' ', Qt::SkipEmptyParts);
    return words;
}

void CodeEditor::updateCompletion(bool force)
{
    QTextCursor tc = textCursor();
    const QString line = stripBidiMarks(tc.block().text());
    const int col = tc.positionInBlock() - marksBefore(tc);
    //! not in a comment or a string
    if (Symbols::codePart(line).size() < col
            || Symbols::codePart(line).mid(0, col) != line.mid(0, col)) {
        if (completer)
            completer->popup()->hide();
        return;
    }
    int start = col;
    while (start > 0 && (line.at(start - 1).isLetterOrNumber() || line.at(start - 1) == '_'
                         || line.at(start - 1) == '.' || line.at(start - 1) == '%'))
        --start;
    const QString prefix = line.mid(start, col - start);
    if ((!force && prefix.size() < 2) || (!prefix.isEmpty() && prefix.at(0).isDigit())) {
        if (completer)
            completer->popup()->hide();
        return;
    }
    if (!completer) {
        completer = new QCompleter(this);
        completer->setWidget(this);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchStartsWith);
        completer->setMaxVisibleItems(9);
        completer->popup()->setLayoutDirection(Qt::LeftToRight);
        completer->popup()->setFont(font());
        connect(completer, QOverload<const QString &>::of(&QCompleter::activated),
                this, &CodeEditor::insertCompletion);
    }
    //! this file's labels, data and constants first, then the language
    QStringList words;
    QString parent;
    for (const Symbols::Symbol &sym : Symbols::scan(plainTextWithoutMarks(), QString()))
        if (!words.contains(sym.name))
            words << sym.name;
    for (const QString &w : baseCompletionWords())
        if (!words.contains(w))
            words << w;
    words.removeAll(prefix); // the word as already typed
    const bool upper = prefix.size() > 1 && prefix == prefix.toUpper() && prefix != prefix.toLower();
    QStringList shown;
    for (const QString &w : words) {
        if (!w.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        //! instructions/registers follow the case the user types
        shown << ((upper && w == w.toLower()) ? w.toUpper() : w);
    }
    if (shown.isEmpty()) {
        completer->popup()->hide();
        return;
    }
    auto *model = qobject_cast<QStringListModel *>(completer->model());
    if (!model) {
        model = new QStringListModel(completer);
        completer->setModel(model);
    }
    model->setStringList(shown);
    completer->setCompletionPrefix(prefix);
    completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
    QRect r = cursorRect();
    r.setWidth(qMax(220, completer->popup()->sizeHintForColumn(0)
                             + completer->popup()->verticalScrollBar()->sizeHint().width() + 24));
    completer->complete(r);
}

void CodeEditor::insertCompletion(const QString &word)
{
    QTextCursor tc = textCursor();
    const QString line = tc.block().text();
    int start = tc.positionInBlock();
    while (start > 0 && (line.at(start - 1).isLetterOrNumber() || line.at(start - 1) == '_'
                         || line.at(start - 1) == '.' || line.at(start - 1) == '%'))
        --start;
    tc.setPosition(tc.block().position() + start, QTextCursor::KeepAnchor);
    tc.insertText(word);
    setTextCursor(tc);
}

void CodeEditor::setBreakpointCondition(int line, const QString &condition)
{
    if (condition.isEmpty()) {
        conditions.remove(line);
    } else {
        conditions.insert(line, condition);
        if (!breakpoints.contains(line)) {
            breakpoints.append(line);
            emit breakpointsChanged(quint64(line), true);
        }
    }
    repaintLineNumberArea();
}
