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

#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QPainter>
#include <QTextBlock>
#include <QScrollBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include "ruqplaintextedit.h"
#include <QUrl>

/**
 * @file codeeditor.h
 * Contains definitions for objects pertaining to the code editing section
 */

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QResizeEvent;
class QSize;
class QWidget;
QT_END_NAMESPACE

class LineNumberArea;


class CodeEditor : public RuQPlainTextEdit
{
    Q_OBJECT

public:
    void applyThemeColors() override;
    CodeEditor(QWidget *parent = 0, bool withBeakpoints = true);
    ~CodeEditor();

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    void lineNumberAreaMousePressEvent(QMouseEvent *event);
    int lineNumberAreaWidth();
    void repaintLineNumberArea();
    bool isMacroOnCurrentDebugLine();
    int currentDebugLine;
    bool debugMode;
    //! Asmon: build errors/warnings shown in the code (wavy underline, a mark
    //! in the gutter, the message as a tooltip). Cleared on the next edit.
    struct Diagnostic { QString message; bool error; };
    void setDiagnostics(const QMap<int, Diagnostic> &lineDiagnostics);
    //! conditional breakpoints (line -> condition); setting one also sets the breakpoint
    QString breakpointCondition(int line) const { return conditions.value(line); }
    const QMap<int, QString> &breakpointConditions() const { return conditions; }
    void setBreakpointCondition(int line, const QString &condition);
    //! Ctrl+Space or typing: suggest instructions, registers, directives,
    //! io macros and this file's labels
    void showCompletion() { updateCompletion(true); }
    //! Hebrew display marks (see codeeditor.cpp): never saved or copied
    static QString stripBidiMarks(QString text);
    QString plainTextWithoutMarks() const;
    static int marksBefore(const QTextCursor &c);

public slots:
    void updateDebugLine(int number);
    void putTab();
    void deleteTab();
    void highlightCurrentLine();
    void highlightDebugLine(int lineNumber, bool moveCursor);
    void setDebugMode(bool mode);
    QList<int> *getBreakpoints();
    void setBreakpointOnCurrentLine();

protected:
    void resizeEvent(QResizeEvent *event);
    void keyPressEvent(QKeyEvent *e);
    bool event(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    QMimeData *createMimeDataFromSelection() const override;

    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);
    
private slots:
    void updateBidiHints(int position, int charsRemoved, int charsAdded);
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &, int);
    void shiftBreakpoints(int blockCount);

private:
    QWidget *lineNumberArea;
    int debugAreaWidth;
    QPixmap debugImage;
    QPixmap breakpointImage;
    //! Breakpoint line numbers
    QList<int> breakpoints;
    int firstTopMargin;
    bool hasBreakpoints;
    int prevBlockCount;
    QSettings settings;
    QColor lineNumberPanelColor;
    QMap<int, Diagnostic> diagnostics;
    QMap<int, QString> conditions;
    bool applyingBidiMarks = false;
    class QCompleter *completer = nullptr;
    void updateCompletion(bool force);
    void insertCompletion(const QString &word);
    void appendDiagnosticSelections(QList<QTextEdit::ExtraSelection> &list);

signals:
    void breakpointsChanged(quint64 lineNumber, bool isAdded);
    void fileOpened(QString path);
    void definitionRequested(); //!< Ctrl+click on a word
};


class LineNumberArea : public QWidget
{
public:
    LineNumberArea(CodeEditor *editor) : QWidget(editor) {
        codeEditor = editor;
    }

    QSize sizeHint() const {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) {
        codeEditor->lineNumberAreaPaintEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) {
        codeEditor->lineNumberAreaMousePressEvent(event);
    }

private:
    CodeEditor *codeEditor;
};

#endif
