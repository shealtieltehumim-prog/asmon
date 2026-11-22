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
#include "mainwindow.h"
#include "licensesdialog.h"
#include "reversepage.h"
#include <QStyle>
#include <QScreen>
#include <QInputDialog>
#include "processpicker.h"
#include "framelesshelper.h"
#include "updater.h"
#include "cardframe.h"
#include "nppthemes.h"
#include <QProgressDialog>
#include <QShortcut>
#include <QFontDatabase>
#include <QStyledItemDelegate>
#include <QFontComboBox>
#include <QStatusBar>
#include <QGuiApplication>

/**
 * @file mainwindow.cpp
 * Code for all of the windows and features.
 */

MainWindow::MainWindow(const QStringList &args, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Asmon");
    setWindowIcon(QIcon(":/icons/app.svg"));

    //! Set save and open directory
    saveOpenDirectory = settings.value("saveopendirectory", QString(Common::applicationDataPath() + "/Projects")).toString();

    //! Initial variables
    programIsBuilded = false;
    prevCodeEditor = 0;
    findDialog = 0;
    settingsWindow = 0;
    updater = nullptr;
    updateManual = false;
    updateDownloading = false;
    updateOffered = 0;
    settingsPage = nullptr;
    settingsReturnIndex = 0;
    closeFromCloseAll = false;
    settingsStartTextEditor = 0;
    settingsHighlighter = 0;
    help = 0;
    debugger = 0;
    programStopped = true; updateStatusBar();
    highlighter = 0;
    tabs = 0;
    debugToolBar = 0;
    debugDock = 0;
    debugPanels = 0;
    debugStepOutAction = 0;
    debugStepInsnAction = 0;
    explorerDock = 0;
    targetBox = 0;
    commandPalette = 0;

    //! Initialize assembler
    assembler = 0;
    recreateAssembler(true);

    //! Set code field start text
    startText = settings.value("starttext", QString()).toString();
    if (startText.isEmpty()) {
        settings.setValue("starttext", assembler->getStartText());
        startText = assembler->getStartText();
    }

    timer = new QTimer;
    timer->setInterval(100);
    timer->stop();
    connect(timer, SIGNAL(timeout()), this, SLOT(testStopOfProgram()));
    runProcess = new QProcess();

    initUi();
    createActions();
    createMenus();
    createToolBars();
    createAsmonUi();
    createEditCommands();

    //! quiet update check a few seconds after start (builds made by CI only; "autoupdate=false" in the ini turns it off)
    if (Updater::currentBuild() > 0 && settings.value("autoupdate", true).toBool())
        QTimer::singleShot(8000, this, [this]() { checkForUpdates(false); });
    applyShortcutOverrides();
    refreshEditMenu();
    setAcceptDrops(true);

    //! Restore log splitter state
    splitter->restoreState(settings.value("panelsplitterstate").toByteArray());

    //! Open documents from command line
    for (int i = 1; i < args.size(); i++) {
        if (args[i] == "--asmon-restart") {
            restorePrevSession(true);
            continue;
        }
        if (args[i] == "--licenses") { // opens the "Licenses and credits" screen (used by the tests)
            QTimer::singleShot(0, this, [this]() { LicensesDialog(this).exec(); });
            continue;
        }
        if (args[i] == "--re" && i + 1 < args.size()) {
            openProgramInAsmon(args[++i]);
            continue;
        }
        //! "--mode write|debug|re": the view to show (the screenshot tests use it)
        if (args[i] == "--mode" && i + 1 < args.size()) {
            const QString m = args[++i].toLower();
            QTimer::singleShot(300, this, [this, m]() {
                modeBar->setMode(m == "debug" ? ModeBar::Debug : m == "re" ? ModeBar::Reverse : ModeBar::Write);
            });
            continue;
        }
        //! "--window WIDTHxHEIGHT": a normal (not maximized) window of that size at the top left
        if (args[i] == "--window" && i + 1 < args.size()) {
            const QStringList wh = args[++i].toLower().split('x');
            if (wh.size() == 2) {
                const int w = wh[0].toInt(), h = wh[1].toInt();
                QTimer::singleShot(200, this, [this, w, h]() {
                    showNormal();
                    resize(w, h);
                    move(0, 0);
                });
            }
            continue;
        }
        //! "--fold-all": fold every region of the opened file (screenshot tests)
        if (args[i] == "--fold-all") {
            QTimer::singleShot(600, this, [this]() {
                if (tabs->count() > 0)
                    ((Tab *) tabs->currentWidget())->code->foldAll();
            });
            continue;
        }
        openFile(args[i]);
    }
}

void MainWindow::enableOrDisableLinkingEdit(int disableLinkingCheckboxState)
{
    bool enabled = (disableLinkingCheckboxState == Qt::Unchecked);
    settingsUi.linkingOptionsEdit->setEnabled(enabled);
    settingsUi.linkerPathEdit->setEnabled(enabled);
}

void MainWindow::initUi()
{
    //! Resize
    settings.beginGroup("MainWindow");
    {
        //! Asmon: fit the screen. First run: 85% of the screen, centred. A saved
        //! position on a monitor that is no longer there (or a window larger
        //! than this screen) is brought back inside.
        QScreen *screen = QGuiApplication::screenAt(settings.value("pos", QPoint(0, 0)).toPoint());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
        QSize size = settings.value("size", QSize(avail.width() * 85 / 100, avail.height() * 85 / 100)).toSize();
        size = size.boundedTo(avail.size());
        QPoint pos = settings.contains("pos") ? settings.value("pos").toPoint()
                                              : avail.center() - QPoint(size.width() / 2, size.height() / 2);
        if (!avail.contains(QRect(pos, size)))
            pos = QPoint(qBound(avail.left(), pos.x(), avail.right() - size.width() + 1),
                         qBound(avail.top(), pos.y(), avail.bottom() - size.height() + 1));
        resize(size);
        move(pos);
    }
    //! Set maximized
    setWindowState(windowState() | (Qt::WindowState) settings.value("maximized", (int) Qt::WindowNoState).toInt());
    settings.endGroup();

    //! Get Started window
    getStartedWidget = new GetStartedWidget;
    connect(getStartedWidget->newButton, SIGNAL(clicked()), this, SLOT(newFile()));
    connect(getStartedWidget->openButton, SIGNAL(clicked()), this, SLOT(openFile()));
    connect(getStartedWidget, &GetStartedWidget::openRecent, this, [this](const QString &p) { openFile(p); });
    QTimer::singleShot(0, this, &MainWindow::refreshRecentFiles);
    connect(getStartedWidget->prevSessionButton, SIGNAL(clicked()), this, SLOT(restorePrevSession()));

    //! Create form
    splitter = new QSplitter;
    splitter->setOrientation(Qt::Vertical);
    workLayout = new QVBoxLayout;
    //! Asmon: the mode row (Write | Debug | Reverse engineering), then the page of the mode:
    //! page 0 is the editor with its panels (Write and Debug), page 1 is the reverse-engineering page.
    modeBar = new ModeBar;   // its buttons are added in createToolBars and createAsmonUi
    modeStack = new QStackedWidget;
    modeStack->addWidget(splitter);
    reversePage = new ReversePage;
    modeStack->addWidget(reversePage);
    // a hidden page must not dictate the window's minimum width (the RE page asks for ~670 px, which
    // together with the debugger panel made the Debug mode wider than a small screen)
    reversePage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    workLayout->addWidget(modeBar);
    workLayout->addWidget(modeStack, 1);
    connect(modeBar, &ModeBar::modeChanged, this, &MainWindow::onModeChanged);
    workLayout->setContentsMargins(0, 0, 0, 0);   // flat panels edge to edge, like VS Code
    workLayout->setSpacing(0);
    splitter->setHandleWidth(1);                  // a 1 px separator between the editor and the panel
    workWidget = new QWidget;
    workWidget->setLayout(workLayout);

    //! Stacked widget
    mainWidget = new QStackedWidget;
    mainWidget->addWidget(getStartedWidget);
    mainWidget->addWidget(workWidget);
    mainWidget->setCurrentIndex(0); //get started
    setCentralWidget(mainWidget);

    //! Create highlighter
    highlighter = new Highlighter(assembler);

    //! Create tabs
    tabs = new QTabWidget;
    connect(tabs, SIGNAL(tabCloseRequested(int)), this, SLOT(deleteTab(int)));
    connect(tabs, SIGNAL(currentChanged(int)), this, SLOT(changeCurrentTab(int)));
    tabs->setTabsClosable(true);
    tabs->setMovable(true);

    //! Create compiler field
    compilerOut = new RuQTextEdit;
    compilerOut->setReadOnly(true);
    compilerOut->setMinimumHeight(100);
    compilerOut->setFont(Theme::codeFont(settings.value("fontsize", 11).toInt() - 1));
    compilerOut->setText(tr("Build log:") + '\n');
    compilerOut->viewport()->installEventFilter(this);

    //! Create gdb command widget
    debugAnyCommandWidget = new DebugAnyCommandWidget;

    //! Asmon: bottom panel: Output | Input | Build log | Build output | Debug console | Terminal. Ctrl+J shows/hides it.
    bottomPanel = new QTabWidget;
    bottomPanel->setObjectName("bottomPanel");
    bottomPanel->setDocumentMode(true);
    debugConsole = new RuQTextEdit;
    debugConsole->setReadOnly(true);
    debugConsole->setLayoutDirection(Qt::LeftToRight);
    debugConsole->setPlaceholderText(tr("gdb commands and their output (while debugging)"));
    QWidget *debugConsolePage = new QWidget;
    QVBoxLayout *debugConsoleLayout = new QVBoxLayout(debugConsolePage);
    debugConsoleLayout->setContentsMargins(0, 0, 0, 0);
    debugConsoleLayout->setSpacing(0);
    debugConsoleLayout->addWidget(debugConsole, 1);
    debugConsoleLayout->addWidget(debugAnyCommandWidget);
    terminal = new TerminalWidget;
    outputStack = new QStackedWidget;
    inputStack = new QStackedWidget;
    bottomPanel->addTab(outputStack, tr("Output"));
    bottomPanel->addTab(inputStack, tr("Input"));
    bottomPanel->addTab(compilerOut, tr("Build log"));
    searchPanel = new SearchPanel;
    bottomPanel->addTab(searchPanel, tr("Search"));
    connect(searchPanel, &SearchPanel::openLocation, this, &MainWindow::jumpToLocation);
    bottomPanel->addTab(debugConsolePage, tr("Debug console"));
    bottomPanel->addTab(terminal, tr("Terminal"));
    bottomPanel->setCurrentIndex(0);
    {
        //! corner: clear the current page, hide the panel (Ctrl+J shows it again)
        QWidget *corner = new QWidget;
        QHBoxLayout *cl = new QHBoxLayout(corner);
        cl->setContentsMargins(0, 0, 6, 0);
        cl->setSpacing(2);
        QToolButton *clearButton = new QToolButton;
        clearButton->setIcon(Theme::icon("clear"));
        clearButton->setProperty("asmonIcon", "clear");
        clearButton->setToolTip(tr("Clear"));
        clearButton->setAutoRaise(true);
        QToolButton *hideButton = new QToolButton;
        hideButton->setIcon(Theme::icon("chevron"));
        hideButton->setProperty("asmonIcon", "chevron");
        hideButton->setToolTip(tr("Hide the panel (Ctrl+J)"));
        hideButton->setAutoRaise(true);
        cl->addWidget(clearButton);
        cl->addWidget(hideButton);
        bottomPanel->setCornerWidget(corner, Qt::TopLeftCorner);
        connect(clearButton, &QToolButton::clicked, this, [this]() {
            QWidget *w = bottomPanel->currentWidget();
            if (w == compilerOut)
                compilerOut->clear();
            else if (w == outputStack && tabs->count() > 0)
                ((Tab *) tabs->currentWidget())->clearOutput();
            else if (w == inputStack && tabs->count() > 0)
                ((Tab *) tabs->currentWidget())->inputEdit()->clear();
            else if (w == terminal)
                terminal->clearView();
            else if (w == debugConsole->parentWidget())
                debugConsole->clear();
        });
        connect(hideButton, &QToolButton::clicked, this, [this]() {
            if (togglePanelAction)
                togglePanelAction->setChecked(false);
            else
                bottomCard->hide();
        });
    }
    connect(bottomPanel, &QTabWidget::currentChanged, this, [this](int i) {
        if (bottomPanel->widget(i) == terminal) {
            syncBottomPanelWithTab();
            terminal->focusInput();
        }
    });

    //! Add widgets on splitter
    splitter->addWidget(new CardFrame(tabs));
    bottomCard = new CardFrame(bottomPanel);
    splitter->addWidget(bottomCard);
    // Small windows / large display scaling: some pages (build artifacts, search) ask for 130-150 px,
    // which kept this panel at ~190 px and left the editor 4 lines. The panel may shrink to its tab bar
    // and a few lines, and 240 px of width (build artifacts asked for 468: the debugger mode then
    // widened the window past a small screen).
    bottomCard->setMinimumSize(240, 110);
    splitter->setCollapsible(0, false);
    debugAnyCommandWidget->close();
    debugAnyCommandWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    splitter->setStretchFactor(0, 3);  // code gets ~3/4 of the height by default
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 3000 << 1000);
    // setSizes() before the window is shown is overridden by the size hints (the bottom panel won ~190 px
    // of a small window). Apply the 3:1 split once the window has its real size, unless a saved one exists.
    if (!settings.contains("panelsplitterstate"))
        QTimer::singleShot(0, this, [this]() {
            const int total = splitter->height();
            splitter->setSizes(QList<int>() << total * 3 / 4 << total - total * 3 / 4);
        });
    //! Restore previous session if it in settings
    if (settings.value("startwindow", 0).toInt() == 1)
        restorePrevSession(true);
}

void MainWindow::createMenus()
{
    fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->addAction(newAction);
    fileMenu->addSeparator();
    fileMenu->addAction(openAction);
    fileMenu->addAction(closeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    fileMenu->addAction(saveExeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);
    editMenu = menuBar()->addMenu(tr("Edit"));
    editMenu->addAction(findAction);
    editMenu->addSeparator();
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editMenu->addAction(cutAction);
    editMenu->addAction(copyAction);
    editMenu->addAction(pasteAction);
    editMenu->addAction(deleteAction);
    editMenu->addSeparator();
    editMenu->addAction(selectAllAction);
    editMenu->addSeparator();
    editMenu->addAction(commentAction);
    editMenu->addAction(uncommentAction);
    editMenu->addSeparator();
    editMenu->addAction(putTabAction);
    editMenu->addAction(deleteTabAction);
    connect(editMenu, SIGNAL(aboutToShow()), this, SLOT(refreshEditMenu()));
    connect(tabs, SIGNAL(currentChanged(int)), this, SLOT(refreshEditMenu()));
    viewMenu = menuBar()->addMenu(tr("View"));
    buildMenu = menuBar()->addMenu(tr("Build"));
    buildMenu->addAction(buildAction);
    buildMenu->addSeparator();
    buildMenu->addAction(runAction);
    #ifdef Q_OS_WIN32
        buildMenu->addAction(runExeAction);
    #endif
    buildMenu->addSeparator();
    buildMenu->addAction(stopAction);
    debugMenu = menuBar()->addMenu(tr("Debug"));
    debugMenu->addAction(debugAction);
    debugMenu->addSeparator();
    debugMenu->addAction(debugNextNiAction);
    debugMenu->addAction(debugNextAction);
    debugMenu->addSeparator();
    debugMenu->addAction(debugToggleBreakpointAction);
    debugMenu->addAction(debugShowRegistersAction);
    debugMenu->addAction(debugShowMemoryAction);
    debugMenu->addSeparator();
    debugMenu->addAction(stopAction);
    //! Settings is a single command: a direct menu-bar item (one click), not a menu with one entry.
    menuBar()->addAction(settingsAction);
    settingsMenu = nullptr;
    helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(helpAction);
    helpMenu->addAction(aboutAction);
    {
        QAction *licensesAction = helpMenu->addAction(tr("Licenses and credits..."));
        connect(licensesAction, &QAction::triggered, this, [this]() { LicensesDialog(this).exec(); });
    }
    helpMenu->addSeparator();
    {
        QAction *updateAction = helpMenu->addAction(tr("Check for updates..."));
        connect(updateAction, &QAction::triggered, this, [this]() { checkForUpdates(true); });
    }
}

void MainWindow::createActions()
{
    QSettings keySettings(Common::applicationDataPath() + "/keys.ini", QSettings::IniFormat);

    newAction = new QAction(Theme::icon("new"), tr("New"), this);

    newAction->setProperty("en", "New");

    newAction->setProperty("asmonIcon", "new");
    QString key = keySettings.value("new", "default").toString();
    QKeySequence stdKey(QKeySequence::New);
    if (key == "default")
        key = stdKey.toString();
    newAction->setShortcut(key);
    connect(newAction, SIGNAL(triggered()), this, SLOT(newFile()));

    openAction = new QAction(Theme::icon("open"), tr("Open"), this);

    openAction->setProperty("en", "Open");

    openAction->setProperty("asmonIcon", "open");
    key = keySettings.value("open", "default").toString();
    stdKey = QKeySequence(QKeySequence::Open);
    if (key == "default")
        key = stdKey.toString();
    openAction->setShortcut(key);
    connect(openAction, SIGNAL(triggered()), this, SLOT(openFile()));

    closeAction = new QAction(tr("Close file"), this);

    closeAction->setProperty("en", "Close file");
    key = keySettings.value("close", "default").toString();
    stdKey = QKeySequence(QKeySequence::Close);
    if (key == "default")
        key = stdKey.toString();
    closeAction->setShortcut(key);
    connect(closeAction, SIGNAL(triggered()), this, SLOT(closeFile()));

    
    saveAction = new QAction(Theme::icon("save"), tr("Save"), this);

    
    saveAction->setProperty("en", "Save");

    
    saveAction->setProperty("asmonIcon", "save");
    key = keySettings.value("save", "default").toString();
    stdKey = QKeySequence(QKeySequence::Save);
    if (key == "default")
        key = stdKey.toString();
    saveAction->setShortcut(key);
    connect(saveAction, SIGNAL(triggered()), this, SLOT(saveFile()));

    saveAsAction = new QAction(tr("Save as"), this);

    saveAsAction->setProperty("en", "Save as");
    key = keySettings.value("saveAs", "default").toString();
    stdKey = QKeySequence(QKeySequence::SaveAs);
    if (key == "default")
        key = stdKey.toString();
    saveAsAction->setShortcut(key);
    connect(saveAsAction, SIGNAL(triggered()), this, SLOT(saveAsFile()));

    saveExeAction = new QAction(tr("Save .exe"), this);

    saveExeAction->setProperty("en", "Save .exe");
    key = keySettings.value("save_exe", "default").toString();
    if (key == "default")
        key = "Ctrl+Shift+E";
    saveExeAction->setShortcut(QKeySequence(key));
    connect(saveExeAction, SIGNAL(triggered()), this, SLOT(saveExe()));

    exitAction = new QAction(tr("Exit"), this);

    exitAction->setProperty("en", "Exit");
    key = keySettings.value("exit", "default").toString();
    stdKey = QKeySequence(QString("Ctrl+Q"));
    if (key == "default")
        key = stdKey.toString();
    exitAction->setShortcut(key);
    connect(exitAction, SIGNAL(triggered()), this, SLOT(closeApp()));

    findAction = new QAction(tr("Find and replace"), this);

    findAction->setProperty("en", "Find and replace");
    key = keySettings.value("find", "default").toString();
    stdKey = QKeySequence(QKeySequence::Find);
    if (key == "default")
        key = stdKey.toString();
    findAction->setShortcut(key);
    connect(findAction, SIGNAL(triggered()), this, SLOT(find()));


    undoAction = new QAction(tr("Undo"), this);


    undoAction->setProperty("en", "Undo");
    key = keySettings.value("undo", "default").toString();
    stdKey = QKeySequence(QKeySequence::Undo);
    if (key == "default")
        key = stdKey.toString();
    undoAction->setShortcut(key);

    redoAction = new QAction(tr("Redo"), this);

    redoAction->setProperty("en", "Redo");
    key = keySettings.value("redo", "default").toString();
    stdKey = QKeySequence(QKeySequence::Redo);
    if (key == "default")
        key = stdKey.toString();
    redoAction->setShortcut(key);

    cutAction = new QAction(tr("Cut"), this);

    cutAction->setProperty("en", "Cut");
    key = keySettings.value("cut", "default").toString();
    stdKey = QKeySequence(QKeySequence::Cut);
    if (key == "default")
        key = stdKey.toString();
    cutAction->setShortcut(key);

    copyAction = new QAction(tr("Copy"), this);

    copyAction->setProperty("en", "Copy");
    key = keySettings.value("copy", "default").toString();
    stdKey = QKeySequence(QKeySequence::Copy);
    if (key == "default")
        key = stdKey.toString();
    copyAction->setShortcut(key);

    pasteAction = new QAction(tr("Paste"), this);

    pasteAction->setProperty("en", "Paste");
    key = keySettings.value("paste", "default").toString();
    stdKey = QKeySequence(QKeySequence::Paste);
    if (key == "default")
        key = stdKey.toString();
    pasteAction->setShortcut(key);

    deleteAction = new QAction(tr("Delete"), this);

    deleteAction->setProperty("en", "Delete");
    key = keySettings.value("delete", "default").toString();
    stdKey = QKeySequence(QKeySequence::Delete);
    if (key == "default")
        key = stdKey.toString();
    deleteAction->setShortcut(key);

    selectAllAction = new QAction(tr("Select all"), this);

    selectAllAction->setProperty("en", "Select all");
    key = keySettings.value("selectAll", "default").toString();
    stdKey = QKeySequence(QKeySequence::SelectAll);
    if (key == "default")
        key = stdKey.toString();
    selectAllAction->setShortcut(key);

    commentAction = new QAction(tr("Comment"), this);

    commentAction->setProperty("en", "Comment");
    key = keySettings.value("comment", "default").toString();
    stdKey = QKeySequence(QString("Shift+Ctrl+A"));
    if (key == "default")
        key = stdKey.toString();
    commentAction->setShortcut(key);

    uncommentAction = new QAction(tr("Remove comment"), this);

    uncommentAction->setProperty("en", "Remove comment");
    key = keySettings.value("uncomment", "default").toString();
    stdKey = QKeySequence(QString("Shift+Ctrl+Q"));
    if (key == "default")
        key = stdKey.toString();
    uncommentAction->setShortcut(key);

    putTabAction = new QAction(tr("Indent"), this);

    putTabAction->setProperty("en", "Indent");
    key = keySettings.value("putTab", "default").toString();
    stdKey = QKeySequence(QString("Tab"));
    if (key == "default")
        key = stdKey.toString();
    putTabAction->setShortcut(key);

    deleteTabAction = new QAction(tr("Remove indent"), this);

    deleteTabAction->setProperty("en", "Remove indent");
    key = keySettings.value("deleteTab", "default").toString();
    stdKey = QKeySequence(QString("Shift+Tab"));
    if (key == "default")
        key = stdKey.toString();
    deleteTabAction->setShortcut(key);
    //! Action in edit menu connects in refreshEditMenu function!


    buildAction = new QAction(Theme::icon("build"), tr("Build this"), this);


    buildAction->setProperty("en", "Build this");


    buildAction->setProperty("asmonIcon", "build");
    key = keySettings.value("build", "default").toString();
    stdKey = QKeySequence(QString("Ctrl+F9"));
    if (key == "default")
        key = stdKey.toString();
    buildAction->setShortcut(key);
    connect(buildAction, SIGNAL(triggered()), this, SLOT(buildProgram()));

    runAction = new QAction(Theme::icon("run"), tr("Build and run"), this);

    runAction->setProperty("en", "Build and run");

    runAction->setProperty("asmonIcon", "run");
    key = keySettings.value("run", "default").toString();
    stdKey = QKeySequence(QString("F9"));
    if (key == "default")
        key = stdKey.toString();
    runAction->setShortcut(key);
    connect(runAction, SIGNAL(triggered()), this, SLOT(runProgram()));

    runExeAction = new QAction(tr("Run in new window"), this);

    runExeAction->setProperty("en", "Run in new window");
    connect(runExeAction, SIGNAL(triggered()), this, SLOT(runExeProgram()));

    stopAction = new QAction(Theme::icon("stop"), tr("Stop"), this);

    stopAction->setProperty("en", "Stop");

    stopAction->setProperty("asmonIcon", "stop");
    stopAction->setShortcut(QKeySequence("Shift+F5"));
    connect(stopAction, SIGNAL(triggered()), this, SLOT(stopProgram()));
    //! Enable in runProgram(), disable in stop
    stopAction->setEnabled(false);

    debugAction = new QAction(Theme::icon("debug"), tr("Debug"), this);

    debugAction->setProperty("en", "Debug");

    debugAction->setProperty("asmonIcon", "debug");
    key = keySettings.value("debug", "default").toString();
    stdKey = QKeySequence(QString("F5"));
    if (key == "default")
        key = stdKey.toString();
    debugAction->setShortcut(key);
    debugKey = key;
    connect(debugAction, SIGNAL(triggered()), this, SLOT(debug()));

    debugNextNiAction = new QAction(Theme::icon("stepover"), tr("Step over"), this);

    debugNextNiAction->setProperty("en", "Step over");

    debugNextNiAction->setProperty("asmonIcon", "stepover");
    key = keySettings.value("stepOver", "default").toString();
    stdKey = QKeySequence(QString("F10"));
    if (key == "default")
        key = stdKey.toString();
    debugNextNiAction->setShortcut(key);
    connect(debugNextNiAction, SIGNAL(triggered()), this, SLOT(debugNextNi()));

    debugNextAction = new QAction(Theme::icon("stepinto"), tr("Step into"), this);

    debugNextAction->setProperty("en", "Step into");

    debugNextAction->setProperty("asmonIcon", "stepinto");
    key = keySettings.value("stepInto", "default").toString();
    stdKey = QKeySequence(QString("F11"));
    if (key == "default")
        key = stdKey.toString();
    debugNextAction->setShortcut(key);
    connect(debugNextAction, SIGNAL(triggered()), this, SLOT(debugNext()));

    debugToggleBreakpointAction = new QAction(tr("Toggle breakpoint"), this);

    debugToggleBreakpointAction->setProperty("en", "Toggle breakpoint");
    key = keySettings.value("breakpoint", "default").toString();
    stdKey = QKeySequence(QString("F8"));
    if (key == "default")
        key = stdKey.toString();
    debugToggleBreakpointAction->setShortcut(key);
    connect(debugToggleBreakpointAction, SIGNAL(triggered()), this, SLOT(debugToggleBreakpoint()));

    debugShowRegistersAction = new QAction(tr("Show registers"), this);

    debugShowRegistersAction->setProperty("en", "Show registers");
    key = keySettings.value("showRegisters", "default").toString();
    stdKey = QKeySequence(QString("Ctrl+R"));
    if (key == "default")
        key = stdKey.toString();
    debugShowRegistersAction->setShortcut(key);
    debugShowRegistersAction->setCheckable(true);
    connect(debugShowRegistersAction, SIGNAL(toggled(bool)), this, SLOT(debugShowRegisters()), Qt::QueuedConnection);

    debugShowMemoryAction = new QAction(tr("Show memory"), this);

    debugShowMemoryAction->setProperty("en", "Show memory");
    key = keySettings.value("showMemory", "default").toString();
    stdKey = QKeySequence(QString("Ctrl+M"));
    if (key == "default")
        key = stdKey.toString();
    debugShowMemoryAction->setShortcut(key);
    debugShowMemoryAction->setCheckable(true);
    connect(debugShowMemoryAction, SIGNAL(toggled(bool)), this, SLOT(debugShowMemory()), Qt::QueuedConnection);

    disableDebugActions(true);


    settingsAction = new QAction(tr("Settings"), this);


    settingsAction->setProperty("en", "Settings");
    connect(settingsAction, SIGNAL(triggered()), this, SLOT(openSettings()));

    connect(mainWidget, SIGNAL(currentChanged(int)), this, SLOT(changeActionsState(int)));
    //! Disable some actions if get started widget opened
    changeActionsState(mainWidget->currentIndex());

    helpAction = new QAction(tr("Help"), this);

    helpAction->setProperty("en", "Help");
    key = keySettings.value("help", "default").toString();
    stdKey = QKeySequence(QKeySequence::HelpContents);
    if (key == "default")
        key = stdKey.toString();
    helpAction->setShortcut(key);
    connect(helpAction, SIGNAL(triggered()), this, SLOT(openHelp()));

    aboutAction = new QAction(tr("About"), this);

    aboutAction->setProperty("en", "About");
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(openAbout()));
}

void MainWindow::createToolBars()
{
    //! Asmon: one quiet toolbar. Step buttons live in a second toolbar that is
    //! visible only while debugging. Undo/cut/copy/paste stay in the Edit menu.
    //! The toolbars sit in the menu bar's free corner: one row instead of two.
    fileToolBar = new QToolBar(tr("Main"), this);
    fileToolBar->setObjectName("MainToolBar");
    fileToolBar->setMovable(false);
    fileToolBar->setIconSize(QSize(18, 18));
    fileToolBar->addAction(newAction);
    fileToolBar->addAction(openAction);
    fileToolBar->addAction(saveAction);
    debugToolBar = fileToolBar;

    //! The buttons of the work itself live in the mode row under the menu, and change with the mode:
    //! Write: build, run, stop. Debug: debug/continue, step over, step into (step out and stop are
    //! added in createAsmonUi, where that action is created). They are always there, greyed out
    //! when they do not apply, so the row never changes size inside a mode.
    modeBar->addAction(ModeBar::Write, buildAction);
    modeBar->addAction(ModeBar::Write, runAction);
    modeBar->addAction(ModeBar::Write, stopAction);
    modeBar->addAction(ModeBar::Debug, debugAction);
    modeBar->addAction(ModeBar::Debug, debugNextNiAction);
    modeBar->addAction(ModeBar::Debug, debugNextAction);

    editToolBar = 0;
    buildToolBar = 0;

    QWidget *corner = new QWidget;
    QHBoxLayout *cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 4, 0);
    cornerLayout->setSpacing(0);
    cornerLayout->addWidget(fileToolBar);
    menuBar()->setCornerWidget(corner, Qt::TopRightCorner);

    //! Hebrew interface: window buttons (minimize, maximize, close) on the left, close at the far left.
    if (QApplication::layoutDirection() == Qt::RightToLeft) {
        FramelessHelper *fh = new FramelessHelper(this, corner);
        fh->addButtons(cornerLayout);
        fh->addDragArea(menuBar());
        fh->addDragArea(corner);
        fh->addDragArea(fileToolBar);
    }

    restoreState(settings.value("windowstate").toByteArray());
}

void MainWindow::onModeChanged(int mode)
{
    //! Write and Debug use the editor page; Debug also shows the debugger panel (empty until a session starts).
    const bool re = (mode == ModeBar::Reverse);
    reversePage->setSizePolicy(re ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                               re ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    splitter->setSizePolicy(re ? QSizePolicy::Ignored : QSizePolicy::Preferred,
                            re ? QSizePolicy::Ignored : QSizePolicy::Preferred);
    modeStack->setCurrentIndex(re ? 1 : 0);
    if (!debugDock)
        return;
    const bool show = mode == ModeBar::Debug;
    if (show == debugDock->isVisible())
        return;
    //! Showing the panel can make the window wider (the editor and the bottom panel need their
    //! minimum width): remember the width and give it back when the panel is hidden again.
    if (show)
        widthBeforeDebugDock = (isMaximized() || isFullScreen()) ? 0 : width();
    debugDock->setVisible(show);
    //! Narrow windows: the panel must leave the editor at least ~half of the window (its own preferred
    //! width is ~550 px, which left a 340 px editor in a 960 px window).
    if (show)
        QTimer::singleShot(0, this, [this]() {
            const int want = qMax(debugDock->minimumSizeHint().width(), width() - 520);
            if (debugDock->isVisible() && debugDock->width() > want && width() < 1300)
                resizeDocks({ debugDock }, { want }, Qt::Horizontal);
        });
    if (!show && widthBeforeDebugDock > 0) {
        const int w = widthBeforeDebugDock;
        widthBeforeDebugDock = 0;
        QTimer::singleShot(0, this, [this, w]() {
            if (!isMaximized() && !isFullScreen())
                resize(w, height());
        });
    }
}

void MainWindow::checkForUpdates(bool manual)
{
    if (!updater) {
        updater = new Updater(this);
        connect(updater, &Updater::upToDate, this, [this]() {
            if (updateManual)
                QMessageBox::information(this, tr("Updates"), tr("You have the newest version."));
        });
        connect(updater, &Updater::failed, this, [this](const QString &msg) {
            if (updateProgress) {
                updateProgress->close();
                updateProgress->deleteLater();
            }
            if (updateManual || updateDownloading)
                QMessageBox::warning(this, tr("Updates"), tr("Could not update:") + "\n" + msg);
            updateDownloading = false;
        });
        connect(updater, &Updater::updateAvailable, this, [this](const Updater::Info &info) {
            if (updateOffered == info.build && !updateManual)
                return; // asked once per session
            updateOffered = info.build;
            QString text = tr("A new version of Asmon is available (build %1).").arg(info.build);
            if (!info.notes.isEmpty())
                text += "\n\n" + info.notes;
            if (!Updater::canInstall()) {
                QMessageBox::information(this, tr("Updates"), text);
                return;
            }
            text += "\n\n" + tr("Asmon will close and open again after the update. Update now?");
            QMessageBox box(QMessageBox::Question, tr("Updates"), text, QMessageBox::NoButton, this);
            QPushButton *yes = box.addButton(tr("Update now"), QMessageBox::AcceptRole);
            box.addButton(tr("Later"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() != yes)
                return;
            updateDownloading = true;
            updateProgress = new QProgressDialog(tr("Downloading the update..."), QString(), 0, 0, this);
            updateProgress->setWindowModality(Qt::WindowModal);
            updateProgress->setMinimumDuration(0);
            updateProgress->setCancelButton(nullptr);
            updateProgress->show();
            updater->download(info);
        });
        connect(updater, &Updater::downloaded, this, [this](const QString &path, const Updater::Info &) {
            updateDownloading = false;
            if (updateProgress) {
                updateProgress->close();
                updateProgress->deleteLater();
            }
            //! unsaved files are asked about first; cancelling stops the update
            if (!closeApp())
                return;
            QString err;
            if (!Updater::installAndRestart(path, &err)) {
                QMessageBox::warning(this, tr("Updates"), err);
                return;
            }
            qApp->quit();
        });
    }
    updateManual = manual;
    updater->check();
}

void MainWindow::createAsmonUi()
{
    //! Command palette (Ctrl+Shift+P)
    commandPalette = new CommandPalette(this);
    commandPaletteAction = new QAction(Theme::icon("palette"), tr("Command palette..."), this);
    commandPaletteAction->setProperty("en", "Command palette...");
    commandPaletteAction->setProperty("asmonIcon", "palette");
    commandPaletteAction->setShortcut(QKeySequence("Ctrl+Shift+P"));
    commandPaletteAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(commandPaletteAction, SIGNAL(triggered()), this, SLOT(showCommandPalette()));
    addAction(commandPaletteAction);

    //! File explorer
    explorerModel = new QFileSystemModel(this);
    explorerModel->setNameFilters(QStringList() << "*.asm" << "*.inc" << "*.s" << "*.S" << "*.nasm"
                                                << "*.lst" << "*.txt" << "*.md");
    explorerModel->setNameFilterDisables(false);
    explorerView = new QTreeView;
    explorerView->setModel(explorerModel);
    for (int c = 1; c < explorerModel->columnCount(); ++c)
        explorerView->hideColumn(c);
    explorerView->setHeaderHidden(true);
    explorerView->setFrameShape(QFrame::NoFrame);
    explorerView->setIndentation(14);
    connect(explorerView, SIGNAL(activated(QModelIndex)), this, SLOT(openFromExplorer(QModelIndex)));
    explorerDock = new QDockWidget(tr("Explorer"), this);
    explorerDock->setObjectName("explorerDock");
    explorerDock->setWidget(explorerView);
    explorerDock->setFeatures(QDockWidget::DockWidgetClosable);
    explorerDock->setMinimumWidth(180);
    addDockWidget(layoutDirection() == Qt::RightToLeft ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea,
                  explorerDock);
    toggleExplorerAction = explorerDock->toggleViewAction();
    toggleExplorerAction->setText(tr("Explorer"));
    toggleExplorerAction->setIcon(Theme::icon("explorer"));
    toggleExplorerAction->setProperty("asmonIcon", "explorer");
    toggleExplorerAction->setShortcut(QKeySequence("Ctrl+B"));
    explorerDock->setVisible(settings.value("explorervisible", false).toBool());
    connect(explorerDock, SIGNAL(visibilityChanged(bool)), this, SLOT(toggleExplorer(bool)));
    connect(tabs, SIGNAL(currentChanged(int)), this, SLOT(updateExplorerRoot()));
    updateExplorerRoot();

    //! Focus mode: only the editor stays visible
    focusModeAction = new QAction(tr("Focus mode"), this);
    focusModeAction->setProperty("en", "Focus mode");
    focusModeAction->setCheckable(true);
    focusModeAction->setShortcuts({ QKeySequence("Alt+Z"), QKeySequence("Ctrl+K, Z") });
    focusModeAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(focusModeAction, SIGNAL(toggled(bool)), this, SLOT(toggleFocusMode(bool)));
    addAction(focusModeAction);

    //! Theme
    darkThemeAction = new QAction(tr("Dark theme"), this);
    darkThemeAction->setProperty("en", "Dark theme");
    darkThemeAction->setCheckable(true);
    darkThemeAction->setChecked(Theme::isDark());
    connect(darkThemeAction, SIGNAL(toggled(bool)), this, SLOT(switchTheme(bool)));

    //! Debug / RE panels: only visible while debugging
    debugPanels = new DebugPanels;
    connect(debugPanels, &DebugPanels::goToLine, this, [this](int id) {
        const int index = id / DebugBackend::kModuleLineBase;
        if (index == 0 && debugMainTab)
            tabs->setCurrentWidget(debugMainTab);
        jumpToLocation(index > 0 ? debugModulePaths.value(index - 1) : QString(),
                       id % DebugBackend::kModuleLineBase, 0);
    });
    connect(debugPanels, &DebugPanels::message, this, [this](const QString &t) {
        printLogWithTime(t + '\n', Theme::color("fontcolor"));
        statusBar()->showMessage(t, 5000);
    });
    debugPanels->lineName = [this](int id) -> QString {
        const int index = id / DebugBackend::kModuleLineBase;
        const int line = id % DebugBackend::kModuleLineBase;
        if (index == 0)
            return QString::number(line);
        return QString("%1:%2").arg(QFileInfo(debugModulePaths.value(index - 1)).fileName()).arg(line);
    };
    debugPanels->labelForLine = [this](int id) -> QString {
        const int index = id / DebugBackend::kModuleLineBase;
        QString text;
        if (index == 0) {
            if (debugMainTab)
                text = debugMainTab->code->plainTextWithoutMarks();
        } else {
            QFile f(debugModulePaths.value(index - 1));
            if (f.open(QIODevice::ReadOnly))
                text = QString::fromUtf8(f.readAll());
        }
        return Symbols::parentLabel(text.split('\n'), id % DebugBackend::kModuleLineBase - 1);
    };
    debugDock = new QDockWidget(tr("Debugger"), this);
    debugDock->setObjectName("debugDock");
    debugDock->setWidget(debugPanels);
    //! Shown only while debugging, without a title bar: no extra chrome.
    debugDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    debugDock->setTitleBarWidget(new QWidget(debugDock));
    addDockWidget(Qt::RightDockWidgetArea, debugDock);
    debugDock->hide();

    //! Status bar (VS Code style): where the cursor is, what builds the code.
    {
        statusCursor = new QLabel;
        statusMode = new QLabel;
        statusState = new QLabel;
        statusCursor->setLayoutDirection(Qt::LeftToRight);
        statusBar()->addWidget(statusState, 1);
        statusDoc = new QLabel;
        statusDoc->setLayoutDirection(Qt::LeftToRight);
        statusBar()->addPermanentWidget(statusMode);
        statusBar()->addPermanentWidget(statusDoc);
        statusBar()->addPermanentWidget(statusCursor);
        statusBar()->setSizeGripEnabled(false);
        updateStatusBar();
    }

    debugStepOutAction = new QAction(Theme::icon("stepout"), tr("Step out"), this);
    debugStepOutAction->setProperty("asmonIcon", "stepout");
    debugStepOutAction->setProperty("en", "Step out finish");
    debugStepOutAction->setShortcut(QKeySequence("Shift+F11"));
    debugStepOutAction->setEnabled(false);
    connect(debugStepOutAction, SIGNAL(triggered()), this, SLOT(debugStepOut()));
    debugStepInsnAction = new QAction(tr("Step one instruction (stepi)"), this);
    debugStepInsnAction->setProperty("en", "Step instruction stepi");
    debugStepInsnAction->setShortcut(QKeySequence("Ctrl+F11"));
    debugStepInsnAction->setEnabled(false);
    connect(debugStepInsnAction, SIGNAL(triggered()), this, SLOT(debugStepInstruction()));
    debugMenu->insertAction(debugToggleBreakpointAction, debugStepOutAction);
    {
        QAction *attachAction = new QAction(tr("Attach to process..."), this);
        attachAction->setProperty("en", "Attach to process running pid gdb");
        connect(attachAction, &QAction::triggered, this, &MainWindow::attachToProcess);
        debugMenu->insertAction(debugMenu->actions().value(1), attachAction);
    }
    {
        QAction *condAction = new QAction(tr("Breakpoint condition..."), this);
        condAction->setProperty("en", "Conditional breakpoint condition");
        condAction->setShortcut(QKeySequence("Ctrl+Shift+F8"));
        connect(condAction, &QAction::triggered, this, &MainWindow::editBreakpointCondition);
        const QList<QAction *> acts = debugMenu->actions();
        const int idx = acts.indexOf(debugToggleBreakpointAction);
        debugMenu->insertAction(idx + 1 < acts.size() ? acts.at(idx + 1) : nullptr, condAction);
    }
    debugMenu->insertAction(debugToggleBreakpointAction, debugStepInsnAction);
    modeBar->addAction(ModeBar::Debug, debugStepOutAction);
    modeBar->addAction(ModeBar::Debug, stopAction);

    //! RE menu: open the program in external reverse-engineering tools
    {
        QMenu *reMenu = new QMenu(tr("RE"), this);
        menuBar()->insertMenu(settingsAction, reMenu);
        //! the Reverse engineering mode offers the same tools from its "Open file" button
        QToolButton *reOpenButton = new QToolButton;
        reOpenButton->setObjectName("modeAction");
        reOpenButton->setText(tr("Open file"));
        reOpenButton->setIcon(Theme::icon("open"));
        reOpenButton->setProperty("asmonIcon", "open");
        reOpenButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        reOpenButton->setIconSize(QSize(16, 16));
        reOpenButton->setPopupMode(QToolButton::InstantPopup);
        reOpenButton->setFocusPolicy(Qt::NoFocus);
        reOpenButton->setMenu(reMenu);
        modeBar->addWidget(ModeBar::Reverse, reOpenButton);
        QToolButton *saveCopyButton = new QToolButton;
        saveCopyButton->setObjectName("modeAction");
        saveCopyButton->setText(tr("Save a patched copy"));
        saveCopyButton->setIcon(Theme::icon("save"));
        saveCopyButton->setProperty("asmonIcon", "save");
        saveCopyButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        saveCopyButton->setIconSize(QSize(16, 16));
        saveCopyButton->setFocusPolicy(Qt::NoFocus);
        saveCopyButton->setEnabled(false);
        modeBar->addWidget(ModeBar::Reverse, saveCopyButton);
        connect(saveCopyButton, &QToolButton::clicked, this, [this]() { reversePage->saveCopy(); });
        connect(reversePage, &ReversePage::stateChanged, this, [this, saveCopyButton]() {
            saveCopyButton->setEnabled(reversePage->hasPatches());
        });
        connect(reversePage, &ReversePage::message, this, [this](const QString &text) {
            statusBar()->showMessage(text, 8000);
        });
        QAction *inAsmon = reMenu->addAction(tr("Open a program here..."));
        inAsmon->setProperty("en", "RE open program in Asmon disassembly");
        connect(inAsmon, &QAction::triggered, this, [this]() { chooseProgramForAsmon(); });
        reMenu->addSeparator();
        for (const ReTools::Tool &tool : ReTools::all()) {
            const QString id = tool.id;
            QAction *current = reMenu->addAction(tr("Open the program in %1").arg(tool.name));
            current->setProperty("en", QString("RE open program in ") + tool.name);
            current->setProperty("reTool", id);
            connect(current, &QAction::triggered, this, [this, id]() { openInReTool(id, false); });
            QAction *other = reMenu->addAction(tr("Open a file in %1...").arg(tool.name));
            other->setProperty("en", QString("RE open file in ") + tool.name);
            other->setProperty("reTool", id);
            connect(other, &QAction::triggered, this, [this, id]() { openInReTool(id, true); });
            reMenu->addSeparator();
        }
        QAction *headers = reMenu->addAction(tr("Headers and sections (objdump -x)"));
        headers->setProperty("en", "objdump headers sections binutils");
        connect(headers, &QAction::triggered, this, [this]() {
            showBottomPage(buildOutputView);
            buildOutputView->showHeaders();
            refreshBuildOutput();
        });
        //! Mark tools that are not installed (checked each time the menu opens).
        connect(reMenu, &QMenu::aboutToShow, this, [this, reMenu]() {
            const bool x86 = settings.value("mode", QString("x64")).toString() == "x86";
            for (QAction *a : reMenu->actions()) {
                const QString id = a->property("reTool").toString();
                if (id.isEmpty())
                    continue;
                const bool installed = !ReTools::find(id, x86).isEmpty();
                a->setEnabled(installed);
                a->setStatusTip(installed ? QString() : tr("Not installed"));
            }
        });
    }

    //! Build output: listing / disassembly / hex of what was built
    //! (a tab of the bottom panel: one place for everything below the code)
    buildOutputView = new BuildOutputView;
    bottomPanel->insertTab(bottomPanel->indexOf(compilerOut) + 1, buildOutputView, tr("Build output"));
    QAction *buildOutputAction = new QAction(tr("Build output (listing, disassembly, hex)"), this);
    buildOutputAction->setProperty("en", "Build output listing disassembly hex objdump");
    buildOutputAction->setShortcut(QKeySequence("Ctrl+Shift+B"));
    connect(buildOutputAction, &QAction::triggered, this, [this]() {
        showBottomPage(buildOutputView);
        refreshBuildOutput();
    });
    connect(bottomPanel, &QTabWidget::currentChanged, this, [this](int i) {
        if (bottomPanel->widget(i) == buildOutputView)
            refreshBuildOutput();
    });

    #ifdef Q_OS_WIN32
    {
        //! Target: Windows exe or Linux ELF (run in WSL)
        targetBox = new QComboBox;
        targetBox->addItem(tr("Windows x64"), "windows");
        targetBox->addItem(tr("Linux ELF (WSL)"), "linux");
        targetBox->setCurrentIndex(settings.value("target", QString("windows")).toString() == "linux" ? 1 : 0);
        targetBox->setToolTip(tr("Build target"));
        connect(targetBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
            settings.setValue("target", targetBox->itemData(i).toString());
            programIsBuilded = false;
            updateStatusBar();
        });
        //! in the status bar (VS Code style), so the menu row keeps room for all menus
        targetBox->setObjectName("statusTarget");
        targetBox->setLayoutDirection(Qt::LeftToRight); // "Linux ELF (WSL)" reads left to right
        targetBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        statusBar()->insertPermanentWidget(0, targetBox);

        QAction *installWslAction = new QAction(tr("Install WSL..."), this);
        installWslAction->setProperty("en", "Install WSL Linux");
        connect(installWslAction, &QAction::triggered, this, [this]() {
            QString why;
            if (LinuxTarget::wslAvailable(&why)) {
                printLogWithTime(tr("WSL is already installed.") + '\n', Theme::color("successcolor"));
                return;
            }
            if (LinuxTarget::startWslInstall())
                printLogWithTime(tr("The WSL installer was started (it asks for administrator rights; "
                                    "a restart of Windows may be needed).") + '\n', Theme::color("fontcolor"));
            else
                printLogWithTime(tr("Could not start the WSL installer.") + '\n', Theme::color("errorcolor"));
        });
        buildMenu->addSeparator();
        buildMenu->addAction(installWslAction);
    }
    #endif

    //! File > New from template
    {
        QMenu *templatesMenu = new QMenu(tr("New from template"), this);
        struct T { const char *title; const char *file; };
        const T list[] = {
            { QT_TR_NOOP("Hello world — Windows x64 (io64.inc)"), ":/templates/hello_win64.asm" },
            { QT_TR_NOOP("Hello world — Windows x64 (Windows API, no macros)"), ":/templates/hello_win64_api.asm" },
            { QT_TR_NOOP("Hello world — Linux x64 (system calls)"), ":/templates/hello_linux64.asm" },
        };
        for (const T &t : list) {
            QAction *a = templatesMenu->addAction(tr(t.title));
            a->setProperty("en", QString("Template ") + t.title);
            const QString file = t.file;
            connect(a, &QAction::triggered, this, [this, file]() { newFromTemplate(file); });
        }
        fileMenu->insertMenu(openAction, templatesMenu);
        QAction *projectAction = new QAction(tr("Project file (several .asm files)..."), this);
        projectAction->setProperty("en", "Project file modules link multiple asm files asmon.project");
        connect(projectAction, &QAction::triggered, this, &MainWindow::createProjectFile);
        fileMenu->insertAction(openAction, projectAction);
    }

    //! Code navigation (Edit menu): go to definition / references / symbol, find in files
    {
        QAction *defAction = new QAction(tr("Go to definition"), this);
        defAction->setProperty("en", "Go to definition label F12");
        defAction->setShortcut(QKeySequence(Qt::Key_F12));
        connect(defAction, &QAction::triggered, this, &MainWindow::goToDefinition);
        QAction *refsAction = new QAction(tr("Find all uses"), this);
        refsAction->setProperty("en", "Find references uses");
        refsAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F12));
        connect(refsAction, &QAction::triggered, this, &MainWindow::findReferences);
        QAction *symAction = new QAction(tr("Go to label..."), this);
        symAction->setProperty("en", "Go to symbol label outline");
        symAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
        connect(symAction, &QAction::triggered, this, &MainWindow::goToSymbol);
        QAction *lineAction = new QAction(tr("Go to line..."), this);
        lineAction->setProperty("en", "Go to line number");
        lineAction->setShortcut(QKeySequence("Ctrl+G"));
        connect(lineAction, &QAction::triggered, this, &MainWindow::goToLine);
        QAction *filesAction = new QAction(tr("Find in files..."), this);
        filesAction->setProperty("en", "Find in files search project folder");
        filesAction->setShortcut(QKeySequence("Ctrl+Shift+F"));
        connect(filesAction, &QAction::triggered, this, &MainWindow::findInFiles);
        editMenu->addSeparator();
        for (QAction *a : { defAction, refsAction, symAction, lineAction, filesAction })
            editMenu->addAction(a);
    }

    connect(tabs, SIGNAL(currentChanged(int)), this, SLOT(syncBottomPanelWithTab()));
    togglePanelAction = new QAction(tr("Bottom panel"), this);
    togglePanelAction->setProperty("en", "Toggle bottom panel output terminal");
    togglePanelAction->setShortcut(QKeySequence("Ctrl+J"));
    togglePanelAction->setCheckable(true);
    togglePanelAction->setChecked(true);
    connect(togglePanelAction, &QAction::toggled, bottomCard, &QWidget::setVisible);
    QAction *terminalAction = new QAction(tr("Terminal"), this);
    terminalAction->setProperty("en", "Terminal shell cmd bash");
    terminalAction->setShortcut(QKeySequence("Ctrl+`"));
    connect(terminalAction, &QAction::triggered, this, [this]() {
        togglePanelAction->setChecked(true);
        syncBottomPanelWithTab();
        showBottomPage(terminal);
        terminal->focusInput();
    });

    viewMenu->addAction(commandPaletteAction);
    viewMenu->addSeparator();
    viewMenu->addAction(togglePanelAction);
    viewMenu->addAction(terminalAction);
    viewMenu->addAction(toggleExplorerAction);
    viewMenu->addAction(buildOutputAction);
    viewMenu->addAction(focusModeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(darkThemeAction);

    //! Language: Hebrew <-> English (needs a restart; tabs are restored)
    const bool hebrew = settings.value("language", 7).toInt() == 7;
    QAction *languageAction = new QAction(hebrew ? QString("Switch to English (restart)")
                                                 : QString::fromUtf8("החלף לעברית (הפעלה מחדש)"), this);
    languageAction->setProperty("en", "Language English Hebrew");
    connect(languageAction, &QAction::triggered, this, [this, hebrew]() {
        settings.setValue("language", hebrew ? 1 : 7);
        restartApplication();
    });
    viewMenu->addAction(languageAction);
}

void MainWindow::restartApplication()
{
    //! close() runs closeEvent: unsaved files are asked about, the session is saved.
    if (!close())
        return;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList() << "--asmon-restart");
    qApp->quit();
}

void MainWindow::refreshBuildOutput()
{
    if (!buildOutputView || !bottomPanel || !bottomPanel->isVisible()
            || bottomPanel->currentWidget() != buildOutputView)
        return;
    QString objdump = "objdump";
    #ifdef Q_OS_WIN32
        objdump = Common::applicationDataPath()
            + (settings.value("mode", QString("x64")).toString() == "x86" ? "/MinGW/bin/objdump.exe"
                                                                           : "/MinGW64/bin/objdump.exe");
        if (linuxTargetSelected()) // MinGW objdump only knows PE/COFF
            objdump = Common::applicationDataPath() + "/LLVM/bin/llvm-objdump.exe";
    #endif
    buildOutputView->setPaths(Common::pathInTemp("program.lst"),
                              Common::pathInTemp(settings.value("objectfilename", "program.o").toString()),
                              Common::pathInTemp("AsmonProg.exe"), objdump);
    buildOutputView->refresh();
}

QString MainWindow::prettifyBuildLog(const QString &text)
{
    //! Asmon: show the user's file name instead of the temporary copy, so
    //! "/tmp/Asmon/program.asm:15: error: ..." becomes "hello.asm:15: error: ...".
    QString name = tr("untitled.asm");
    if (tabs && tabs->count() > 0) {
        const QString path = ((Tab *) tabs->currentWidget())->getCurrentFilePath();
        if (!path.isEmpty())
            name = QFileInfo(path).fileName();
    }
    QString out = text;
    const QString temp = Common::pathInTemp("program.asm");
    out.replace(temp, name);
    out.replace(QDir::fromNativeSeparators(temp), name);
    const QString tempDir = Common::pathInTemp(QString());
    out.replace(tempDir + QDir::separator(), QString());
    out.replace(QDir::fromNativeSeparators(tempDir) + "/", QString());
    for (auto it = lastModulePaths.constBegin(); it != lastModulePaths.constEnd(); ++it) {
        out.replace(QDir::toNativeSeparators(it.value()), it.key());
        out.replace(it.value(), it.key());
    }
    return out;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    //! Double click on "file.asm:15: error: ..." in the build log jumps to line 15.
    if (compilerOut && watched == compilerOut->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        const QPoint pos = static_cast<QMouseEvent *>(event)->position().toPoint();
        const QString line = compilerOut->cursorForPosition(pos).block().text();
        static const QRegularExpression re(":(\\d+):\\s*(?:\\d+:\\s*)?(?:error|warning|fatal)",
                                           QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch() && tabs && tabs->count() > 0) {
            //! an error in another module of the project: open that file
            const QString fileName = line.left(m.capturedStart()).trimmed().section(' ', -1);
            const QString current = QFileInfo(((Tab *) tabs->currentWidget())->getCurrentFilePath()).fileName();
            if (fileName != current && lastModulePaths.contains(fileName))
                openFile(lastModulePaths.value(fileName));
            CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
            const QTextBlock block = code->document()->findBlockByNumber(m.captured(1).toInt() - 1);
            if (block.isValid()) {
                QTextCursor c(block);
                code->setTextCursor(c);
                code->centerCursor();
                code->setFocus();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::newFromTemplate(const QString &resource)
{
    QFile f(resource);
    if (!f.open(QIODevice::ReadOnly))
        return;
    newFile();
    Tab *tab = (Tab *) tabs->currentWidget();
    tab->code->setPlainText(QString::fromUtf8(f.readAll()));
}

void MainWindow::openInReTool(const QString &id, bool pickFile)
{
    QString file;
    if (pickFile) {
        file = QFileDialog::getOpenFileName(this, tr("Open a binary"), saveOpenDirectory,
                                            tr("Programs and objects (*.exe *.dll *.o *.obj *.elf *.bin *.so);;All files (*)"));
        if (file.isEmpty())
            return;
    } else {
        if (!programIsBuilded)
            buildProgram(false);
        if (!programIsBuilded)
            return;
        //! Copy the program next to its source with a stable name, so the
        //! tool's project/analysis keeps pointing at a real file.
        const QString built = Common::pathInTemp("AsmonProg.exe");
        file = built;
        if (tabs && tabs->count() > 0) {
            const QString src = ((Tab *) tabs->currentWidget())->getCurrentFilePath();
            if (!src.isEmpty()) {
                QFileInfo fi(src);
                QString dest = fi.absolutePath() + "/" + fi.completeBaseName();
                #ifdef Q_OS_WIN32
                if (!linuxTargetSelected())
                    dest += ".exe";
                #endif
                QFile::remove(dest);
                if (QFile::copy(built, dest))
                    file = dest;
            }
        }
    }
    QString error;
    const bool x86 = settings.value("mode", QString("x64")).toString() == "x86";
    if (!ReTools::launch(id, file, x86, &error)) {
        printLogWithTime(error + '\n', Theme::color("errorcolor"));
        return;
    }
    if (id == "ghidra") {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(file));
        printLogWithTime(tr("Ghidra is starting. The program's path is on the clipboard: in Ghidra choose "
                            "File > Import File (I) and paste it.") + '\n', Theme::color("fontcolor"));
    } else {
        printLogWithTime(tr("Opening %1 in %2...").arg(QDir::toNativeSeparators(file), id) + '\n',
                         Theme::color("fontcolor"));
    }
}

bool MainWindow::linuxTargetSelected() const
{
    #ifdef Q_OS_WIN32
        return settings.value("target", QString("windows")).toString() == "linux";
    #else
        return false; // on Linux the native target already is Linux
    #endif
}

void MainWindow::buildForLinuxTarget(Tab *currentTab)
{
    if (settings.value("mode", QString("x64")).toString() == "x86") {
        printLogWithTime(tr("The Linux target supports x64 only. Switch to x64 mode in Settings.") + '\n',
                         Theme::color("errorcolor"));
        return;
    }
    const QString dataPath = Common::applicationDataPath();
    QString sourceDir;
    if (!currentTab->getCurrentFilePath().isEmpty())
        sourceDir = QFileInfo(currentTab->getCurrentFilePath()).absolutePath();
    LinuxTarget::BuildResult r = LinuxTarget::build(
        dataPath + "/NASM/nasm.exe", dataPath + "/LLVM/bin/ld.lld.exe",
        Common::pathInTemp("program.asm"), Common::pathInTemp("program.lst"),
        Common::pathInTemp(settings.value("objectfilename", "program.o").toString()),
        Common::pathInTemp("AsmonProg.exe"), dataPath + "/include", sourceDir,
        projectModules(currentTab));
    if (!r.ok) {
        printLogWithTime(tr("Warning! Errors have occurred in the build:") + '\n', Theme::color("errorcolor"));
        showBottomPage(compilerOut);
        printLog(prettifyBuildLog(r.log), Theme::color("errorcolor"));
        markBuildDiagnostics(prettifyBuildLog(r.log));
        return;
    }
    printLogWithTime(tr("Built successfully (Linux ELF x64).") + '\n', Theme::color("successcolor"));
    printLog(prettifyBuildLog(r.log), Theme::color("errorcolor"));
    markBuildDiagnostics(prettifyBuildLog(r.log));
    refreshBuildOutput();
    programIsBuilded = true;
}

// ---------------------------------------------------------------- shortcuts

static void collectActions(QMenu *menu, QList<QAction *> &out)
{
    for (QAction *a : menu->actions()) {
        if (a->menu())
            collectActions(a->menu(), out);
        else if (!a->isSeparator() && !out.contains(a))
            out << a;
    }
}

QList<QAction *> MainWindow::shortcutActions() const
{
    //! Every menu command that has a stable id (its English name).
    QList<QAction *> all, out;
    for (QAction *a : menuBar()->actions())
        if (a->menu())
            collectActions(a->menu(), all);
    for (QAction *a : all)
        if (!shortcutId(a).isEmpty())
            out << a;
    return out;
}

QString MainWindow::shortcutId(QAction *a)
{
    QString id = a->property("en").toString();
    if (id.isEmpty())
        id = a->objectName();
    return id.simplified().replace('/', '_').replace('\\', '_');
}

void MainWindow::applyShortcutOverrides()
{
    //! [shortcuts] in Asmon.ini: "id=Ctrl+Alt+B" (empty value = no shortcut).
    settings.beginGroup("shortcuts");
    for (QAction *a : shortcutActions()) {
        if (!a->property("asmonDefaultShortcut").isValid())
            a->setProperty("asmonDefaultShortcut", a->shortcut().toString(QKeySequence::PortableText));
        const QString id = shortcutId(a);
        if (settings.contains(id))
            a->setShortcut(QKeySequence::fromString(settings.value(id).toString(), QKeySequence::PortableText));
    }
    settings.endGroup();
}

void MainWindow::createShortcutsPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(page);
    QLineEdit *filter = new QLineEdit;
    filter->setPlaceholderText(tr("Search a command..."));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);

    shortcutsTable = new QTableWidget(0, 2);
    shortcutsTable->setHorizontalHeaderLabels(QStringList() << tr("Command") << tr("Shortcut"));
    shortcutsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    shortcutsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    shortcutsTable->setColumnWidth(1, 220);
    shortcutsTable->verticalHeader()->hide();
    shortcutsTable->setSelectionMode(QAbstractItemView::NoSelection);
    shortcutsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(shortcutsTable, 1);

    shortcutsConflict = new QLabel;
    shortcutsConflict->setWordWrap(true);
    shortcutsConflict->setStyleSheet(QString("color: %1;").arg(Theme::color("errorcolor").name()));
    QPushButton *defaults = new QPushButton(tr("Restore default shortcuts"));
    QHBoxLayout *bottom = new QHBoxLayout;
    bottom->addWidget(shortcutsConflict, 1);
    bottom->addWidget(defaults);
    layout->addLayout(bottom);
    QLabel *hint = new QLabel(tr("Click a shortcut and press the new keys. Saved in the [shortcuts] "
                                 "section of the settings file."));
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(hint);

    connect(filter, &QLineEdit::textChanged, this, [this](const QString &t) {
        for (int r = 0; r < shortcutsTable->rowCount(); ++r) {
            const QTableWidgetItem *it = shortcutsTable->item(r, 0);
            shortcutsTable->setRowHidden(r, !t.isEmpty()
                && !it->text().contains(t, Qt::CaseInsensitive)
                && !it->data(Qt::UserRole + 1).toString().contains(t, Qt::CaseInsensitive));
        }
    });
    connect(defaults, &QPushButton::clicked, this, [this]() {
        for (int r = 0; r < shortcutsTable->rowCount(); ++r) {
            QAction *a = shortcutsTable->item(r, 0)->data(Qt::UserRole).value<QAction *>();
            auto *edit = static_cast<QKeySequenceEdit *>(shortcutsTable->cellWidget(r, 1));
            edit->setKeySequence(QKeySequence::fromString(a->property("asmonDefaultShortcut").toString(),
                                                          QKeySequence::PortableText));
        }
    });
    settingsUi.tabWidget->addTab(page, tr("Shortcuts"));
}

void MainWindow::fillShortcutsPage()
{
    shortcutsTable->setRowCount(0);
    for (QAction *a : shortcutActions()) {
        const int r = shortcutsTable->rowCount();
        shortcutsTable->insertRow(r);
        QString name = a->text();
        name.remove('&');
        //! a name that starts in English keeps its own order in the RTL table
        if (!name.isEmpty() && name.at(0).direction() == QChar::DirL
                && QApplication::layoutDirection() == Qt::RightToLeft)
            name = QChar(0x202A) + name + QChar(0x202C);
        QWidget *w = a->parentWidget();
        auto *item = new QTableWidgetItem(a->icon(), name);
        item->setData(Qt::UserRole, QVariant::fromValue(a));
        item->setData(Qt::UserRole + 1, shortcutId(a));
        item->setToolTip(shortcutId(a));
        Q_UNUSED(w);
        shortcutsTable->setItem(r, 0, item);
        auto *edit = new QKeySequenceEdit(a->shortcut());
        edit->setLayoutDirection(Qt::LeftToRight);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        edit->setClearButtonEnabled(true);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        edit->setMaximumSequenceLength(2);
#endif
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, &MainWindow::checkShortcutConflicts);
        shortcutsTable->setCellWidget(r, 1, edit);
    }
    checkShortcutConflicts();
}

void MainWindow::checkShortcutConflicts()
{
    QHash<QString, QStringList> byKey;
    for (int r = 0; r < shortcutsTable->rowCount(); ++r) {
        auto *edit = static_cast<QKeySequenceEdit *>(shortcutsTable->cellWidget(r, 1));
        const QString k = edit->keySequence().toString(QKeySequence::NativeText);
        if (!k.isEmpty())
            byKey[k] << shortcutsTable->item(r, 0)->text();
    }
    QStringList msgs;
    for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it)
        if (it.value().size() > 1)
            msgs << QString("%1: %2").arg(it.key(), it.value().join(", "));
    shortcutsConflict->setText(msgs.isEmpty() ? QString()
                               : tr("Same shortcut for several commands: ") + msgs.join("; "));
}

void MainWindow::saveShortcuts()
{
    if (!shortcutsTable)
        return;
    settings.beginGroup("shortcuts");
    for (int r = 0; r < shortcutsTable->rowCount(); ++r) {
        QAction *a = shortcutsTable->item(r, 0)->data(Qt::UserRole).value<QAction *>();
        auto *edit = static_cast<QKeySequenceEdit *>(shortcutsTable->cellWidget(r, 1));
        const QString k = edit->keySequence().toString(QKeySequence::PortableText);
        const QString id = shortcutId(a);
        if (k == a->property("asmonDefaultShortcut").toString())
            settings.remove(id);
        else
            settings.setValue(id, k);
        a->setShortcut(edit->keySequence());
    }
    settings.endGroup();
}

//! asmon.project settings lines: "assembler-flags: -DDEBUG", "linker-flags: -lm"
static bool isProjectSettingLine(const QString &line)
{
    static const QRegularExpression re("^(assembler-flags|linker-flags)\\s*:", QRegularExpression::CaseInsensitiveOption);
    return re.match(line.trimmed()).hasMatch();
}

QStringList MainWindow::projectFlags(Tab *tab, const QString &key)
{
    const QString filePath = tab ? tab->getCurrentFilePath() : QString();
    if (filePath.isEmpty())
        return QStringList();
    QFile f(QFileInfo(filePath).absoluteDir().filePath("asmon.project"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringList();
    QStringList out;
    for (QString l : QString::fromUtf8(f.readAll()).split('\n')) {
        l = l.trimmed();
        if (l.startsWith(key + ":", Qt::CaseInsensitive))
            out << QProcess::splitCommand(l.mid(key.size() + 1).trimmed());
    }
    return out;
}

QStringList MainWindow::projectModules(Tab *tab)
{
    //! Returns the absolute paths of the other modules listed in
    //! asmon.project next to the file, or {""} after reporting an error.
    lastModulePaths.clear();
    const QString filePath = tab->getCurrentFilePath();
    if (filePath.isEmpty())
        return QStringList();
    const QDir dir = QFileInfo(filePath).absoluteDir();
    QFile f(dir.filePath("asmon.project"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringList();
    QStringList modules;
    const QString self = QFileInfo(filePath).absoluteFilePath();
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    for (QString l : lines) {
        l = l.trimmed();
        if (l.isEmpty() || l.startsWith('#') || l.startsWith(';') || isProjectSettingLine(l))
            continue;
        const QString abs = QFileInfo(dir.filePath(l)).absoluteFilePath();
        if (abs == self)
            continue;
        if (!QFileInfo::exists(abs)) {
            printLogWithTime(tr("asmon.project lists a file that does not exist: %1").arg(l) + '\n',
                             Theme::color("errorcolor"));
            showBottomPage(compilerOut);
            return QStringList() << QString();
        }
        modules << abs;
        lastModulePaths.insert(QFileInfo(abs).fileName(), abs);
    }
    return modules;
}

QString MainWindow::projectMainFile(Tab *tab)
{
    //! If tab's file is listed in asmon.project, the main file is the .asm
    //! next to it that is not listed and defines main.
    const QString filePath = tab ? tab->getCurrentFilePath() : QString();
    if (filePath.isEmpty())
        return QString();
    const QDir dir = QFileInfo(filePath).absoluteDir();
    QFile f(dir.filePath("asmon.project"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QSet<QString> listed;
    for (QString l : QString::fromUtf8(f.readAll()).split('\n')) {
        l = l.trimmed();
        if (!l.isEmpty() && !l.startsWith('#') && !l.startsWith(';') && !isProjectSettingLine(l))
            listed.insert(QFileInfo(dir.filePath(l)).absoluteFilePath());
    }
    if (!listed.contains(QFileInfo(filePath).absoluteFilePath()))
        return QString();
    static const QRegularExpression mainLabel("^\\s*(main|_main|start|_start)\\s*:",
                                              QRegularExpression::MultilineOption);
    for (const QString &name : dir.entryList(QStringList() << "*.asm" << "*.s" << "*.S", QDir::Files, QDir::Name)) {
        const QString abs = dir.absoluteFilePath(name);
        if (listed.contains(abs))
            continue;
        QFile src(abs);
        if (src.open(QIODevice::ReadOnly) && QString::fromUtf8(src.readAll()).contains(mainLabel))
            return abs;
    }
    return QString();
}

void MainWindow::createProjectFile()
{
    if (!tabs || tabs->count() == 0)
        return;
    Tab *tab = (Tab *) tabs->currentWidget();
    if (tab->getCurrentFilePath().isEmpty() && !saveFile(-1, true))
        return;
    const QFileInfo fi(tab->getCurrentFilePath());
    const QString projectPath = fi.absoluteDir().filePath("asmon.project");
    if (!QFile::exists(projectPath)) {
        QString text = tr("# Asmon project: these files are assembled and linked together\n"
                          "# with the file you build (the one with main).\n"
                          "# One file per line, relative to this folder. Lines starting with # are ignored.\n"
                          "# Extra flags (optional):\n"
                          "#   assembler-flags: -DDEBUG\n"
                          "#   linker-flags: -lm\n");
        const QStringList others = fi.absoluteDir().entryList(QStringList() << "*.asm" << "*.s" << "*.S",
                                                               QDir::Files, QDir::Name);
        for (const QString &o : others)
            if (o != fi.fileName())
                text += o + '\n';
        QFile f(projectPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(text.toUtf8());
    }
    openFile(projectPath);
}

void MainWindow::markBuildDiagnostics(const QString &prettyLog)
{
    //! "file.asm:8: error: ..." lines for the open file -> marks in the code
    if (!tabs || tabs->count() == 0)
        return;
    Tab *tab = (Tab *) tabs->currentWidget();
    const QString path = tab->getCurrentFilePath();
    const QString name = path.isEmpty() ? tr("untitled.asm") : QFileInfo(path).fileName();
    static const QRegularExpression re("^(.*?):(\\d+):(?:\\d+:)?\\s*(error|warning|fatal)[^:]*:\\s*(.*)$",
                                       QRegularExpression::CaseInsensitiveOption);
    QMap<int, CodeEditor::Diagnostic> marks;
    for (const QString &line : prettyLog.split('\n')) {
        const QRegularExpressionMatch m = re.match(line.trimmed());
        if (!m.hasMatch() || QFileInfo(m.captured(1).trimmed()).fileName() != name)
            continue;
        const int n = m.captured(2).toInt();
        const bool error = m.captured(3).compare("warning", Qt::CaseInsensitive) != 0;
        if (marks.contains(n)) {
            marks[n].message += "\n" + m.captured(4);
            marks[n].error = marks[n].error || error;
        } else {
            marks.insert(n, CodeEditor::Diagnostic{ m.captured(4), error });
        }
    }
    tab->code->setDiagnostics(marks);
}

// ---------------------------------------------------------------- navigation

QList<Symbols::Source> MainWindow::navigationSources(int *currentIndex)
{
    //! the current file, its project modules and the files it includes
    //! (open editors win over the copy on disk)
    QList<Symbols::Source> out;
    if (currentIndex)
        *currentIndex = -1;
    if (!tabs || tabs->count() == 0)
        return out;
    QHash<QString, QString> open;
    for (int i = 0; i < tabs->count(); ++i) {
        Tab *t = (Tab *) tabs->widget(i);
        if (!t->getCurrentFilePath().isEmpty())
            open.insert(QFileInfo(t->getCurrentFilePath()).absoluteFilePath(), t->code->plainTextWithoutMarks());
    }
    Tab *cur = (Tab *) tabs->currentWidget();
    const QString curPath = cur->getCurrentFilePath().isEmpty() ? QString()
                                                                : QFileInfo(cur->getCurrentFilePath()).absoluteFilePath();
    out << Symbols::Source{ curPath, cur->code->plainTextWithoutMarks() };
    if (currentIndex)
        *currentIndex = 0;
    QSet<QString> seen;
    seen.insert(curPath);
    auto readFile = [&open](const QString &path) -> QString {
        if (open.contains(path))
            return open.value(path);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024)
            return QString();
        return QString::fromUtf8(f.readAll());
    };
    const QString dir = curPath.isEmpty() ? QString() : QFileInfo(curPath).absolutePath();
    QStringList queue;
    //! a project (asmon.project): its modules and the other sources of its folder
    if (!dir.isEmpty() && QFile::exists(QDir(dir).filePath("asmon.project"))) {
        QFile pf(QDir(dir).filePath("asmon.project"));
        if (pf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (QString l : QString::fromUtf8(pf.readAll()).split('\n')) {
                l = l.trimmed();
                if (!l.isEmpty() && !l.startsWith('#') && !l.startsWith(';') && !isProjectSettingLine(l))
                    queue << QFileInfo(QDir(dir).filePath(l)).absoluteFilePath();
            }
        }
        //! the folder's other sources (the main file, when this is a module)
        for (const QString &n : QDir(dir).entryList(QStringList() << "*.asm" << "*.inc" << "*.s", QDir::Files))
            queue << QDir(dir).absoluteFilePath(n);
    }
    //! includes, two levels deep
    auto addIncludes = [&](const QString &text, const QString &baseDir) {
        for (const QString &inc : Symbols::includes(text)) {
            for (const QString &d : { baseDir, dir, Common::applicationDataPath() + "/include" }) {
                if (d.isEmpty())
                    continue;
                const QString p = QFileInfo(QDir(d).filePath(inc)).absoluteFilePath();
                if (QFileInfo::exists(p)) {
                    queue.prepend(p);
                    break;
                }
            }
        }
    };
    addIncludes(out.first().text, dir);
    for (int depth = 0; !queue.isEmpty() && out.size() < 200; ++depth) {
        const QString p = queue.takeFirst();
        if (seen.contains(p))
            continue;
        seen.insert(p);
        const QString text = readFile(p);
        out << Symbols::Source{ p, text };
        if (depth < 50)
            addIncludes(text, QFileInfo(p).absolutePath());
    }
    return out;
}

void MainWindow::jumpToLocation(const QString &file, int line, int column)
{
    if (!file.isEmpty()) {
        Tab *cur = tabs->count() > 0 ? (Tab *) tabs->currentWidget() : nullptr;
        if (!cur || QFileInfo(cur->getCurrentFilePath()).absoluteFilePath() != QFileInfo(file).absoluteFilePath())
            openFile(file);
    }
    if (tabs->count() == 0)
        return;
    CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
    const QTextBlock block = code->document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    //! the column counts characters of the file; skip Hebrew display marks
    int pos = 0, seen = 0;
    const QString t = block.text();
    while (pos < t.size() && seen < column) {
        if (t.at(pos) != QChar(0x200E) && t.at(pos) != QChar(0x202B))
            ++seen;
        ++pos;
    }
    QTextCursor c(block);
    c.setPosition(block.position() + pos);
    code->setTextCursor(c);
    code->centerCursor();
    code->setFocus();
}

QString MainWindow::wordUnderCursor(int *lineIndex)
{
    if (!tabs || tabs->count() == 0)
        return QString();
    CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
    const QTextCursor c = code->textCursor();
    if (lineIndex)
        *lineIndex = c.blockNumber();
    if (c.hasSelection())
        return CodeEditor::stripBidiMarks(c.selectedText()).trimmed();
    const QString line = CodeEditor::stripBidiMarks(c.block().text());
    const int col = c.positionInBlock() - CodeEditor::marksBefore(c);
    return Symbols::wordAt(line, col);
}

void MainWindow::goToDefinition()
{
    int line = 0;
    const QString word = wordUnderCursor(&line);
    if (word.isEmpty())
        return;
    int current = -1;
    const QList<Symbols::Source> sources = navigationSources(&current);
    Symbols::Symbol sym;
    if (Symbols::findDefinition(sources, current, line, word, &sym)) {
        jumpToLocation(sym.file, sym.line, 0);
        statusBar()->showMessage(tr("%1: %2, line %3").arg(word, Symbols::kindName(sym.kind)).arg(sym.line), 4000);
    } else {
        statusBar()->showMessage(tr("No definition of \"%1\" was found.").arg(word), 4000);
    }
}

void MainWindow::findReferences()
{
    const QString word = wordUnderCursor();
    if (word.isEmpty())
        return;
    const QList<Symbols::Source> sources = navigationSources();
    searchPanel->showReferences(word, Symbols::findReferences(sources, word));
    showBottomPage(searchPanel);
}

void MainWindow::goToSymbol()
{
    if (!tabs || tabs->count() == 0)
        return;
    int current = -1;
    const QList<Symbols::Source> sources = navigationSources(&current);
    QList<CommandPalette::Item> items;
    //! this file's symbols first, then the project's (not the includes' macros)
    for (int i = 0; i < sources.size(); ++i) {
        const Symbols::Source &src = sources.at(i);
        const bool here = (i == current);
        if (!here && src.file.startsWith(QDir(Common::applicationDataPath() + "/include").absolutePath()))
            continue;
        for (const Symbols::Symbol &sym : Symbols::scan(src.text, src.file)) {
            const QString name = sym.kind == Symbols::LocalLabel ? sym.parent + sym.name : sym.name;
            const QString where = here ? tr("line %1").arg(sym.line)
                                       : QFileInfo(sym.file).fileName() + ":" + QString::number(sym.line);
            const QString file = sym.file;
            const int line = sym.line;
            //! the (Latin) name first, as one left-to-right unit in the RTL list
            const QString shown = QApplication::layoutDirection() == Qt::RightToLeft
                                      ? QChar(0x202A) + name + QChar(0x202C) : name;
            items << CommandPalette::Item{ shown, Symbols::kindName(sym.kind) + "  ·  " + where,
                                           [this, file, line]() { jumpToLocation(file, line, 0); } };
        }
    }
    commandPalette->popupItems(items, tr("Go to a label, data, constant or macro..."));
}

void MainWindow::goToLine()
{
    if (!tabs || tabs->count() == 0)
        return;
    CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
    bool ok = false;
    const int n = QInputDialog::getInt(this, tr("Go to line"), tr("Line (1-%1):").arg(code->blockCount()),
                                       code->textCursor().blockNumber() + 1, 1, code->blockCount(), 1, &ok);
    if (ok)
        jumpToLocation(QString(), n, 0);
}

void MainWindow::findInFiles()
{
    QString dir = saveOpenDirectory;
    QHash<QString, QString> open;
    if (tabs && tabs->count() > 0) {
        const QString p = ((Tab *) tabs->currentWidget())->getCurrentFilePath();
        if (!p.isEmpty())
            dir = QFileInfo(p).absolutePath();
        for (int i = 0; i < tabs->count(); ++i) {
            Tab *t = (Tab *) tabs->widget(i);
            if (!t->getCurrentFilePath().isEmpty())
                open.insert(QFileInfo(t->getCurrentFilePath()).absoluteFilePath(), t->code->plainTextWithoutMarks());
        }
    }
    searchPanel->setRoot(dir);
    searchPanel->setOpenBuffers(open);
    showBottomPage(searchPanel);
    QString selected;
    if (tabs && tabs->count() > 0) {
        const QTextCursor c = ((Tab *) tabs->currentWidget())->code->textCursor();
        if (c.hasSelection() && !c.selectedText().contains(QChar::ParagraphSeparator))
            selected = CodeEditor::stripBidiMarks(c.selectedText());
    }
    searchPanel->focusSearch(selected);
}

void MainWindow::editBreakpointCondition()
{
    if (!tabs || tabs->count() == 0)
        return;
    CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
    const int line = code->textCursor().blockNumber() + 1;
    bool ok = false;
    const QString cond = QInputDialog::getText(this, tr("Breakpoint condition"),
        tr("Stop on line %1 only when (for example: rcx == 5, rax > 100, rdi != 0).\n"
           "Empty: always stop.").arg(line), QLineEdit::Normal, code->breakpointCondition(line), &ok);
    if (!ok)
        return;
    code->setBreakpointCondition(line, cond.trimmed());
    const int index = debugFileIndex((Tab *) tabs->currentWidget());
    if (debugger && index >= 0)
        debugger->setBreakpointCondition(index * DebugBackend::kModuleLineBase + line, cond.trimmed());
}

void MainWindow::addRecentFile(const QString &path)
{
    QStringList recent = settings.value("recentfiles").toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    recent.removeAll(abs);
    if (QFileInfo(abs).fileName() != "Asmon.ini")
        recent.prepend(abs);
    while (recent.size() > 10)
        recent.removeLast();
    settings.setValue("recentfiles", recent);
    refreshRecentFiles();
}

void MainWindow::refreshRecentFiles()
{
    QStringList existing;
    for (const QString &p : settings.value("recentfiles").toStringList())
        if (QFileInfo::exists(p))
            existing << p;
    getStartedWidget->setRecentFiles(existing);
}

void MainWindow::updateStatusBar()
{
    if (!statusCursor)
        return;
    if (tabs && tabs->count() > 0 && mainWidget->currentIndex() == 1) {
        const QTextCursor c = ((Tab *) tabs->currentWidget())->code->textCursor();
        QString pos = tr("Ln %1, Col %2").arg(c.blockNumber() + 1)
                          .arg(c.positionInBlock() - CodeEditor::marksBefore(c) + 1);
        if (c.hasSelection())
            pos += "  " + tr("(%1 selected)").arg(CodeEditor::stripBidiMarks(c.selectedText()).size());
        statusCursor->setText(pos);
        QTextDocument *doc = ((Tab *) tabs->currentWidget())->code->document();
        statusDoc->setText(tr("Lines: %1").arg(doc->blockCount()) + "   " + tr("Length: %1").arg(doc->characterCount() - 1)
                           + "   UTF-8");
    } else {
        statusCursor->clear();
        statusDoc->clear();
    }
    QString mode = settings.value("assembler", QString("NASM")).toString() + "  ·  "
                   + settings.value("mode", QString("x64")).toString();
    #ifdef Q_OS_WIN32
    //! the target is the combo box beside this label
    #else
    mode += "  ·  Linux";
    #endif
    statusMode->setText(mode);
    QString state;
    if (debugger)
        state = debugger->isStopped() ? tr("Debugging: paused") : tr("Debugging: running");
    else if (!programStopped)
        state = tr("Running...");
    statusState->setText(state);
    //! VS Code: the status bar is blue, orange while a program is being debugged
    const bool debugging = debugger != nullptr;
    if (statusBar()->property("debugging").toBool() != debugging) {
        statusBar()->setProperty("debugging", debugging);
        statusBar()->style()->unpolish(statusBar());
        statusBar()->style()->polish(statusBar());
        for (QWidget *w : statusBar()->findChildren<QWidget *>()) {
            w->style()->unpolish(w);
            w->style()->polish(w);
        }
    }
}

void MainWindow::showBottomPage(QWidget *page)
{
    if (!bottomPanel || !page)
        return;
    bottomPanel->setCurrentWidget(page);
    if (!bottomPanel->isVisible()) {
        if (togglePanelAction)
            togglePanelAction->setChecked(true);
        else
            bottomCard->show();
    }
}

void MainWindow::appendDebugConsole(const QString &text, const QColor &color)
{
    QTextCursor c(debugConsole->document());
    c.movePosition(QTextCursor::End);
    QTextCharFormat f;
    f.setForeground(color);
    c.insertText(text, f);
    debugConsole->setTextCursor(c);
    debugConsole->ensureCursorVisible();
}

void MainWindow::syncBottomPanelWithTab()
{
    if (!tabs || tabs->count() == 0)
        return;
    Tab *t = (Tab *) tabs->currentWidget();
    disconnect(statusCursorConnection);
    statusCursorConnection = connect(t->code, &QPlainTextEdit::cursorPositionChanged,
                                     this, &MainWindow::updateStatusBar);
    updateStatusBar();
    outputStack->setCurrentWidget(t->outputEdit());
    inputStack->setCurrentWidget(t->inputEdit());
    const QString path = t->getCurrentFilePath();
    terminal->setWorkingDirectory(path.isEmpty() ? saveOpenDirectory : QFileInfo(path).absolutePath());
}

void MainWindow::showCommandPalette()
{
    commandPalette->popup(menuBar());
}

void MainWindow::toggleFocusMode(bool on)
{
    //! Focus mode: only the code stays. Everything around it goes away (menu, mode bar, file tabs,
    //! bottom panel, debugger panels, status bar) and comes back exactly as it was on exit.
    static QShortcut *escape = nullptr;
    if (!escape) {
        escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        escape->setContext(Qt::ApplicationShortcut);
        escape->setEnabled(false);
        connect(escape, &QShortcut::activated, this, [this]() {
            if (QWidget *popup = QApplication::activePopupWidget())
                popup->close();   // Esc first closes an open completion list or menu
            else
                focusModeAction->setChecked(false);
        });
    }
    escape->setEnabled(on);   // Esc leaves focus mode; outside it Esc keeps its usual meaning
    if (on) {
        if (modeBar->mode() != ModeBar::Write)
            modeBar->setMode(ModeBar::Write);
        hiddenByFocusMode.clear();
        QList<QWidget *> candidates;
        candidates << menuBar() << modeBar << bottomCard << statusBar() << tabs->tabBar();
        for (QDockWidget *d : findChildren<QDockWidget *>())
            candidates << d;
        for (QWidget *w : candidates) {
            if (w && w->isVisible()) {
                hiddenByFocusMode << w;
                w->hide();
            }
        }
        //! a short hint over the code (the log panel is hidden, so a log line would not be seen)
        QLabel *hint = new QLabel(tr("Focus mode  ·  Esc to exit"), tabs);
        hint->setObjectName("focusHint");
        hint->setAttribute(Qt::WA_TransparentForMouseEvents);
        hint->setStyleSheet("QLabel#focusHint { background: rgba(40,40,40,230); color: #e6e6e6;"
                            " border: 1px solid #3a3a3a; border-radius: 10px; padding: 7px 16px; }");
        hint->adjustSize();
        hint->move((tabs->width() - hint->width()) / 2, 14);
        hint->show();
        hint->raise();
        QTimer::singleShot(3500, hint, &QObject::deleteLater);
        if (Tab *t = qobject_cast<Tab *>(tabs->currentWidget()))
            t->code->setFocus();   // typing continues right away
    } else {
        for (QWidget *w : hiddenByFocusMode)
            if (w)
                w->show();
        hiddenByFocusMode.clear();
        if (QLabel *hint = tabs->findChild<QLabel *>("focusHint"))
            hint->deleteLater();
        if (Tab *t = qobject_cast<Tab *>(tabs->currentWidget()))
            t->code->setFocus();
    }
}

void MainWindow::toggleExplorer(bool on)
{
    settings.setValue("explorervisible", on);
    if (on)
        updateExplorerRoot();
}

void MainWindow::updateExplorerRoot()
{
    if (!explorerDock)
        return;
    QString dir = saveOpenDirectory;
    if (tabs && tabs->count() > 0) {
        QString path = ((Tab *) tabs->currentWidget())->getCurrentFilePath();
        if (!path.isEmpty())
            dir = QFileInfo(path).absolutePath();
    }
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::homePath();
    explorerModel->setRootPath(dir);
    explorerView->setRootIndex(explorerModel->index(dir));
    explorerDock->setWindowTitle(tr("Explorer") + " — " + QDir(dir).dirName());
}

void MainWindow::openFromExplorer(const QModelIndex &index)
{
    const QString path = explorerModel->filePath(index);
    if (QFileInfo(path).isFile())
        openFile(path);
}

void MainWindow::switchTheme(bool dark)
{
    //! remember the old theme's log colours to recolour text already written
    const QStringList logKeys = { "fontcolor", "errorcolor", "successcolor", "keywordscolor" };
    QList<QColor> oldColors;
    for (const QString &k : logKeys)
        oldColors << Theme::color(k);
    Theme::setTheme(settings, dark);
    if (pendingNpp) {
        //! a Notepad++ colour theme: its colours replace the defaults just written
        static const char *keys[9] = { "keywords", "registers", "numbers", "memory", "labels",
                                       "comments", "system", "iomacro", "quotation" };
        for (int k = 0; k < 9; k++) {
            settings.setValue(QString(keys[k]) + "color", QString::fromLatin1(pendingNpp->syntax[k]));
            settings.setValue(QString(keys[k]) + "colorbg", QString::fromLatin1(pendingNpp->bg));
        }
        settings.setValue("backgroundcolor", QString::fromLatin1(pendingNpp->bg));
        settings.setValue("linenumberpanelcolor", QString::fromLatin1(pendingNpp->bg));
        settings.setValue("fontcolor", QString::fromLatin1(pendingNpp->fg));
        settings.setValue("codetextcolor", QString::fromLatin1(pendingNpp->fg));
        settings.setValue("currentlinecolor", QString::fromLatin1(pendingNpp->currentLine));
        settings.setValue("linenumberfontcolor", QString::fromLatin1(pendingNpp->lineNumber));
    }
    QHash<QRgb, QColor> recolor;
    for (int i = 0; i < logKeys.size(); ++i)
        if (oldColors[i] != Theme::color(logKeys[i]))
            recolor.insert(oldColors[i].rgb(), Theme::color(logKeys[i]));
    for (QTextEdit *e : findChildren<QTextEdit *>())
        Theme::recolorDocument(e->document(), recolor);
    for (QPlainTextEdit *e : findChildren<QPlainTextEdit *>())
        if (!qobject_cast<CodeEditor *>(e))
            Theme::recolorDocument(e->document(), recolor);
    Theme::apply(*qApp);
    for (QAction *a : findChildren<QAction *>()) {
        const QString name = a->property("asmonIcon").toString();
        if (!name.isEmpty())
            a->setIcon(Theme::icon(name));
    }
    for (QToolButton *b : findChildren<QToolButton *>()) {
        const QString name = b->property("asmonIcon").toString();
        if (!name.isEmpty())
            b->setIcon(Theme::icon(name));
    }
    //! Editors get their colours after the style sheet re-polish has run,
    //! otherwise the re-polish restores the old palette.
    QTimer::singleShot(0, this, [this]() {
        for (RuQPlainTextEdit *e : findChildren<RuQPlainTextEdit *>())
            e->applyThemeColors();
        for (RuQTextEdit *e : findChildren<RuQTextEdit *>())
            e->applyThemeColors();
        recreateHighlighter();
    });
}

void MainWindow::applyNppTheme(int index)
{
    if (index < 0 || index >= kNppThemeCount)
        return;
    pendingNpp = &kNppThemes[index];
    switchTheme(pendingNpp->dark);
    initColorButtons(pendingNpp->dark, false);
    pendingNpp = nullptr;
    mainWidget->update();
}

void MainWindow::refreshEditMenu()
{
    CodeEditor *codeEditor = 0;

    //! Disconnect from previous
    if (prevCodeEditor) {
        disconnect(commentAction, SIGNAL(triggered()), prevCodeEditor, SLOT(commentSelectedCode()));
        disconnect(uncommentAction, SIGNAL(triggered()), prevCodeEditor, SLOT(uncommentSelectedCode()));
        disconnect(undoAction, SIGNAL(triggered()), prevCodeEditor, SLOT(undo()));
        disconnect(redoAction, SIGNAL(triggered()), prevCodeEditor, SLOT(redo()));
        disconnect(cutAction, SIGNAL(triggered()), prevCodeEditor, SLOT(cut()));
        disconnect(copyAction, SIGNAL(triggered()), prevCodeEditor, SLOT(copy()));
        disconnect(pasteAction, SIGNAL(triggered()), prevCodeEditor, SLOT(paste()));
        disconnect(deleteAction, SIGNAL(triggered()), prevCodeEditor, SLOT(deleteSelected()));
        disconnect(selectAllAction, SIGNAL(triggered()), prevCodeEditor, SLOT(selectAll()));
        disconnect(putTabAction, SIGNAL(triggered()), prevCodeEditor, SLOT(putTab()));
        disconnect(deleteTabAction, SIGNAL(triggered()), prevCodeEditor, SLOT(deleteTab()));

        disconnect(prevCodeEditor, SIGNAL(undoAvailable(bool)), undoAction, SLOT(setEnabled(bool)));
        disconnect(prevCodeEditor, SIGNAL(redoAvailable(bool)), redoAction, SLOT(setEnabled(bool)));
    }

    //! Connect to current
    if (tabs->currentIndex() != -1) {
        codeEditor = ((Tab *) tabs->currentWidget())->code;
        connect(commentAction, SIGNAL(triggered()), codeEditor, SLOT(commentSelectedCode()));
        connect(uncommentAction, SIGNAL(triggered()), codeEditor, SLOT(uncommentSelectedCode()));
        connect(undoAction, SIGNAL(triggered()), codeEditor, SLOT(undo()));
        connect(redoAction, SIGNAL(triggered()), codeEditor, SLOT(redo()));
        connect(cutAction, SIGNAL(triggered()), codeEditor, SLOT(cut()));
        connect(copyAction, SIGNAL(triggered()), codeEditor, SLOT(copy()));
        connect(pasteAction, SIGNAL(triggered()), codeEditor, SLOT(paste()));
        connect(deleteAction, SIGNAL(triggered()), codeEditor, SLOT(deleteSelected()));
        connect(selectAllAction, SIGNAL(triggered()), codeEditor, SLOT(selectAll()));
        connect(putTabAction, SIGNAL(triggered()), codeEditor, SLOT(putTab()));
        connect(deleteTabAction, SIGNAL(triggered()), codeEditor, SLOT(deleteTab()));

        connect(codeEditor, SIGNAL(undoAvailable(bool)), undoAction, SLOT(setEnabled(bool)));
        connect(codeEditor, SIGNAL(redoAvailable(bool)), redoAction, SLOT(setEnabled(bool)));
        connect(codeEditor, SIGNAL(copyAvailable(bool)), cutAction, SLOT(setEnabled(bool)));
        connect(codeEditor, SIGNAL(copyAvailable(bool)), copyAction, SLOT(setEnabled(bool)));
        connect(codeEditor, SIGNAL(copyAvailable(bool)), commentAction, SLOT(setEnabled(bool)));
        connect(codeEditor, SIGNAL(copyAvailable(bool)), uncommentAction, SLOT(setEnabled(bool)));
        connect(codeEditor, SIGNAL(copyAvailable(bool)), deleteAction, SLOT(setEnabled(bool)));
    }

    prevCodeEditor = codeEditor;
}

void MainWindow::newFile()
{
    //! Tabs
    mainWidget->setCurrentIndex(1);
    Tab *tab = new Tab;
    //! the tab's program output/input are shown in the bottom panel
    outputStack->addWidget(tab->outputEdit());
    inputStack->addWidget(tab->inputEdit());
    tab->code->setPlainText(startText);
    tabs->addTab(tab, tr("New"));
    tabs->setCurrentWidget(tab);
    connect(tab->code, SIGNAL(textChanged()), this, SLOT(setProgramBuildedFlagToFalse()));
    connect(tab->code, SIGNAL(textChanged()), this, SLOT(findNextAfterTextChange()));
    connect(tab->code, SIGNAL(modificationChanged(bool)), this, SLOT(changeCurrentSavedState(bool)));
    connect(tab->code, SIGNAL(fileOpened(QString)), this, SLOT(openFile(QString)));
    connect(tab->code, &CodeEditor::definitionRequested, this, &MainWindow::goToDefinition);
}

void MainWindow::changeCurrentSavedState(bool changed)
{
    if (changed)
        tabs->setTabText(tabs->currentIndex(), '*' + tabs->tabText(tabs->currentIndex()));
    else {
        QString tabText = tabs->tabText(tabs->currentIndex());
        if (tabText[0] == '*')
            tabText.remove(0, 1);
        tabs->setTabText(tabs->currentIndex(), tabText);
    }
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open file"), saveOpenDirectory,
                                                    tr("Assembler source files (*.asm *.S *.s);;All files (*)"));
    if (fileName.isEmpty()) {
        return;
    }
    saveOpenDirectory = QFileInfo(fileName).absoluteDir().absolutePath();
    openFile(fileName);
}

void MainWindow::openFile(QString path)
{
    if (path.isEmpty()) {
        return;
    }
    //! Asmon: a file that is already open is shown, not opened twice.
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < tabs->count(); ++i) {
        const QString p = ((Tab *) tabs->widget(i))->getCurrentFilePath();
        if (!p.isEmpty() && QFileInfo(p).absoluteFilePath() == wanted) {
            mainWidget->setCurrentIndex(1);
            tabs->setCurrentIndex(i);
            return;
        }
    }

    newFile();
    Tab *curTab = (Tab *) tabs->currentWidget();
    curTab->loadCodeFromFile(path);
    setTabPath(path);
    addRecentFile(path);
    //connect(curTab, SIGNAL(fileOpened(QString)), this, SLOT(openFile(QString)));
}

void MainWindow::setTabPath(const QString &filePath, int index)
{
    //! Taking name of file (without path)
    int i;
    if ((i = filePath.lastIndexOf('/')) == -1)
        i = filePath.lastIndexOf('\\');
    QString caption = filePath.mid(i + 1);
    if (index == -1) {
        tabs->setTabToolTip(tabs->currentIndex(), filePath);
        tabs->setTabText(tabs->currentIndex(), caption);
    } else {
        tabs->setTabToolTip(index, filePath);
        tabs->setTabText(index, caption);
    }
}

void MainWindow::closeFile()
{
    deleteTab(tabs->currentIndex());
}

bool MainWindow::saveFile(int index, bool openSaveAs)
{
    Tab *tab;
    if (index == -1)
        tab = (Tab *) tabs->currentWidget();
    else
        tab = (Tab *) tabs->widget(index);
    QString filePath = tab->getCurrentFilePath();
    if (filePath.isEmpty()) {
        if (openSaveAs) {
            return saveAsFile(index);
        } else {
            return false;
        }
    } else {
        tab->saveCodeToFile(filePath, assembler);
        return true;
    }
}

bool MainWindow::saveAsFile(int index)
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save file"), saveOpenDirectory,
                                                    tr("Assembler source files (*.asm);;All files (*)"));
    if (fileName.isEmpty()) {
        return false;
    }
    saveOpenDirectory = QFileInfo(fileName).absoluteDir().absolutePath();
    Tab *tab;
    if (index == -1)
        tab = (Tab *) tabs->currentWidget();
    else
        tab = (Tab *) tabs->widget(index);
    tab->saveCodeToFile(fileName, assembler);
    setTabPath(fileName, index);
    addRecentFile(fileName);
    return true;
}

void MainWindow::saveExe()
{
    if (!programIsBuilded) {
        buildProgram();
    }
    if (!programIsBuilded) {
       return;
    }
    QString saveFileName = QFileDialog::getSaveFileName(this, tr("Save .exe file"), saveOpenDirectory,
                                                    tr("Execution files (*.exe);;All files (*.*)"));
    if (saveFileName.isEmpty())
        return;
    saveOpenDirectory = QFileInfo(saveFileName).absoluteDir().absolutePath();
    QFile exeFile(Common::pathInTemp("AsmonProg.exe"));
    QFile::remove(saveFileName);
    exeFile.copy(saveFileName);
}

bool MainWindow::closeApp()
{
    int tabsCount = tabs->count();
    if (tabsCount > 0) {
        //! Consider all tabs
        tabs->setCurrentIndex(0);
        for (int i = 0; i < tabsCount; i++) {
            //! Left only saved tabs
            if (!deleteTab(tabs->currentIndex(), true))
                return false;
        }
    }
    writeSettings();
    closeFromCloseAll = true;
    close();
    return true;
}

void MainWindow::closeEvent(QCloseEvent *e)
{

    if (!closeFromCloseAll)
        if (closeApp()) {
            closeAllChildWindows();
            e->accept();
        } else
            e->ignore();
    else
        e->accept();
}

void MainWindow::closeAllChildWindows()
{
    if (findDialog) {
        findDialog->close();
        delete findDialog;
    }
    if (settingsWindow) {
        delete settingsPage;
    }
    if (help) {
        help->close();
        delete help;
    }
}

bool MainWindow::deleteTab(int index, bool saveFileName)
{
    if (okToContinue(index)) {
        if (debugger && index == tabs->currentIndex())
            debugExit();
        if (tabs->count() == 1)
            //! Get started
            mainWidget->setCurrentIndex(0);
        QTimer::singleShot(0, this, &MainWindow::updateStatusBar);
        Tab *tabForDeleting = (Tab *) tabs->widget(index);
        if (!saveFileName || tabForDeleting->getCurrentFilePath().isEmpty()) {
            tabs->removeTab(index);
            delete tabForDeleting;
        } else {
            //! If tab did not delete
            tabs->setCurrentIndex(tabs->currentIndex() + 1);
        }
        if (tabForDeleting->code == prevCodeEditor)
            prevCodeEditor = 0;
        return true;
    }
    return false;
}

bool MainWindow::okToContinue(int index)
{
    Tab *deletingTab;
    if (index == -1)
        deletingTab = (Tab *) tabs->currentWidget();
    else
        deletingTab = (Tab *) tabs->widget(index);
    if (deletingTab->code->document()->isModified()) {
        QMessageBox msgBox(this);
        QPushButton *yesButton = msgBox.addButton(tr("Yes"), QMessageBox::YesRole);
        msgBox.addButton(tr("No"), QMessageBox::NoRole);
        QPushButton *cancelButton = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
        msgBox.setWindowTitle(tr("Save changes?"));
        msgBox.setText(tr("Save changes?"));
        msgBox.exec();

        if (msgBox.clickedButton() == yesButton) {
            return saveFile(index);
        } else if (msgBox.clickedButton() == cancelButton) {
            return false;
        }
    }
    return true;
}

void MainWindow::changeCurrentTab(int index)
{
    if (debugger && index >= 0) {
        for (int bp : connectDebugTab((Tab *) tabs->widget(index)))
            debugger->setBreakpoint(bp, true);
    }
    //! Set highlighter
    if (tabs->count() == 0)
        return;
    Tab *currentTab = (Tab *) tabs->widget(index);
    highlighter->setDocument(currentTab->getCodeDocument());
}

void MainWindow::buildProgram(bool debugMode)
{
    saveFile(-1, false); //save file before building if program already was saved

    //! Asmon: building from a module of a project builds the project's main file.
    {
        const QString mainPath = projectMainFile((Tab *) tabs->currentWidget());
        if (!mainPath.isEmpty()) {
            printLogWithTime(tr("This file is a module of the project: building %1")
                                 .arg(QFileInfo(mainPath).fileName()) + '\n', Theme::color("fontcolor"));
            openFile(mainPath);
        }
    }

    programIsBuilded = false;

    using Common::applicationDataPath;
    printLogWithTime(tr("Build started...") + '\n', Theme::color("fontcolor"));
    showBottomPage(compilerOut);
    QCoreApplication::processEvents();

    if (! QFile::exists(applicationDataPath())) {
        printLogWithTime(tr("Error! Program directory does not exist. Please reinstall the program.") + '\n', Theme::color("errorcolor"));
        QMessageBox::critical(0, tr("Error!"), tr("Directory NASM does not exist. Please reinstall the program."));
        return;
    }

    #ifdef Q_OS_WIN32
    //! Asmon: the 32-bit toolchain is an optional component.
    if (settings.value("mode", QString("x64")).toString() == "x86"
            && !QFile::exists(applicationDataPath() + "/MinGW/bin/gcc.exe")) {
        printLogWithTime(tr("The 32-bit toolchain is not installed. Switch to x64 mode in Settings, "
                            "or extract the 32-bit tools pack (Asmon-x86-tools.zip) into the Asmon folder.") + '\n', Theme::color("errorcolor"));
        return;
    }
    #endif

    QString tempPath = Common::pathInTemp(QString());
    removeDirRecuresively(tempPath);
    QString path = Common::pathInTemp("program.asm");

    Tab *currentTab = (Tab *) tabs->currentWidget();
    currentTab->saveCodeToFile(path, assembler, false, debugMode && settings.value("insertdebugstring", false).toBool());

    if (debugMode) {
        //! Save input to file
        QString inputPath = Common::pathInTemp("input.txt");
        Tab *currentTab = (Tab *) tabs->currentWidget();
        currentTab->saveInputToFile(inputPath);
    }

    while (! QFile::exists(path)) {
    }

    //! Asmon: multi-module projects (asmon.project next to the file).
    const QStringList modules = projectModules(currentTab);
    if (!modules.isEmpty() && modules.first().isEmpty())
        return; // projectModules() reported an error
    if (!modules.isEmpty()) {
        QStringList names;
        for (const QString &m : modules)
            names << QFileInfo(m).fileName();
        printLogWithTime(tr("Project: also building %1").arg(names.join(", ")) + '\n', Theme::color("fontcolor"));
        //! save modules that are open and modified
        for (int i = 0; i < tabs->count(); ++i) {
            Tab *t = (Tab *) tabs->widget(i);
            if (t != currentTab && modules.contains(QFileInfo(t->getCurrentFilePath()).absoluteFilePath())
                    && t->code->document()->isModified())
                saveFile(i, false);
        }
    }

    if (linuxTargetSelected()) {
        buildForLinuxTarget(currentTab);
        return;
    }

    QString stdioMacros = Common::pathInTemp("macro.o");

    //! Assembler
    QString assemblerPath = assembler->getAssemblerPath();
    if (settings.contains("assemblerpath"))
        assemblerPath = settings.value("assemblerpath").toString();
    QString assemblerOptions = assembler->getAssemblerOptions();
    if (settings.contains("assemblyoptions"))
        assemblerOptions = settings.value("assemblyoptions").toString();
    QStringList assemblerArguments = assemblerOptions.split(QChar(' '));
    assemblerArguments.replaceInStrings("$SOURCE$", Common::pathInTemp("program.asm"));
    assemblerArguments.replaceInStrings("$LSTOUTPUT$", Common::pathInTemp("program.lst"));
    assemblerArguments.replaceInStrings("$PROGRAM.OBJ$",
        Common::pathInTemp(settings.value("objectfilename", "program.o").toString()));
    assemblerArguments.replaceInStrings("$PROGRAM$", Common::pathInTemp("AsmonProg.exe"));
    assemblerArguments.replaceInStrings("$MACRO.OBJ$", stdioMacros);
    //! Asmon: multi-file programs. NASM runs in the include/ directory (for
    //! io64.inc), so also search the source file's own directory: then
    //! %include "other.asm" / "defs.inc" next to the file just works.
    if (settings.value("assembler", QString("NASM")).toString() == "NASM"
            && !currentTab->getCurrentFilePath().isEmpty()) {
        QString sourceDir = QFileInfo(currentTab->getCurrentFilePath()).absolutePath();
        assemblerArguments.prepend(QDir::toNativeSeparators(sourceDir + "/"));
        assemblerArguments.prepend("-I");
    }
    //! Asmon: extra flags from asmon.project ("assembler-flags: ...")
    const QStringList extraAsmFlags = projectFlags(currentTab, "assembler-flags");
    assemblerArguments << extraAsmFlags;
    QProcess assemblerProcess;
    QString assemblerOutput = Common::pathInTemp("compilererror.txt");
    assemblerProcess.setStandardOutputFile(assemblerOutput);
    assemblerProcess.setStandardErrorFile(assemblerOutput, QIODevice::Append);
    bool currentDir = settings.value("currentdir", false).toBool();
    if (currentDir) {
        QString path = currentTab->getCurrentFilePath();
        if (!path.isEmpty()) {
            QString dirPath = QFileInfo(path).absoluteDir().absolutePath();
            assemblerProcess.setWorkingDirectory(dirPath);
        } else {
            assemblerProcess.setWorkingDirectory(applicationDataPath() + "/include");
        }
    } else {
        assemblerProcess.setWorkingDirectory(applicationDataPath() + "/include");
    }
    assemblerProcess.start(assemblerPath, assemblerArguments);
    assemblerProcess.waitForFinished();

    if (assemblerProcess.error() != QProcess::UnknownError) {
        printLogWithTime(tr("Unable to start assembler. Check your settings.") + '\n', Theme::color("errorcolor"));
        #ifndef Q_OS_WIN32
        printLog(tr("Is it installed? On Ubuntu/Debian: sudo apt install nasm gcc gdb "
                    "(Fedora: sudo dnf install nasm gcc gdb)") + '\n', Theme::color("errorcolor"));
        #endif
        return;
    }

    //! Asmon: assemble the project's other modules, each into its own object.
    QStringList moduleObjects;
    QString modulesLog;
    const QString objectSuffix = QFileInfo(settings.value("objectfilename", "program.o").toString()).suffix();
    for (int i = 0; i < modules.size(); ++i) {
        const QString obj = Common::pathInTemp(QString("module_%1.%2").arg(i + 1).arg(objectSuffix));
        QStringList args = assemblerOptions.split(QChar(' '));
        args.replaceInStrings("$SOURCE$", QDir::toNativeSeparators(modules[i]));
        args.replaceInStrings("$LSTOUTPUT$", Common::pathInTemp(QString("module_%1.lst").arg(i + 1)));
        args.replaceInStrings("$PROGRAM.OBJ$", obj);
        args.replaceInStrings("$PROGRAM$", Common::pathInTemp("AsmonProg.exe"));
        args.replaceInStrings("$MACRO.OBJ$", stdioMacros);
        if (settings.value("assembler", QString("NASM")).toString() == "NASM")
            args << "-I" << QDir::toNativeSeparators(QFileInfo(modules[i]).absolutePath() + "/");
        args << extraAsmFlags;
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.setWorkingDirectory(applicationDataPath() + "/include");
        p.start(assemblerPath, args);
        p.waitForFinished();
        modulesLog += QString::fromLocal8Bit(p.readAll());
        moduleObjects << obj;
    }

    //! GCC
    QString linkerOptions = assembler->getLinkerOptions();
    if (settings.contains("linkingoptions"))
        linkerOptions = settings.value("linkingoptions").toString();
    //! macro.c compilation/copying
    QFile macro;
    #ifdef Q_OS_WIN32
        //! Asmon: WinLibs gcc is not multilib: x64 links with MinGW64, x86 with MinGW.
        QString linker = settings.value("linkerpath", applicationDataPath()
            + (settings.value("mode", QString("x64")).toString() == "x86" ? "/MinGW/bin/gcc.exe"
                                                                           : "/MinGW64/bin/gcc.exe")).toString();
        if (settings.value("mode", QString("x64")).toString() == "x86") {
            macro.setFileName(applicationDataPath() + "/NASM/macro.o");
        } else {
            macro.setFileName(applicationDataPath() + "/NASM/macro64.o");
        }
        macro.copy(stdioMacros);
    #else
        QString gcc = "gcc";
        QString linker = settings.value("linkerpath", "gcc").toString();
        macro.setFileName(applicationDataPath() + "/NASM/macro.c");
        macro.copy(Common::pathInTemp("macro.c"));

        //! macro.c compilation
        QStringList gccMArguments;
        gccMArguments << "-x" << "c" << Common::pathInTemp("macro.c") << "-c" << "-g" << "-o" << stdioMacros;
        if (settings.value("mode", QString("x64")).toString() == "x86")
            gccMArguments << "-m32";
        else
            gccMArguments << "-m64";
        QProcess gccMProcess;
        gccMProcess.start(gcc, gccMArguments);
        gccMProcess.waitForFinished();
    #endif

    //! Final linking
    bool disableLinking = settings.value("disablelinking", false).toBool();
    QString linkerOutput;
    //! Asmon: if the assembler failed there is no object file; running the
    //! linker anyway only adds noise ("cannot find program.o", "collect2").
    bool objectExists = QFile::exists(
        Common::pathInTemp(settings.value("objectfilename", "program.o").toString()));
    for (const QString &obj : moduleObjects)
        objectExists = objectExists && QFile::exists(obj);
    if (!disableLinking && objectExists) {
        QStringList linkerArguments = linkerOptions.split(QChar(' '));
        const int objIndex = linkerArguments.indexOf("$PROGRAM.OBJ$");
        for (int i = moduleObjects.size() - 1; i >= 0; --i)
            linkerArguments.insert(objIndex >= 0 ? objIndex + 1 : linkerArguments.size(), moduleObjects[i]);
        linkerArguments.replaceInStrings("$PROGRAM.OBJ$",
            Common::pathInTemp(settings.value("objectfilename", "program.o").toString()));
        linkerArguments.replaceInStrings("$MACRO.OBJ$", stdioMacros);
        linkerArguments.replaceInStrings("$PROGRAM$", Common::pathInTemp("AsmonProg.exe"));
        linkerArguments.replaceInStrings("$SOURCE$", Common::pathInTemp("program.asm"));
        linkerArguments.replaceInStrings("$LSTOUTPUT$", Common::pathInTemp("program.lst"));
        linkerArguments << projectFlags(currentTab, "linker-flags"); // asmon.project
        QProcess linkerProcess;
        linkerOutput = Common::pathInTemp("linkererror.txt");
        linkerProcess.setStandardOutputFile(linkerOutput);
        linkerProcess.setStandardErrorFile(linkerOutput, QIODevice::Append);
        linkerProcess.start(linker, linkerArguments);
        linkerProcess.waitForFinished();

        if (linkerProcess.error() != QProcess::UnknownError) {
            printLogWithTime(tr("Unable to start linker. Check your settings.") + '\n', Theme::color("errorcolor"));
            #ifndef Q_OS_WIN32
            printLog(tr("Is it installed? On Ubuntu/Debian: sudo apt install nasm gcc gdb "
                        "(Fedora: sudo dnf install nasm gcc gdb)") + '\n', Theme::color("errorcolor"));
            #endif
            return;
        }
    }

    QFile logFile;
    logFile.setFileName(assemblerOutput);
    logFile.open(QIODevice::ReadOnly);
    QTextStream log(&logFile);
    QString logText = log.readAll();
    if (settings.value("assembler", QString("NASM")).toString() == "FASM" && logText.count(QChar('\n')) == 2
            && logText.contains(RegExp(" bytes\\.")))
        logText.clear();
    if (settings.value("assembler", QString("NASM")).toString() == "MASM") {
        if (logText.count(QChar('\n')) == 1 && logText.startsWith(" Assembling:"))
            logText.clear();
        if (logText.count(QChar('\n')) == 6 && logText.contains("ASCII build"))
            logText.clear();
    }
    logFile.close();
    logText += modulesLog;

    bool builded;
    if (QFile::exists(Common::pathInTemp("AsmonProg.exe")))
        builded = true;
    else
        builded = false;
    if (!builded) {
        printLogWithTime(tr("Warning! Errors have occurred in the build:") + '\n', Theme::color("errorcolor"));
        showBottomPage(compilerOut);

        //! Print errors
        printLog(prettifyBuildLog(logText), Theme::color("errorcolor"));
        markBuildDiagnostics(prettifyBuildLog(logText));
        if (!disableLinking && !linkerOutput.isEmpty()) {
            logFile.setFileName(linkerOutput);
            logFile.open(QIODevice::ReadOnly);
            QTextStream logLinker(&logFile);
            logText = logLinker.readAll();
            logFile.close();
            printLog(prettifyBuildLog(logText), Theme::color("errorcolor"));
        }

        //! QMessageBox::critical(0, tr("Warning!"), tr("Errors have occurred in the build!"));
    } else {
        printLogWithTime(tr("Built successfully.") + '\n', Theme::color("successcolor"));
        refreshBuildOutput();
        //! print warnings
        printLog(prettifyBuildLog(logText), Theme::color("errorcolor"));
        markBuildDiagnostics(prettifyBuildLog(logText));
        if (!disableLinking && !linkerOutput.isEmpty()) {
            logFile.setFileName(linkerOutput);
            logFile.open(QIODevice::ReadOnly);
            QTextStream logLinker(&logFile);
            logText = logLinker.readAll();
            logFile.close();
            printLog(prettifyBuildLog(logText), Theme::color("errorcolor"));
        }
        programIsBuilded = true;
    }
}

void MainWindow::runProgram()
{
    if (!programStopped) {
        QMessageBox::information(0, QString("Asmon"), tr("The program is already running."));
        return;
    }
    if (!programIsBuilded) {
        buildProgram();
    }
    outputIndex = tabs->currentIndex(); // after the build: it may switch to the project's main file
    if (!programIsBuilded) {
       return;
    }

    stopAction->setEnabled(true);
    debugAction->setEnabled(false);
    buildAction->setEnabled(false);

    ((Tab *) tabs->currentWidget())->clearOutput();

    printLogWithTime(tr("The program is executing...") + '\n', Theme::color("fontcolor"));
    syncBottomPanelWithTab();
    showBottomPage(outputStack);
    QCoreApplication::processEvents();

    QString input = Common::pathInTemp("input.txt");
    Tab *currentTab = (Tab *) tabs->currentWidget();
    currentTab->saveInputToFile(input);

    QString program = Common::pathInTemp("AsmonProg.exe");
    QStringList programArguments;
    if (linuxTargetSelected()) {
        //! Asmon: Linux ELF built on Windows runs inside WSL.
        QString why;
        if (!LinuxTarget::wslAvailable(&why)) {
            printLogWithTime(why + ' ' + tr("Use Build > Install WSL, then try again.") + '\n',
                             Theme::color("errorcolor"));
            stopAction->setEnabled(false);
            debugAction->setEnabled(true);
            buildAction->setEnabled(true);
            return;
        }
        QStringList cmd = LinuxTarget::wslRunCommand(program);
        program = cmd.takeFirst();
        programArguments = cmd;
    }
    runProcess->setStandardInputFile(input);

    //! Run program in code directory if it exists
    QString codePath = currentTab->getCurrentFilePath();
    if (!codePath.isEmpty()) {
        QString codeDirPath = QFileInfo(codePath).absoluteDir().absolutePath();
        runProcess->setWorkingDirectory(codeDirPath);
    }

    connect(runProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(getOutput()));
    programStopped = false; updateStatusBar();
    connect(runProcess, SIGNAL(started()), this, SLOT(startCountProgramTime()));
    connect(runProcess, SIGNAL(finished(int)), this, SLOT(testStopOfProgram()));
    runProcess->start(program, programArguments);

    timer->start(100);
    //Previous - connect(timer, SIGNAL(timeout()), this, SLOT(testStopOfProgram()));
}

void MainWindow::startCountProgramTime()
{
    programExecutionTime.start();
}

void MainWindow::getOutput()
{
    QByteArray output = runProcess->readAllStandardOutput();
    QString outputString = QString::fromLocal8Bit(output.constData(), output.size());
    printOutput(outputString, outputIndex);
}

void MainWindow::testStopOfProgram()
{
    QCoreApplication::processEvents();
    if (runProcess->state() == QProcess::NotRunning) {
        stopAction->setEnabled(false);
        debugAction->setEnabled(true);
        buildAction->setEnabled(true);
        if (!programStopped) {
            if (runProcess->exitStatus() == QProcess::NormalExit)
                printLogWithTime(tr("The program finished normally. Execution time: %1 s")
                                 .arg(programExecutionTime.elapsed() / 1000.0)
                                 + '\n', Theme::color("successcolor"));
            else
                printLogWithTime(tr("The program crashed! Execution time: %1 s").
                                 arg(programExecutionTime.elapsed() / 1000.0)
                                 + '\n', Theme::color("errorcolor"));
            programStopped = true; updateStatusBar();
        }
        timer->stop();
    }
}

void MainWindow::runExeProgram()
{
    if (!programIsBuilded) {
        buildProgram();
    }
    if (!programIsBuilded) {
       return;
    }

    QString program = Common::pathInTemp("AsmonProg.exe");
    runProcess->startDetached(program, QStringList());
}

void MainWindow::stopProgram()
{
    if (timer->isActive()) {
        programStopped = true; updateStatusBar();
        timer->stop();

        stopAction->setEnabled(false);

        runProcess->kill();

        printLogWithTime(tr("The program stopped.") + '\n', Theme::color("successcolor"));
    } else {
        QMessageBox::information(0, QString("Asmon"), tr("The program is not running."));
    }
}

void MainWindow::printLogWithTime(const QString &message, const QColor &color)
{
    QTime curTime = QTime::currentTime();
    QString timeString = curTime.toString("[HH:mm:ss] ");
    printLog(timeString + message, color);
}

void MainWindow::printLog(const QString &message, const QColor &color)
{
    QTextCursor cursor = QTextCursor(compilerOut->document());
    cursor.movePosition(QTextCursor::End);
    compilerOut->setTextCursor(cursor);
    compilerOut->setTextColor(color);
    compilerOut->insertPlainText(message);
    cursor.movePosition(QTextCursor::End);
    compilerOut->setTextCursor(cursor);
}

void MainWindow::setProgramBuildedFlagToFalse()
{
    programIsBuilded = false;
}

void MainWindow::printOutput(QString msg, int index)
{
    Tab *tab = 0;
    if (index != -1) {
        tab = (Tab *) tabs->widget(index);
    } else
        tab = (Tab *) tabs->currentWidget();
    tab->appendOutput(msg);
}

void MainWindow::debug()
{
    //! Asmon: gdb/MI debugger (MiDebugger) with the RE panels (DebugPanels).
    //! Asmon: a Linux program on Windows is debugged by gdb inside WSL.
    const bool viaWsl = !debugger && linuxTargetSelected();
    if (viaWsl) {
        QString why;
        if (!LinuxTarget::wslAvailable(&why)) {
            printLogWithTime(tr("Debugging a Linux program needs WSL: %1").arg(why) + '\n',
                             Theme::color("errorcolor"));
            return;
        }
        if (!LinuxTarget::wslHasGdb()) {
            printLogWithTime(tr("gdb is not installed in WSL.") + '\n', Theme::color("errorcolor"));
            if (QMessageBox::question(this, tr("Install gdb in WSL"),
                    tr("Debugging Linux programs uses gdb inside WSL, and it is not installed.\n"
                       "Install it now (Ubuntu/Debian: apt-get install gdb)? It runs in the terminal below.")) ==
                    QMessageBox::Yes) {
                showBottomPage(terminal);
                terminal->runCommand(LinuxTarget::wslInstallGdbCommand());
                printLogWithTime(tr("When the installation finishes, press Debug again.") + '\n',
                                 Theme::color("fontcolor"));
            }
            return;
        }
    }
    if (debugger) {
        //! Continue or pause
        if (debugger->isStopped())
            debugger->continueRun();
        else
            debugger->pause();
        return;
    }

    buildProgram(true);
    if (!programIsBuilded) {
        printLogWithTime(tr("Before debugging you need to build the program.") + '\n', Theme::color("errorcolor"));
        return;
    }
    Tab *currentTab = (Tab *) tabs->currentWidget();
    currentTab->clearOutput();
    CodeEditor *code = currentTab->code;
    printLogWithTime(tr("Debugging started...") + '\n', Theme::color("successcolor"));

    DebugBackend::Config cfg;
    #ifdef Q_OS_WIN32
        cfg.gdb = Common::applicationDataPath()
            + (settings.value("mode", QString("x64")).toString() == "x86" ? "/MinGW/bin/gdb.exe"
                                                                           : "/MinGW64/bin/gdb.exe");
        cfg.windows = true;
    #else
        cfg.gdb = "gdb";
        cfg.windows = false;
    #endif
    cfg.executable = Common::pathInTemp("AsmonProg.exe");
    const QString codePath = currentTab->getCurrentFilePath();
    cfg.workingDir = codePath.isEmpty() ? Common::pathInTemp("") : QFileInfo(codePath).absolutePath();
    cfg.workingDir.replace("\\", "/");
    currentTab->saveInputToFile(Common::pathInTemp("input.txt"));
    cfg.inputFile = Common::pathInTemp("input.txt");
    if (viaWsl) {
        //! gdb runs inside WSL in the temp folder: Linux-relative paths only.
        cfg.gdb = "wsl.exe";
        cfg.gdbPrefixArgs = QStringList() << "--cd" << QDir::toNativeSeparators(Common::pathInTemp(""))
                                          << "--exec" << "gdb";
        cfg.windows = false;
        cfg.executable = "./AsmonProg.exe";
        cfg.workingDir = ".";
        cfg.inputFile = "input.txt";
    }
    cfg.listing = Common::pathInTemp("program.lst");
    cfg.assembler = assembler;
    addProjectModules(cfg, currentTab);
    Q_UNUSED(code)
    launchDebugger(cfg, currentTab);
}

//! Asmon: the project's other files are debugged too (module N = module_N.lst).
void MainWindow::addProjectModules(DebugBackend::Config &cfg, Tab *tab)
{
    debugModulePaths = projectModules(tab);
    if (!debugModulePaths.isEmpty() && debugModulePaths.first().isEmpty())
        debugModulePaths.clear();
    for (int i = 0; i < debugModulePaths.size(); ++i)
        cfg.modules << DebugBackend::Config::Module{ debugModulePaths.at(i),
                                                   Common::pathInTemp(QString("module_%1.lst").arg(i + 1)) };
}

int MainWindow::debugFileIndex(Tab *tab) const
{
    if (!tab)
        return -1;
    if (tab == debugMainTab)
        return 0;
    const QString path = tab->getCurrentFilePath();
    if (path.isEmpty())
        return -1;
    const int i = debugModulePaths.indexOf(QFileInfo(path).absoluteFilePath());
    return i < 0 ? -1 : i + 1;
}

//! Breakpoints, conditions and "add watch" of a tab that belongs to the
//! debugged program (the main file or a module). Returns its breakpoint ids.
QList<int> MainWindow::connectDebugTab(Tab *tab)
{
    QList<int> ids;
    const int index = debugFileIndex(tab);
    if (!debugger || index < 0 || tab->property("asmonDebugFile").isValid())
        return ids;
    tab->setProperty("asmonDebugFile", index);
    const int base = index * DebugBackend::kModuleLineBase;
    CodeEditor *code = tab->code;
    connect(code, &CodeEditor::breakpointsChanged, debugger, [this, base](quint64 line, bool on) {
        if (debugger)
            debugger->setBreakpoint(base + int(line), on);
    });
    connect(code, &RuQPlainTextEdit::addWatchSignal, debugPanels, &DebugPanels::addWatch);
    code->setDebugEnabled();
    for (auto it = code->breakpointConditions().constBegin(); it != code->breakpointConditions().constEnd(); ++it)
        debugger->setBreakpointCondition(base + it.key(), it.value());
    for (quint64 l : *(code->getBreakpoints()))
        ids << base + int(l);
    return ids;
}

//! Shows a line id of the debugger (main file or module) and marks it.
void MainWindow::showDebugLine(int id)
{
    const int index = id / DebugBackend::kModuleLineBase;
    const int line = id % DebugBackend::kModuleLineBase;
    Tab *target = nullptr;
    if (index == 0) {
        target = debugMainTab;
        if (target)
            tabs->setCurrentWidget(target);
    } else if (index - 1 < debugModulePaths.size()) {
        openFile(debugModulePaths.at(index - 1));
        target = (Tab *) tabs->currentWidget();
        if (debugger) {
            for (int bp : connectDebugTab(target))
                debugger->setBreakpoint(bp, true);
        }
    }
    if (!target)
        return;
    if (debugLineTab && debugLineTab != target)
        debugLineTab->code->updateDebugLine(0);
    debugLineTab = target;
    target->code->updateDebugLine(line);
}

QString MainWindow::debugLineName(int id) const
{
    const int index = id / DebugBackend::kModuleLineBase;
    const int line = id % DebugBackend::kModuleLineBase;
    if (index == 0)
        return tr("line %1").arg(line);
    return tr("%1, line %2").arg(QFileInfo(debugModulePaths.value(index - 1)).fileName()).arg(line);
}

//! Shared by Debug (F5) and Attach to process: panels, signals, breakpoints.
void MainWindow::launchDebugger(const DebugBackend::Config &cfg, Tab *tab)
{
    CodeEditor *code = tab->code;
    debugMainTab = tab;
    debugLineTab = tab;
    //! Native Windows programs (64-bit) are debugged by Asmon's own engine; gdb stays for everything
    //! else, and can be chosen for everything with the setting "debugengine" = gdb (or the
    //! environment variable ASMON_DEBUG_ENGINE).
    debugger = nullptr;
#ifdef Q_OS_WIN
    const QString engine = qEnvironmentVariable("ASMON_DEBUG_ENGINE", settings.value("debugengine", "native").toString());
    if (engine != "gdb" && NativeDebugger::canDebug(cfg))
        debugger = new NativeDebugger(cfg, this);
#endif
    if (!debugger)
        debugger = new MiDebugger(cfg, this);
    printLog(tr("Debugger: %1").arg(debugger->engineName()) + '\n', Theme::color("fontcolor"));
    connect(debugger, &DebugBackend::stopped, this, &MainWindow::onDebugStopped);
    connect(debugger, &DebugBackend::crashed, this, &MainWindow::onDebugCrashed);
    connect(debugger, &DebugBackend::finished, this, &MainWindow::onDebugFinished);
    connect(debugger, &DebugBackend::running, this, &MainWindow::onDebugRunning);
    connect(debugger, &DebugBackend::programOutput, this, [this](const QString &t) { printOutput(t); });
    connect(debugger, &DebugBackend::consoleOutput, this, [this](const QString &t) {
        appendDebugConsole(t, Theme::color("fontcolor"));
    });
    connect(debugger, &DebugBackend::log, this, [this](const QString &t, bool error) {
        printLog(t, Theme::color(error ? "errorcolor" : "fontcolor"));
    });
    connect(debugger, &DebugBackend::ended, this, &MainWindow::debugExit, Qt::QueuedConnection);
    debugPanels->attach(debugger);

    enableDebugActions();
    if (debugger->hasConsole())
        showAnyCommandWidget();
    else
        closeAnyCommandWidget();
    debugConsole->clear();
    syncBottomPanelWithTab();
    showBottomPage(outputStack);
    //! Starting a debug session switches to the Debug mode (which shows the debugger panel);
    //! when the session ends the window goes back to where it was.
    debugModeAuto = modeBar->mode() != ModeBar::Debug;
    modeBar->setMode(ModeBar::Debug);
    debugDock->show();
    debugDock->raise();
    //! Asmon: on a small screen the debugger panel must not squeeze the code
    //! away. Keep it at most ~40% of the window (the user can still drag it).
    QTimer::singleShot(0, this, [this]() {
        if (debugDock && !debugDock->isFloating() && debugDock->width() > width() * 45 / 100)
            resizeDocks({ debugDock }, { width() * 40 / 100 }, Qt::Horizontal);
    });

    //! the main file and every open module of the project
    QList<int> lines;
    for (int i = 0; i < tabs->count(); ++i)
        lines << connectDebugTab((Tab *) tabs->widget(i));
    Q_UNUSED(code)
    debugger->start(lines);
}

void MainWindow::attachToProcess()
{
    if (debugger) {
        printLogWithTime(tr("A debugging session is already running.") + '\n', Theme::color("errorcolor"));
        return;
    }
    const QString built = QFileInfo(Common::pathInTemp("AsmonProg.exe")).absoluteFilePath();
    ProcessPicker picker(built, this);
    if (picker.exec() != QDialog::Accepted || picker.selectedPid() <= 0)
        return;
    if (!tabs || tabs->count() == 0)
        newFile();
    //! a module of a project: the program was built from the main file
    const QString mainPath = projectMainFile((Tab *) tabs->currentWidget());
    if (!mainPath.isEmpty())
        openFile(mainPath);
    Tab *tab = (Tab *) tabs->currentWidget();

    DebugBackend::Config cfg;
    #ifdef Q_OS_WIN32
        cfg.gdb = Common::applicationDataPath()
            + (settings.value("mode", QString("x64")).toString() == "x86" ? "/MinGW/bin/gdb.exe"
                                                                           : "/MinGW64/bin/gdb.exe");
        cfg.windows = true;
    #else
        cfg.gdb = "gdb";
        cfg.windows = false;
    #endif
    cfg.attachPid = picker.selectedPid();
    cfg.executable = picker.selectedPath();
    cfg.workingDir = picker.selectedPath().isEmpty() ? QDir::homePath()
                                                     : QFileInfo(picker.selectedPath()).absolutePath();
    //! the program built from this tab: lines, breakpoints and steps work too
    const bool ours = programIsBuilded
        && QFileInfo(picker.selectedPath()).absoluteFilePath() == built;
    debugModulePaths.clear();
    if (ours) {
        cfg.listing = Common::pathInTemp("program.lst");
        cfg.assembler = assembler;
        addProjectModules(cfg, tab);
    }
    printLogWithTime(tr("Attaching to process %1 (%2)...").arg(picker.selectedPid())
                         .arg(QDir::toNativeSeparators(picker.selectedPath())) + '\n',
                     Theme::color("successcolor"));
    if (!ours)
        printLog(tr("This is not the program built from the open file: debugging by instructions "
                    "(Disassembly, registers, memory), without source lines.") + '\n',
                 Theme::color("fontcolor"));
    tab->clearOutput();
    launchDebugger(cfg, tab);
}

void MainWindow::onDebugStopped(quint64 pc, int line)
{
    Common::testLog(QString("debug-stopped line=%1 pc=0x%2").arg(line).arg(pc, 0, 16));
    QTimer::singleShot(0, this, &MainWindow::updateStatusBar);
    if (!debugger)
        return;
    changeDebugActionToStart();
    if (line > 0) {
        showDebugLine(line);
    } else {
        if (debugLineTab)
            debugLineTab->code->updateDebugLine(-2);
        printLogWithTime(tr("Stopped outside your code at 0x%1 (see Disassembly).").arg(pc, 0, 16) + '\n',
                         Theme::color("fontcolor"));
        debugPanels->showTab(DebugPanels::DisassemblyTab);
    }
}

void MainWindow::onDebugCrashed(const QString &signal, const QString &meaning, quint64 pc, int line)
{
    Common::testLog(QString("debug-crashed line=%1 signal=%2").arg(line).arg(signal));
    changeDebugActionToStart();
    if (line > 0)
        showDebugLine(line);
    const QString where = line > 0 ? debugLineName(line) : QString("0x%1").arg(pc, 0, 16);
    printLogWithTime(tr("The program crashed: %1 (%2) at %3. Registers and memory show the state at the crash; "
                        "Stop ends the session.").arg(signal, meaning, where) + '\n',
                     Theme::color("errorcolor"));
    debugPanels->showTab(DebugPanels::RegistersTab);
}

void MainWindow::onDebugFinished(int exitCode)
{
    printLogWithTime(tr("The program finished with exit code %1.").arg(exitCode) + '\n',
                     Theme::color(exitCode == 0 ? "successcolor" : "errorcolor"));
}

void MainWindow::onDebugRunning()
{
    QTimer::singleShot(0, this, &MainWindow::updateStatusBar);
    debugAction->setText(tr("Pause"));
    debugAction->setIcon(Theme::icon("pause"));
    debugAction->setProperty("asmonIcon", "pause");
    //! F5 stays active while running: it pauses the program.
    if (debugLineTab)
        debugLineTab->code->updateDebugLine(-2);
}

void MainWindow::changeDebugActionToStart()
{
    debugAction->setText(tr("Continue"));
    debugAction->setIcon(Theme::icon("continue"));
    debugAction->setProperty("asmonIcon", "continue");
    debugAction->setShortcut(debugKey);
}

void MainWindow::enableDebugActions()
{
    changeDebugActionToStart();

    debugNextAction->setEnabled(true);
    debugNextNiAction->setEnabled(true);
    debugStepOutAction->setEnabled(true);
    debugStepInsnAction->setEnabled(true);
    debugShowRegistersAction->setEnabled(true);
    debugShowMemoryAction->setEnabled(true);
    stopAction->setEnabled(true);

    //! Change stopAction
    disconnect(stopAction, SIGNAL(triggered()), this, SLOT(stopProgram()));
    connect(stopAction, SIGNAL(triggered()), this, SLOT(debugExit()));

    //! Block run and build
    buildAction->setEnabled(false);
    runAction->setEnabled(false);

}

void MainWindow::disableDebugActions(bool start)
{
    debugAction->setText(tr("Debug"));
    debugAction->setIcon(Theme::icon("debug"));
    debugAction->setProperty("asmonIcon", "debug");
    debugAction->setShortcut(debugKey);

    debugNextAction->setEnabled(false);
    debugNextNiAction->setEnabled(false);
    if (debugStepOutAction) {
        debugStepOutAction->setEnabled(false);
        debugStepInsnAction->setEnabled(false);
    }
    debugShowRegistersAction->setEnabled(false);
    debugShowMemoryAction->setEnabled(false);
    stopAction->setEnabled(false);

    //! Change stopAction
    if (!start) {
        disconnect(stopAction, SIGNAL(triggered()), this, SLOT(debugExit()));
        connect(stopAction, SIGNAL(triggered()), this, SLOT(stopProgram()));
    }

    //!Enable run and build
    buildAction->setEnabled(true);
    runAction->setEnabled(true);
}

void MainWindow::debugNext()          // F11: step into
{
    if (debugger)
        debugger->stepInto();
}

void MainWindow::debugNextNi()        // F10: step over
{
    if (debugger)
        debugger->stepOver();
}

void MainWindow::debugStepOut()       // Shift+F11
{
    if (debugger)
        debugger->stepOut();
}

void MainWindow::debugStepInstruction()
{
    if (debugger)
        debugger->stepInstruction();
}

void MainWindow::debugToggleBreakpoint()
{
    CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
    code->setBreakpointOnCurrentLine();
}

void MainWindow::debugShowMemory()
{
    if (!debugDock)
        return;
    modeBar->setMode(ModeBar::Debug);
    debugDock->show();
    debugDock->raise();
    debugPanels->showTab(DebugPanels::MemoryTab);
}

void MainWindow::setShowMemoryToUnchecked()
{
}

void MainWindow::setShowMemoryToChecked(const RuQPlainTextEdit::Watch &variable)
{
    debugPanels->addWatch(variable);
}

void MainWindow::debugShowRegisters()
{
    if (!debugDock)
        return;
    modeBar->setMode(ModeBar::Debug);
    debugDock->show();
    debugDock->raise();
    debugPanels->showTab(DebugPanels::RegistersTab);
}

void MainWindow::setShowRegistersToUnchecked()
{
}

void MainWindow::debugExit()
{
    QTimer::singleShot(0, this, &MainWindow::updateStatusBar);
    if (!debugger)
        return;
    DebugBackend *d = debugger;
    debugger = 0;
    d->disconnect(this);
    d->stop();
    d->deleteLater();
    debugPanels->attach(nullptr);
    for (int i = 0; tabs && i < tabs->count(); ++i) {
        Tab *t = (Tab *) tabs->widget(i);
        if (!t->property("asmonDebugFile").isValid() && t != debugLineTab)
            continue;
        t->setProperty("asmonDebugFile", QVariant());
        t->code->disconnect(debugPanels);
        t->code->updateDebugLine(-1);
        t->code->setDebugDisabled();
    }
    debugMainTab = nullptr;
    debugLineTab = nullptr;
    closeAnyCommandWidget();
    if (debugModeAuto)
        modeBar->setMode(ModeBar::Write);   // hides the debugger panel through onModeChanged
    debugModeAuto = false;
    printLogWithTime(tr("Debugging finished.") + '\n', Theme::color("successcolor"));
    disableDebugActions();
    //! attached to a program started with Run: it keeps running, so Stop stays usable
    if (timer->isActive())
        stopAction->setEnabled(true);
}

void MainWindow::showAnyCommandWidget()
{
    debugAnyCommandWidget->show();
    debugAnyCommandWidget->setFocusOnLineEdit();
    connect(debugAnyCommandWidget, SIGNAL(performCommand(QString,bool)),
            this, SLOT(debugRunCommand(QString,bool)), Qt::UniqueConnection);
}

void MainWindow::closeAnyCommandWidget()
{
    disconnect(debugAnyCommandWidget, SIGNAL(performCommand(QString,bool)),
            this, SLOT(debugRunCommand(QString,bool)));
    debugAnyCommandWidget->close();
}

void MainWindow::debugRunCommand(QString command, bool print)
{
    if (!debugger || !debugger->isStopped())
        return;
    showBottomPage(debugConsole->parentWidget());
    appendDebugConsole("(gdb) " + command + "\n", Theme::color("keywordscolor"));
    debugger->consoleCommand(print ? "print " + command : command);
}

void MainWindow::find()
{
    if (!findDialog) {
        findDialog = new FindDialog(this);
        connect(findDialog, SIGNAL(findNext(QString,Qt::CaseSensitivity,bool,bool,QString)),
                this, SLOT(findNext(QString,Qt::CaseSensitivity,bool,bool,QString)));
    }
    CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
    QString selectedText = code->textCursor().selectedText();
    if (!selectedText.isEmpty()) {
        findDialog->setSearchText(selectedText);
    }
    findDialog->show();
    findDialog->activateWindow();
}

void MainWindow::findNextAfterTextChange()
{
    findNext(QString(), Qt::CaseSensitive, true, false);
}

void MainWindow::findNext(const QString &pattern, Qt::CaseSensitivity cs, bool all,
                          bool replace, const QString &replaceText)
{
    //! Clear all highlights and disable highlighting of current line
    for (int i = 0; i < tabs->count(); i++) {
        CodeEditor *code = ((Tab *) tabs->widget(i))->code;
        disconnect(code, SIGNAL(cursorPositionChanged()), code, SLOT(highlightCurrentLine()));
        code->setExtraSelections(QList<QTextEdit::ExtraSelection>());
    }
     //! Restore highlight
    if (pattern.isEmpty()) {

        for (int i = 0; i < tabs->count(); i++) {
            CodeEditor *code = ((Tab *) tabs->widget(i))->code;
            connect(code, SIGNAL(cursorPositionChanged()), code, SLOT(highlightCurrentLine()));
            code->highlightCurrentLine();
            if (code->debugMode)
                code->highlightDebugLine(code->currentDebugLine, false);
        }
    } else {
         //If true, find all
        if (all) {
            for (int i = 0; i < tabs->count(); i++) {
                QTextEdit::ExtraSelection selection;
                QList<QTextEdit::ExtraSelection> extraSelections;
                selection.format.setBackground(QBrush(Qt::green));
                QTextDocument *document = ((Tab *) tabs->widget(i))->getCodeDocument();
                QTextCursor newCursor = QTextCursor(document);
                CodeEditor *code = ((Tab *) tabs->widget(i))->code;

                while (!newCursor.isNull() && !newCursor.atEnd()) {
                    if (cs == Qt::CaseSensitive)
                        newCursor = document->find(pattern, newCursor, QTextDocument::FindCaseSensitively);
                    else
                        newCursor = document->find(pattern, newCursor, QTextDocument::FindFlags());
                    //! Replace mode
                    if (replace && i == tabs->currentIndex()) {
                        newCursor.removeSelectedText();
                        newCursor.insertText(replaceText);
                    }
                    if (!newCursor.isNull()) {
                        selection.cursor = newCursor;
                        extraSelections.append(selection);
                    }
                }

                //! Highlight all
                code->setExtraSelections(extraSelections);
            }
        }
       //!Find next only
        else {
            QTextEdit::ExtraSelection selection;
            QList<QTextEdit::ExtraSelection> extraSelections;
            selection.format.setBackground(QBrush(Qt::green));
            QTextDocument *document = ((Tab *) tabs->currentWidget())->getCodeDocument();
            CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
            static QTextCursor newCursor(document);
            //! if documents differ, cursor is ignored in QTextDocument::find()
            if (replace) {
                newCursor.removeSelectedText();
                newCursor.insertText(replaceText);
            }
            if (cs == Qt::CaseSensitive)
                newCursor = document->find(pattern, newCursor, QTextDocument::FindCaseSensitively);
            else
                newCursor = document->find(pattern, newCursor, QTextDocument::FindFlags());
             //! Continue from start
            if (newCursor.isNull()) {
                if (cs == Qt::CaseSensitive)
                    newCursor = document->find(pattern, newCursor, QTextDocument::FindCaseSensitively);
                else
                    newCursor = document->find(pattern, newCursor, QTextDocument::FindFlags());
            }
            if (!newCursor.isNull()) {
                selection.cursor = newCursor;
                extraSelections.append(selection);
                QTextCursor cursor = newCursor;
                cursor.clearSelection();
                code->setTextCursor(cursor);
            }

            code->setExtraSelections(extraSelections);
        }
    }
}

void MainWindow::restorePrevSession(bool notNotify)
{
    int count = settings.value("tabscount", 0).toInt();
    if (count == 0 && !notNotify) {
        QMessageBox::information(0, QString("Asmon"),
                                 tr("In the previous session was not open any of the saved tabs!"));
        return;
    }
    settings.beginGroup("Tabs");
    for (int i = 0; i < count; i++) {
        QString fileName = settings.value(QString::number(i)).toString();
        newFile();
        Tab *curTab = (Tab *) tabs->currentWidget();
        curTab->loadCodeFromFile(fileName);
        setTabPath(fileName);
    }
    settings.endGroup();
}

void MainWindow::writeSettings()
{
    //! GUI
    settings.beginGroup("MainWindow");
    settings.setValue("size", size());
    settings.setValue("pos", pos());
    unsigned int maxFlag = windowState() & Qt::WindowMaximized;
    settings.setValue("maximized", maxFlag);
    settings.endGroup();
    settings.setValue("windowstate", saveState());

    //! Opened tabs
    int prevTabsCount = settings.value("tabscount", 0).toInt();
    //! Remove previous
    for (int i = 0; i < prevTabsCount; i++)
    {
        settings.remove(QString("Tabs/") + QString::number(i));
    }
    //! Create current
    settings.setValue("tabscount", tabs->count());
    settings.beginGroup("Tabs");
    for (int i = 0; i < tabs->count(); i++) {
        settings.setValue(QString::number(i), ((Tab *) tabs->widget(i))->getCurrentFilePath());
    }
    settings.endGroup();

    //! Save and open directory
    settings.setValue("saveopendirectory", saveOpenDirectory);

    //! Splitters state
    settings.setValue("panelsplitterstate", splitter->saveState());
}

void MainWindow::openSettings()
{
    //! Initialize
    if (!settingsWindow) {
        //! Asmon: settings open as a full page in the editor area (like VS Code), not a small window.
        settingsWindow = new QWidget;
        settingsUi.setupUi(settingsWindow);
        settingsWindow->setMaximumWidth(1100);
        {
            //! Font list: monospaced families only, plain names in the interface font (no giant
            //! per-font previews or icons); the preview box below shows the chosen font.
            struct PlainDelegate : QStyledItemDelegate {
                using QStyledItemDelegate::QStyledItemDelegate;
                void initStyleOption(QStyleOptionViewItem *o, const QModelIndex &i) const override {
                    QStyledItemDelegate::initStyleOption(o, i);
                    o->icon = QIcon();
                    o->features &= ~QStyleOptionViewItem::HasDecoration;
                    o->font = QApplication::font();
                    o->textElideMode = Qt::ElideRight;
                }
                QSize sizeHint(const QStyleOptionViewItem &o, const QModelIndex &i) const override {
                    QSize s = QStyledItemDelegate::sizeHint(o, i);
                    s.setHeight(26);
                    return s;
                }
            };
            settingsUi.fontComboBox->setItemDelegate(new PlainDelegate(settingsUi.fontComboBox));
            settingsUi.fontComboBox->setMaxVisibleItems(12);
            settingsUi.fontComboBox->setMinimumWidth(240);
        }
        settingsPage = new QWidget;
        QHBoxLayout *pageLayout = new QHBoxLayout(settingsPage);
        pageLayout->setContentsMargins(16, 8, 16, 12);
        pageLayout->addStretch(1);
        pageLayout->addWidget(new CardFrame(settingsWindow), 100); // the whole page is one rounded card
        pageLayout->addStretch(1);
        mainWidget->addWidget(settingsPage);
        auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), settingsPage);
        escape->setContext(Qt::WidgetWithChildrenShortcut);
        connect(escape, SIGNAL(activated()), this, SLOT(restoreSettingsAndExit()));
        settingsUi.tabWidget->setLayoutDirection(QApplication::layoutDirection());
        settingsUi.tabWidget->setCurrentIndex(0);
        //! the old CLI debugger's "show all registers" switch has no effect any more
        for (QWidget *w : { (QWidget *) settingsUi.registersYesRadioButton,
                            (QWidget *) settingsUi.registersNoRadioButton })
            w->hide();
        settingsUi.registersLabel_2->hide();
        createShortcutsPage();
        {
            QPushButton *openIni = new QPushButton(tr("Open the settings file"));
            openIni->setToolTip(QDir::toNativeSeparators(settings.fileName()));
            connect(openIni, &QPushButton::clicked, this, [this]() {
                settings.sync();
                closeSettingsPage();
                openFile(settings.fileName());
            });
            settingsUi.buttonBox->addButton(openIni, QDialogButtonBox::ResetRole);
        }
        connect(settingsUi.buttonBox->button(QDialogButtonBox::Ok),
                SIGNAL(clicked()), this, SLOT(exitSettings()));
        connect(settingsUi.buttonBox->button(QDialogButtonBox::Apply),
                SIGNAL(clicked()), this, SLOT(saveSettings()));
        connect(settingsUi.buttonBox->button(QDialogButtonBox::Cancel),
                SIGNAL(clicked()), this, SLOT(restoreSettingsAndExit()));
        connect(settingsUi.resetAllButton, SIGNAL(clicked()), this, SLOT(resetAllSettings()));
        connect(settingsUi.disableLinkingCheckbox, SIGNAL(stateChanged(int)),
                this, SLOT(enableOrDisableLinkingEdit(int)));

        //! Colors
        colorSignalMapper = new QSignalMapper(this);
        colorButtons <<
                        //! Foreground
                        settingsUi.keywordsColorButton << settingsUi.registersColorButton <<
                        settingsUi.numbersColorButton << settingsUi.memoryColorButton <<
                        settingsUi.labelsColorButton << settingsUi.commentsColorButton <<
                        settingsUi.systemColorButton << settingsUi.iomacroColorButton <<
                        settingsUi.quotationColorButton <<
                        //! Background
                        settingsUi.keywordsColorButton_2 << settingsUi.registersColorButton_2 <<
                        settingsUi.numbersColorButton_2 << settingsUi.memoryColorButton_2 <<
                        settingsUi.labelsColorButton_2 << settingsUi.commentsColorButton_2 <<
                        settingsUi.systemColorButton_2 << settingsUi.iomacroColorButton_2 <<
                        settingsUi.quotationColorButton_2 <<
                        //! Common
                        settingsUi.backgroundColorButton << settingsUi.lineNumberPanelColorButton <<
                        settingsUi.fontColorButton <<
                        settingsUi.currentLineColorButton << settingsUi.debugLineColorButton <<
                        settingsUi.lineNumberFontColorButton;

        //! Add color buttons signal mapping
        for (int i = 0; i < colorButtons.size(); i++) {
            connect(colorButtons[i], SIGNAL(clicked()), colorSignalMapper, SLOT(map()));
            colorSignalMapper->setMapping(colorButtons[i], colorButtons[i]);
        }
        connect(colorSignalMapper, SIGNAL(mapped(QWidget*)), this, SLOT(pickColor(QWidget*)));

        //! Fonts
        fontsSignalMapper = new QSignalMapper(this);
        fontCheckBoxes <<
                          //! Bold
                          settingsUi.keywordsBoldCheckBox << settingsUi.registersBoldCheckBox <<
                          settingsUi.numbersBoldCheckBox << settingsUi.memoryBoldCheckBox <<
                          settingsUi.labelsBoldCheckBox << settingsUi.commentsBoldCheckBox <<
                          settingsUi.systemBoldCheckBox << settingsUi.iomacroBoldCheckBox <<
                          settingsUi.quotationBoldCheckBox <<
                          //! Italic
                          settingsUi.keywordsItalicCheckBox << settingsUi.registersItalicCheckBox <<
                          settingsUi.numbersItalicCheckBox << settingsUi.memoryItalicCheckBox <<
                          settingsUi.labelsItalicCheckBox << settingsUi.commentsItalicCheckBox <<
                          settingsUi.systemItalicCheckBox << settingsUi.iomacroItalicCheckBox <<
                          settingsUi.quotationItalicCheckBox;
         //! Add font to associative array
        for (int i = 0; i < fontCheckBoxes.size(); i++) {

            QString name = fontCheckBoxes[i]->objectName();
            name.remove("CheckBox");
            name = name.toLower();

            connect(fontCheckBoxes[i], SIGNAL(clicked()), fontsSignalMapper, SLOT(map()));
            fontsSignalMapper->setMapping(fontCheckBoxes[i], fontCheckBoxes[i]);
        }
        connect(fontsSignalMapper, SIGNAL(mapped(QWidget*)), this, SLOT(changeHighlightingFont(QWidget*)));

        connect(settingsUi.currentLineCheckBox, SIGNAL(stateChanged(int)), this, SLOT(changeHighlightingLineMode(int)));

        connect(settingsUi.lightThemeButton, SIGNAL(clicked()), this, SLOT(changeColorButtonsLight()));
        connect(settingsUi.darkThemeButton, SIGNAL(clicked()), this, SLOT(changeColorButtonsDark()));
        //! Colour themes imported from Notepad++ (GPL-3): pick one and the editor colours switch at once.
        {
            QComboBox *themeCombo = new QComboBox;
            themeCombo->addItem(tr("Notepad++ themes..."));
            for (int i = 0; i < kNppThemeCount; i++)
                themeCombo->addItem(QString::fromUtf8(kNppThemes[i].name));
            settingsUi.horizontalLayout_10->addWidget(themeCombo);
            connect(themeCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
                if (idx <= 0)
                    return;
                applyNppTheme(idx - 1);
            });
        }

        backupSettings();
        initAssemblerSettings(true);
    } else {
        initAssemblerSettings(false);
    }

    //! Set settings
    //! Common
    settingsUi.startWindow->setCurrentIndex(settings.value("startwindow", 0).toInt());
    settingsUi.language->setCurrentIndex(settings.value("language", 0).toInt());
    {
        //! monospaced families only, plus whatever is stored (never silently switch the user's font)
        const QString stored = settings.value("fontfamily", Theme::defaultCodeFont()).toString();
        QStringList list;
        for (const QString &f : QFontDatabase::families())
            if (QFontDatabase::isFixedPitch(f) && !f.startsWith('@'))
                list << f;
        for (const char *f : { "JetBrains Mono", "Cascadia Code", "Cascadia Mono", "Source Code Pro",
                               "Hack", "Fira Code", "Consolas" })
            if (QFontDatabase::families().contains(QString(f)) && !list.contains(QString(f)))
                list << QString(f);
        if (!list.contains(stored))
            list << stored;
        list.removeDuplicates();
        list.sort(Qt::CaseInsensitive);
        settingsUi.fontComboBox->clear();
        settingsUi.fontComboBox->addItems(list);
        settingsUi.fontComboBox->setCurrentIndex(qMax(0, list.indexOf(stored)));
    }
    settingsUi.fontSizeSpinBox->setValue(settings.value("fontsize", 11).toInt());
    if (settings.value("allregisters", false).toBool())
        settingsUi.registersYesRadioButton->setChecked(true);
    else
        settingsUi.registersNoRadioButton->setChecked(true);
    settingsUi.insertDebugStringCheckBox->setChecked(settings.value("insertdebugstring", false).toBool());

    //! Colors
    initColorButtons(Theme::isDark(), false);
    //! Fonts
    for (int i = 0; i < fontCheckBoxes.size(); i++) {
        changeHighlightingFont((QWidget *) fontCheckBoxes[i], true);
    }

    settingsUi.currentLineCheckBox->setChecked(settings.value("currentlinemode", true).toBool());
    fillShortcutsPage();
    if (mainWidget->currentWidget() != settingsPage) {
        settingsReturnIndex = mainWidget->currentIndex();
        mainWidget->setCurrentWidget(settingsPage);
    }
}

void MainWindow::closeSettingsPage()
{
    if (!settingsPage || mainWidget->currentWidget() != settingsPage)
        return;
    const int back = (settingsReturnIndex >= 0 && settingsReturnIndex < mainWidget->count()
                      && mainWidget->widget(settingsReturnIndex) != settingsPage) ? settingsReturnIndex : 0;
    mainWidget->setCurrentIndex(back);
}

void MainWindow::initColorButtons(bool dark, bool ignoreSettings) {
    defaultColors.clear();
    if (dark) {
        if (ignoreSettings)
            settingsUi.currentLineCheckBox->setChecked(false);
        defaultColors <<
            //! According to colorButtons
            QColor(181, 206, 168) << QColor(144, 220, 254) <<
            QColor(181, 206, 168) << QColor(212, 212, 212) <<
            QColor(244, 71, 71) << QColor(106, 153, 85) <<
            QColor(78, 201, 176) << QColor(28, 113, 216) <<
            QColor(195, 145, 120) <<
            QColor(30, 30, 30) << QColor(30, 30, 30) <<
            QColor(30, 30, 30) << QColor(30, 30, 30) <<
            QColor(30, 30, 30) << QColor(30, 30, 30) <<
            QColor(30, 30, 30) << QColor(30, 30, 30) <<
            QColor(30, 30, 30) <<
            QColor(30, 30, 30) << QColor(50, 50, 50) <<
            QColor(255, 255, 255) <<
            QColor(232, 232, 255) << QColor(100, 78, 0) <<
            QColor(255, 255, 255);
    } else {
        if (ignoreSettings)
            settingsUi.currentLineCheckBox->setChecked(true);
        defaultColors <<
            //! According to colorButtons
            QColor(Qt::blue) << QColor(153, 0, 204) <<
            QColor(255, 122, 0) << QColor(0, 128, 255) <<
            QColor(128, 0, 0) << QColor(Qt::darkGreen) <<
            QColor(Qt::darkCyan) << QColor(Qt::blue) <<
            QColor(128, 128, 128) <<
            QColor(255, 255, 255) << QColor(255, 255, 255) <<
            QColor(255, 255, 255) << QColor(255, 255, 255) <<
            QColor(255, 255, 255) << QColor(255, 255, 255) <<
            QColor(255, 255, 255) << QColor(255, 255, 255) <<
            QColor(255, 255, 255) <<
            QColor(255, 255, 255) << QColor(235, 235, 235) <<
            QColor(30, 30, 30) <<
            QColor(232, 232, 255) << QColor(235, 200, 40) <<
            QColor(30, 30, 30);
    }

    //! Add color to associative array
    for (int i = 0; i < colorButtons.size(); i++) {
        QString name = colorButtons[i]->objectName();
        name.remove("Button");
        name.replace("_2", "bg");
        name = name.toLower();
        colorsMap.insert(name, Theme::editorDefaults(dark).value(name, defaultColors[i]));
    }

    //! Colors
    for (int i = 0; i < colorButtons.size(); i++) {
        pickColor((QWidget *) colorButtons[i], true, ignoreSettings);
    }
}

void MainWindow::changeColorButtonsLight() {
    Theme::setTheme(settings, false);
    initColorButtons(false, true);
    Theme::apply(*qApp);
}

void MainWindow::changeColorButtonsDark() {
    Theme::setTheme(settings, true);
    initColorButtons(true, true);
    Theme::apply(*qApp);
}

void MainWindow::initAssemblerSettings(bool firstOpening)
{
    if (firstOpening) {
        /*!
         *
         *  Assembler Dependent Options Begin
         *
         */
        connect(settingsUi.nasmRadioButton, SIGNAL(clicked()), this, SLOT(changeAssembler()));
        connect(settingsUi.gasRadioButton, SIGNAL(clicked()), this, SLOT(changeAssembler()));
        connect(settingsUi.fasmRadioButton, SIGNAL(clicked()), this, SLOT(changeAssembler()));
        #ifdef Q_OS_WIN32
            connect(settingsUi.masmRadioButton, SIGNAL(clicked()), this, SLOT(changeAssembler()));
            connect(settingsUi.masmRadioButton, SIGNAL(clicked()), this, SLOT(printMasmInfo()));
        //! Else it is Linux
        #else
            settingsUi.masmRadioButton->setEnabled(false);
        #endif

        //! Asmon: NASM is the default; other assemblers are an advanced option.
        {
            QCheckBox *advanced = new QCheckBox(tr("Show other assemblers (advanced)"));
            settingsUi.horizontalLayout_6->insertWidget(1, advanced);
            QList<QWidget *> others;
            others << settingsUi.gasRadioButton << settingsUi.fasmRadioButton << settingsUi.masmRadioButton;
            const bool show = settings.value("showallassemblers", false).toBool()
                              || settings.value("assembler", QString("NASM")).toString() != "NASM";
            advanced->setChecked(show);
            for (QWidget *w : others)
                w->setVisible(show);
            connect(advanced, &QCheckBox::toggled, this, [this, others](bool on) {
                settings.setValue("showallassemblers", on);
                for (QWidget *w : others)
                    w->setVisible(on);
            });
        }

            /*!
             *
             *  Assembler Dependent Options End
             *
             */

        connect(settingsUi.x86RadioButton, SIGNAL(toggled(bool)), this, SLOT(changeMode(bool)));

        //! Start text editor
        settingsStartTextEditor = new CodeEditor(0, false);
        settingsHighlighter = new Highlighter(assembler);
        settingsHighlighter->setDocument(settingsStartTextEditor->document());
        QVBoxLayout *startTextLayout = new QVBoxLayout;
        startTextLayout->addWidget(settingsStartTextEditor);
        settingsStartTextEditor->setFixedHeight(120);
        settingsUi.startTextWidget->setLayout(startTextLayout);
    }

    settingsStartTextEditor->setPlainText(settings.value("starttext").toString());

    //! Build options
    QString options = assembler->getAssemblerOptions();
    settingsUi.assemblyOptionsEdit->setText(settings.value("assemblyoptions", options).toString());

    QString linkerOptions = assembler->getLinkerOptions();
    settingsUi.linkingOptionsEdit->setText(settings.value("linkingoptions", linkerOptions).toString());

    settingsUi.objectFileNameEdit->setText(settings.value("objectfilename", "program.o").toString());

    settingsUi.disableLinkingCheckbox->setChecked(settings.value("disablelinking", false).toBool());

    settingsUi.runInCurrentDirectoryCheckbox->setChecked(settings.value("currentdir", false).toBool());

    QString assemblerPath = assembler->getAssemblerPath();
    settingsUi.assemblerPathEdit->setText(settings.value("assemblerpath", assemblerPath).toString());

    #ifdef Q_OS_WIN32
        settingsUi.linkerPathEdit->setText(settings.value("linkerpath", Common::applicationDataPath() + "/MinGW/bin/gcc.exe").toString());
    #else
        settingsUi.linkerPathEdit->setText(settings.value("linkerpath", "gcc").toString());
    #endif

    disconnect(settingsUi.x86RadioButton, SIGNAL(toggled(bool)), this, SLOT(changeMode(bool)));
    //! Mode
    if (assembler->isx86())
        settingsUi.x86RadioButton->setChecked(true);
    else
        settingsUi.x64RadioButton->setChecked(true);
    connect(settingsUi.x86RadioButton, SIGNAL(toggled(bool)), this, SLOT(changeMode(bool)));

    /*!
     *
     *  Assembler Dependent Options Begin
     *
     */

    QString assemblerName = settings.value("assembler", QString("NASM")).toString();
    if (assemblerName == "MASM") {
        settingsUi.x64RadioButton->setEnabled(false);
        printMasmInfo();
    }
    if (assemblerName == "NASM") {
        settingsUi.nasmRadioButton->setChecked(true);
    } else if (assemblerName == "GAS") {
        settingsUi.gasRadioButton->setChecked(true);
    } else if (assemblerName == "FASM") {
        settingsUi.fasmRadioButton->setChecked(true);
    } else if (assemblerName == "MASM") {
        settingsUi.masmRadioButton->setChecked(true);
    }
}

/*!
 *
 *  Assembler Dependent Options End
 *
 */

void MainWindow::printMasmInfo()
{
    settingsUi.infoLabel->setText(tr("Please set path to MASM assembler (ml.exe) and linker (link.exe) on your computer ") +
                                  tr("in fields \"Assembler path\" and \"Linker path\" above."));
}
/*!
 *
 *  Assembler Dependent Options Begin
 *
 */
void MainWindow::changeAssembler()
{
    if (settingsUi.nasmRadioButton->isChecked())
        settings.setValue("assembler", QString("NASM"));
    else if (settingsUi.gasRadioButton->isChecked()) {
        settings.setValue("assembler", QString("GAS"));
    } else if (settingsUi.fasmRadioButton->isChecked()) {
        settings.setValue("assembler", QString("FASM"));
    } else if (settingsUi.masmRadioButton->isChecked()) {
        settings.setValue("assembler", QString("MASM"));
    }
    recreateAssembler();
}

/*!
 *
 *  Assembler Dependent Options End
 *
 */

void MainWindow::changeMode(bool x86)
{
    if (x86)
        settings.setValue("mode", QString("x86"));
    else
        settings.setValue("mode", QString("x64"));
    recreateAssembler();
}

void MainWindow::recreateAssembler(bool start)
{
    if (assembler) {
        delete assembler;
        assembler = 0;
    }
    bool x86 = true;
    if (settings.value("mode", QString("x64")).toString() != "x86")
        x86 = false;
    /*!
     *
     *  Assembler Dependent Options Begin
     *
     */

    QString assemblerName = settings.value("assembler", QString("NASM")).toString();
    if (!start) {
        if (assemblerName == "MASM") {
            settingsUi.x64RadioButton->setEnabled(false);
            if (settingsUi.x64RadioButton->isChecked()) {
                settingsUi.x86RadioButton->setChecked(true);
                return;
            }
        } else {
            settingsUi.x64RadioButton->setEnabled(true);
            settingsUi.infoLabel->setText("");
        }
    }
    if (assemblerName == "NASM") {
        assembler = new NASM(x86);
    } else if (assemblerName == "GAS") {
        assembler = new GAS(x86);
    } else if (assemblerName == "FASM") {
        assembler = new FASM(x86);
    } else if (assemblerName == "MASM") {
        assembler = new MASM(x86);
    }

    /*!
     *
     *  Assembler Dependent Options End
     *
     */


    if (!start) {
        settingsUi.assemblyOptionsEdit->setText(assembler->getAssemblerOptions());
        settingsUi.linkingOptionsEdit->setText(assembler->getLinkerOptions());
        settingsUi.objectFileNameEdit->setText("program.o");
        settingsUi.disableLinkingCheckbox->setChecked(false);
        settingsUi.runInCurrentDirectoryCheckbox->setChecked(false);
        settingsUi.assemblerPathEdit->setText(assembler->getAssemblerPath());
        settingsUi.linkerPathEdit->setText(assembler->getLinkerPath());
        settings.setValue("assemblyoptions", assembler->getAssemblerOptions());
        settings.setValue("linkingoptions", assembler->getLinkerOptions());
        settings.setValue("objectfilename", "program.o");
        settings.setValue("disablelinking", false);
        settings.setValue("currentdir", false);
        settings.setValue("assemblerpath", assembler->getAssemblerPath());
        settings.setValue("linkerpath", assembler->getLinkerPath());
        changeStartText();
        recreateHighlighter();
    }
}

void MainWindow::changeStartText()
{
    settingsStartTextEditor->setPlainText(assembler->getStartText());
    settings.setValue("starttext", assembler->getStartText());
}

void MainWindow::backupSettings()
{
    backupAssembler = settings.value("assembler", QString("NASM")).toString();
    backupMode = settings.value("mode", QString("x64")).toString();
    backupAssemblerOptions = settings.value("assemblyoptions", assembler->getAssemblerOptions()).toString();
    backupAssemblerPath = settings.value("assemblerpath", assembler->getAssemblerPath()).toString();
    backupLinkerOptions = settings.value("linkingoptions", assembler->getLinkerOptions()).toString();
    backupObjectFileName = settings.value("objectfilename", "program.o").toString();
    backupDisableLinking = settings.value("disablelinking", false).toBool();
    backupCurrentDir = settings.value("currentdir", false).toBool();
    backupStartText = settings.value("starttext", assembler->getStartText()).toString();
    backupLinkerPath = settings.value("linkerpath", assembler->getLinkerPath()).toString();
}

void MainWindow::restoreSettingsAndExit()
{
    settings.setValue("assembler", backupAssembler);
    settings.setValue("mode", backupMode);
    recreateAssembler();
    settings.setValue("assemblyoptions", backupAssemblerOptions);
    settings.setValue("assemblerpath", backupAssemblerPath);
    settings.setValue("linkingoptions", backupLinkerOptions);
    settings.setValue("objectfilename", backupObjectFileName);
    settings.setValue("disablelinking", backupDisableLinking);
    settings.setValue("currentdir", backupCurrentDir);
    settings.setValue("starttext", backupStartText);
    settings.setValue("linkerpath", backupLinkerPath);
    closeSettingsPage();
}

void MainWindow::recreateHighlighter()
{
    if (highlighter) {
        delete highlighter;
        highlighter = new Highlighter(assembler);
    }
    if (tabs && tabs->count() > 0) {
        Tab *currentTab = (Tab *) tabs->currentWidget();
        highlighter->setDocument(currentTab->getCodeDocument());
    }
    //! The settings window (and its preview editor) may not exist yet.
    if (settingsStartTextEditor) {
        delete settingsHighlighter;
        settingsHighlighter = new Highlighter(assembler);
        settingsHighlighter->setDocument(settingsStartTextEditor->document());
    }
}

void MainWindow::saveSettings()
{
    saveShortcuts();
    QTimer::singleShot(0, this, &MainWindow::updateStatusBar);
    settings.setValue("startwindow", settingsUi.startWindow->currentIndex());
    settings.setValue("language", settingsUi.language->currentIndex());
    startText = settingsStartTextEditor->toPlainText();
    settings.setValue("allregisters", settingsUi.registersYesRadioButton->isChecked());
    settings.setValue("insertdebugstring", settingsUi.insertDebugStringCheckBox->isChecked());

    //! Change fonts
    settings.setValue("fontfamily", settingsUi.fontComboBox->currentText());
    settings.setValue("fontsize", settingsUi.fontSizeSpinBox->value());
    for (int i = 0; i < tabs->count(); i++) {
        Tab *tab = (Tab *) tabs->widget(i);
        tab->setFonts();
    }
    compilerOut->setFont(Theme::codeFont(settings.value("fontsize", 11).toInt() - 1));

    settings.setValue("assemblyoptions", settingsUi.assemblyOptionsEdit->text());
    settings.setValue("linkingoptions", settingsUi.linkingOptionsEdit->text());
    settings.setValue("objectfilename", settingsUi.objectFileNameEdit->text());
    settings.setValue("disablelinking", settingsUi.disableLinkingCheckbox->isChecked());
    settings.setValue("currentdir", settingsUi.runInCurrentDirectoryCheckbox->isChecked());
    settings.setValue("assemblerpath", settingsUi.assemblerPathEdit->text());
    settings.setValue("linkerpath", settingsUi.linkerPathEdit->text());
    settings.setValue("starttext", settingsStartTextEditor->document()->toPlainText());

    backupSettings();
}

void MainWindow::pickColor(QWidget *button, bool init, bool ignoreSettings)
{
    QPushButton *colorButton = (QPushButton *) button;
    QString name = colorButton->objectName();
    name.remove("Button");
    name.replace("_2", "bg");
    name = name.toLower();
    QColor color = (ignoreSettings) ? colorsMap[name] : (settings.contains(name) ? Theme::color(name) : colorsMap[name]);
    if (!init) {
        QColor newColor = QColorDialog::getColor(color);
        if (newColor.isValid()) {
            color = newColor;
        }
    }
    settings.setValue(name, color.name());
    if (name == "backgroundcolor") {
        int r, g, b;
        color.getRgb(&r, &g, &b);
        for (int i = 9; i < 18; i++) {
            QString buttonName = colorButtons[i]->objectName();
            buttonName.remove("Button");
            buttonName.replace("_2", "bg");
            buttonName = buttonName.toLower();
            settings.setValue(buttonName, color.name());
            colorButtons[i]->setStyleSheet(QString("background-color: rgb(%1, %2, %3)").arg(r).arg(g).arg(b));
        }
    }
    recreateHighlighter();
    int r, g, b;
    color.getRgb(&r, &g, &b);
    colorButton->setStyleSheet(QString("background-color: rgb(%1, %2, %3)").arg(r).arg(g).arg(b));
}

void MainWindow::changeHighlightingFont(QWidget *box, bool init)
{
    QCheckBox *checkBox = (QCheckBox *) box;
    QString name = checkBox->objectName();
    name.remove("CheckBox");
    name = name.toLower();
    if (init) {
        bool defaultState = (name == "keywordsbold") ? true : false;
        checkBox->setChecked(settings.value(name, defaultState).toBool());
    } else {
        settings.setValue(name, checkBox->isChecked());
        recreateHighlighter();
    }
}

void MainWindow::changeHighlightingLineMode(int mode)
{
    if (mode == Qt::Unchecked) {
        settings.setValue("currentlinemode", false);
    } else if (mode == Qt::Checked) {
        settings.setValue("currentlinemode", true);
    }
}

void MainWindow::exitSettings()
{
    saveSettings();
    closeSettingsPage();
}

void MainWindow::resetAllSettings()
{
    //! Ask before execution
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.addButton(tr("Yes"), QMessageBox::YesRole);
    QPushButton *cancelButton = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
    msgBox.setWindowTitle(tr("Warning!"));
    msgBox.setText(tr("All settings will be deleted and all unsaved data will be lost. Are you sure?"));
    msgBox.exec();
    if (msgBox.clickedButton() == cancelButton) {
        return;
    }

    settings.remove("MainWindow/size");
    settings.remove("MainWindow/pos");

    int prevTabsCount = settings.value("tabscount", 0).toInt();
    //! Remove previous
    for (int i = 0; i < prevTabsCount; i++)
        settings.remove(QString("Tabs/") + QString::number(i));

    settings.clear();

    closeFromCloseAll = true;
    close();
}

void MainWindow::changeActionsState(int widgetIndex)
{
    if (settingsPage && mainWidget->widget(widgetIndex) == settingsPage)
        return; // the settings page changes nothing in the file actions
    //! Get started window
    if (widgetIndex == 0) {
        closeAction->setEnabled(false);
        saveAction->setEnabled(false);
        saveAsAction->setEnabled(false);
        saveExeAction->setEnabled(false);
        findAction->setEnabled(false);
        commentAction->setEnabled(false);
        uncommentAction->setEnabled(false);
        undoAction->setEnabled(false);
        redoAction->setEnabled(false);
        cutAction->setEnabled(false);
        copyAction->setEnabled(false);
        pasteAction->setEnabled(false);
        deleteAction->setEnabled(false);
        selectAllAction->setEnabled(false);
        putTabAction->setEnabled(false);
        deleteTabAction->setEnabled(false);
        buildAction->setEnabled(false);
        runAction->setEnabled(false);
        runExeAction->setEnabled(false);
        stopAction->setEnabled(false);
        debugAction->setEnabled(false);
        debugNextAction->setEnabled(false);
        debugNextNiAction->setEnabled(false);
        debugShowRegistersAction->setEnabled(false);
        debugToggleBreakpointAction->setEnabled(false);
        debugShowMemoryAction->setEnabled(false);
    } else {
        closeAction->setEnabled(true);
        saveAction->setEnabled(true);
        saveAsAction->setEnabled(true);
        saveExeAction->setEnabled(true);
        findAction->setEnabled(true);
        pasteAction->setEnabled(true);
        selectAllAction->setEnabled(true);
        putTabAction->setEnabled(true);
        deleteTabAction->setEnabled(true);
        buildAction->setEnabled(true);
        runAction->setEnabled(true);
        runExeAction->setEnabled(true);
        debugAction->setEnabled(true);
        debugToggleBreakpointAction->setEnabled(true);
    }
}

void MainWindow::openHelp()
{
    help = new QTextBrowser;
    help->setAttribute(Qt::WA_DeleteOnClose);
    QFile helpFile;
    //! language 0=Russian
    if (settings.value("language", 0).toInt() == 0) {
        helpFile.setFileName(":help/help.html");
    } else if (settings.value("language", 0).toInt() == 7) {
        //! Asmon: Hebrew help
        helpFile.setFileName(":help/helpHE.html");
    } else {
        //! English and other languages
        helpFile.setFileName(":help/helpENG.html");
    }

    helpFile.open(QFile::ReadOnly);
    QTextStream helpText(&helpFile);
    helpText.setEncoding(QStringConverter::Utf8);
    help->setHtml(helpText.readAll());
    helpFile.close();
    help->setWindowState(Qt::WindowMaximized);
    help->setOpenExternalLinks(true);
    help->setWindowTitle(tr("Help"));
    help->setWindowIcon(QIcon(":images/mainIcon.png"));

    QAction *closeHelpAction = new QAction(tr("Close help"), help);
    closeHelpAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(closeHelpAction, SIGNAL(triggered()), help, SLOT(close()));
    help->addAction(closeHelpAction);

    help->show();
}

void MainWindow::openAbout()
{
    QMessageBox::about(this, tr("About Asmon"),
                       QString("Asmon ") + ASMON_VERSION + "\n" +
                       tr("Assembly IDE with a built-in debugger and reverse-engineering tools.") + "\n\n" +
                       tr("Licensed under the GNU GPL v3.0") + "\n" +
                       "https://github.com/shealtieltehumim-prog/asmon" + "\n\n" +
                       tr("Based on SASM %1 by Dmitriy Manushin (https://github.com/Dman95/SASM).").arg(SASM_VERSION) + "\n" +
                       tr("Copyright (c) 2013 Dmitriy Manushin") + "\n" +
                       tr("Icon and advices - Alick Gaybullaev") + "\n" +
                       tr("Based on the Qt.")
                       );
}

void MainWindow::onMessageReceived(const QString &message)
{
    raise();
    activateWindow();
    if (message.startsWith("run")) {
        openFile(message.mid(3));
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    foreach (const QUrl &url, event->mimeData()->urls()) {
        const QString file = url.toLocalFile();
        const QString suffix = QFileInfo(file).suffix().toLower();
        if (modeBar->mode() == ModeBar::Reverse && (suffix == "exe" || suffix == "dll" || suffix == "sys"))
            openProgramInAsmon(file);
        else
            openFile(file);
    }
}

//! Reverse engineering mode: opens a program (exe, dll) in Asmon's own disassembler.
void MainWindow::openProgramInAsmon(const QString &path)
{
    if (mainWidget->currentWidget() != workWidget)
        mainWidget->setCurrentWidget(workWidget);
    modeBar->setMode(ModeBar::Reverse);
    reversePage->openProgram(path);
}

void MainWindow::chooseProgramForAsmon()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Open a program"), QString(),
                                                      tr("Programs (*.exe *.dll *.sys);;All files (*)"));
    if (!file.isEmpty())
        openProgramInAsmon(file);
}

bool MainWindow::removeDirRecuresively(const QString &dirName){
    bool result = true;
    QDir dir(dirName);

    if (dir.exists(dirName)) {
        Q_FOREACH(QFileInfo info, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::System |
                                                    QDir::Hidden  | QDir::AllDirs | QDir::Files, QDir::DirsFirst)) {
            if (info.isDir()) {
                result = removeDirRecuresively(info.absoluteFilePath());
            }
            else {
                result = QFile::remove(info.absoluteFilePath());
            }

            if (!result) {
                return result;
            }
        }
        result = dir.rmdir(dirName);
    }
    return result;
}

MainWindow::~MainWindow()
{
    //! Delete all temporary files
    removeDirRecuresively(Common::pathInTemp(QString()));
}
