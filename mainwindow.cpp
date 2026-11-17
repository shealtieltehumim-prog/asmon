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
    closeFromCloseAll = false;
    settingsStartTextEditor = 0;
    settingsHighlighter = 0;
    help = 0;
    registersWindow = 0;
    memoryWindow = 0;
    debugger = 0;
    programStopped = true;
    highlighter = 0;
    tabs = 0;
    memoryDock = 0;
    registersDock = 0;
    debugToolBar = 0;
    debugDock = 0;
    debugPanels = 0;
    debugStepOutAction = 0;
    debugStepInsnAction = 0;
    explorerDock = 0;
    targetBox = 0;
    commandPalette = 0;
    buildOutputDock = 0;

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
    refreshEditMenu();
    setAcceptDrops(true);

    //! Restore log splitter state
    splitter->restoreState(settings.value("logsplitterstate").toByteArray());

    //! Open documents from command line
    for (int i = 1; i < args.size(); i++) {
        if (args[i] == "--asmon-restart") {
            restorePrevSession(true);
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
    resize(settings.value("size", QSize(1024, 650)).toSize()); //default 1024x650
    move(settings.value("pos", QPoint(200, 200)).toPoint());
    //! Set maximized
    setWindowState(windowState() | (Qt::WindowState) settings.value("maximized", (int) Qt::WindowNoState).toInt());
    settings.endGroup();

    //! Get Started window
    getStartedWidget = new GetStartedWidget;
    connect(getStartedWidget->newButton, SIGNAL(clicked()), this, SLOT(newFile()));
    connect(getStartedWidget->openButton, SIGNAL(clicked()), this, SLOT(openFile()));
    connect(getStartedWidget->prevSessionButton, SIGNAL(clicked()), this, SLOT(restorePrevSession()));

    //! Create form
    splitter = new QSplitter;
    splitter->setOrientation(Qt::Vertical);
    workLayout = new QVBoxLayout;
    workLayout->addWidget(splitter);
    workLayout->setContentsMargins(0, 0, 0, 0);
    workLayout->setSpacing(0);
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
    QFont compilerOutFont;
    compilerOutFont.setPointSize(settings.value("fontsize", 11).toInt());
    compilerOut->setFont(compilerOutFont);
    compilerOut->setText(tr("Build log:") + '\n');
    compilerOut->viewport()->installEventFilter(this);

    //! Create gdb command widget
    debugAnyCommandWidget = new DebugAnyCommandWidget;

    //! Add widgets on splitter
    splitter->addWidget(tabs);
    splitter->addWidget(compilerOut);
    splitter->setCollapsible(1, false);
    workLayout->addWidget(debugAnyCommandWidget);
    int compilerOutSize = 100;
    debugAnyCommandWidget->close();
    debugAnyCommandWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    splitter->setSizes(QList<int>() << splitter->size().height() - compilerOutSize << compilerOutSize);
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
    settingsMenu = menuBar()->addMenu(tr("Settings"));
    settingsMenu->addAction(settingsAction);
    helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(helpAction);
    helpMenu->addAction(aboutAction);
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
    fileToolBar = addToolBar(tr("Main"));
    fileToolBar->setObjectName("MainToolBar");
    fileToolBar->setMovable(false);
    fileToolBar->setIconSize(QSize(18, 18));
    fileToolBar->addAction(newAction);
    fileToolBar->addAction(openAction);
    fileToolBar->addAction(saveAction);
    fileToolBar->addSeparator();
    fileToolBar->addAction(runAction);
    fileToolBar->addAction(buildAction);
    fileToolBar->addAction(debugAction);
    fileToolBar->addAction(stopAction);

    debugToolBar = addToolBar(tr("Debug"));
    debugToolBar->setObjectName("DebugToolBar");
    debugToolBar->setMovable(false);
    debugToolBar->setIconSize(QSize(18, 18));
    debugToolBar->addAction(debugNextNiAction);
    debugToolBar->addAction(debugNextAction);
    debugToolBar->hide();

    editToolBar = 0;
    buildToolBar = 0;

    restoreState(settings.value("windowstate").toByteArray());
    debugToolBar->hide();
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
    focusModeAction->setShortcut(QKeySequence("Ctrl+K, Z"));
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
    debugDock = new QDockWidget(tr("Debugger"), this);
    debugDock->setObjectName("debugDock");
    debugDock->setWidget(debugPanels);
    debugDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, debugDock);
    debugDock->hide();

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
    debugMenu->insertAction(debugToggleBreakpointAction, debugStepInsnAction);
    debugToolBar->addAction(debugStepOutAction);

    //! RE menu: open the program in external reverse-engineering tools
    {
        QMenu *reMenu = new QMenu(tr("RE"), this);
        menuBar()->insertMenu(settingsMenu->menuAction(), reMenu);
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
            buildOutputDock->show();
            buildOutputDock->raise();
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
    buildOutputView = new BuildOutputView;
    buildOutputDock = new QDockWidget(tr("Build output"), this);
    buildOutputDock->setObjectName("buildOutputDock");
    buildOutputDock->setWidget(buildOutputView);
    buildOutputDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::BottomDockWidgetArea, buildOutputDock);
    buildOutputDock->hide();
    QAction *buildOutputAction = buildOutputDock->toggleViewAction();
    buildOutputAction->setText(tr("Build output (listing, disassembly, hex)"));
    buildOutputAction->setProperty("en", "Build output listing disassembly hex objdump");
    buildOutputAction->setShortcut(QKeySequence("Ctrl+Shift+B"));
    connect(buildOutputDock, SIGNAL(visibilityChanged(bool)), this, SLOT(refreshBuildOutput()));

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
        });
        fileToolBar->addSeparator();
        fileToolBar->addWidget(targetBox);

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
    }

    viewMenu->addAction(commandPaletteAction);
    viewMenu->addSeparator();
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
    if (!buildOutputDock || !buildOutputDock->isVisible())
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
        Common::pathInTemp("AsmonProg.exe"), dataPath + "/include", sourceDir);
    if (!r.ok) {
        printLogWithTime(tr("Warning! Errors have occurred in the build:") + '\n', Theme::color("errorcolor"));
        printLog(prettifyBuildLog(r.log), Theme::color("errorcolor"));
        return;
    }
    printLogWithTime(tr("Built successfully (Linux ELF x64).") + '\n', Theme::color("successcolor"));
    printLog(prettifyBuildLog(r.log), Theme::color("errorcolor"));
    refreshBuildOutput();
    programIsBuilded = true;
}

void MainWindow::showCommandPalette()
{
    commandPalette->popup(menuBar());
}

void MainWindow::toggleFocusMode(bool on)
{
    if (on) {
        hiddenByFocusMode.clear();
        QList<QWidget *> candidates;
        candidates << menuBar() << fileToolBar << debugToolBar << compilerOut;
        for (QDockWidget *d : findChildren<QDockWidget *>())
            candidates << d;
        for (QWidget *w : candidates) {
            if (w && w->isVisible()) {
                hiddenByFocusMode << w;
                w->hide();
            }
        }
        printLogWithTime(tr("Focus mode: press Ctrl+K, Z to exit.") + '\n', Theme::color("fontcolor"));
    } else {
        for (QWidget *w : hiddenByFocusMode)
            if (w)
                w->show();
        hiddenByFocusMode.clear();
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
    Theme::setTheme(settings, dark);
    Theme::apply(*qApp);
    for (QAction *a : findChildren<QAction *>()) {
        const QString name = a->property("asmonIcon").toString();
        if (!name.isEmpty())
            a->setIcon(Theme::icon(name));
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
    if (tabs->count() > 0) {
        settings.setValue("tabgeometry", ((Tab *) tabs->currentWidget())->saveGeometry());
        settings.setValue("tabwindowstate", ((Tab *) tabs->currentWidget())->saveState());
    }
    //! Tabs
    mainWidget->setCurrentIndex(1);
    Tab *tab = new Tab;
    tab->code->setPlainText(startText);
    tabs->addTab(tab, tr("New"));
    tabs->setCurrentWidget(tab);
    connect(tab->code, SIGNAL(textChanged()), this, SLOT(setProgramBuildedFlagToFalse()));
    connect(tab->code, SIGNAL(textChanged()), this, SLOT(findNextAfterTextChange()));
    connect(tab->code, SIGNAL(modificationChanged(bool)), this, SLOT(changeCurrentSavedState(bool)));
    connect(tab->code, SIGNAL(fileOpened(QString)), this, SLOT(openFile(QString)));
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

    newFile();
    Tab *curTab = (Tab *) tabs->currentWidget();
    curTab->loadCodeFromFile(path);
    setTabPath(path);
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
        settingsWindow->close();
        delete settingsWindow;
    }
    if (help) {
        help->close();
        delete help;
    }
    if (registersWindow) {
        registersWindow->close();
        delete registersWindow;
        registersWindow = 0;
    }
    if (memoryWindow) {
        memoryWindow->close();
        delete memoryWindow;
        memoryWindow = 0;
    }
}

bool MainWindow::deleteTab(int index, bool saveFileName)
{
    settings.setValue("tabgeometry", ((Tab *) tabs->widget(index))->saveGeometry());
    settings.setValue("tabwindowstate", ((Tab *) tabs->widget(index))->saveState());
    if (okToContinue(index)) {
        if (debugger && index == tabs->currentIndex())
            debugExit();
        if (tabs->count() == 1)
            //! Get started
            mainWidget->setCurrentIndex(0);
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
    //! Set highlighter
    if (tabs->count() == 0)
        return;
    Tab *currentTab = (Tab *) tabs->widget(index);
    highlighter->setDocument(currentTab->getCodeDocument());
}

void MainWindow::buildProgram(bool debugMode)
{
    saveFile(-1, false); //save file before building if program already was saved

    programIsBuilded = false;

    using Common::applicationDataPath;
    printLogWithTime(tr("Build started...") + '\n', Theme::color("fontcolor"));
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
                            "or reinstall Asmon with the 32-bit component.") + '\n', Theme::color("errorcolor"));
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
        return;
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
    const bool objectExists = QFile::exists(
        Common::pathInTemp(settings.value("objectfilename", "program.o").toString()));
    if (!disableLinking && objectExists) {
        QStringList linkerArguments = linkerOptions.split(QChar(' '));
        linkerArguments.replaceInStrings("$PROGRAM.OBJ$",
            Common::pathInTemp(settings.value("objectfilename", "program.o").toString()));
        linkerArguments.replaceInStrings("$MACRO.OBJ$", stdioMacros);
        linkerArguments.replaceInStrings("$PROGRAM$", Common::pathInTemp("AsmonProg.exe"));
        linkerArguments.replaceInStrings("$SOURCE$", Common::pathInTemp("program.asm"));
        linkerArguments.replaceInStrings("$LSTOUTPUT$", Common::pathInTemp("program.lst"));
        QProcess linkerProcess;
        linkerOutput = Common::pathInTemp("linkererror.txt");
        linkerProcess.setStandardOutputFile(linkerOutput);
        linkerProcess.setStandardErrorFile(linkerOutput, QIODevice::Append);
        linkerProcess.start(linker, linkerArguments);
        linkerProcess.waitForFinished();

        if (linkerProcess.error() != QProcess::UnknownError) {
            printLogWithTime(tr("Unable to start linker. Check your settings.") + '\n', Theme::color("errorcolor"));
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

    bool builded;
    if (QFile::exists(Common::pathInTemp("AsmonProg.exe")))
        builded = true;
    else
        builded = false;
    if (!builded) {
        printLogWithTime(tr("Warning! Errors have occurred in the build:") + '\n', Theme::color("errorcolor"));

        //! Print errors
        printLog(prettifyBuildLog(logText), Theme::color("errorcolor"));
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
    outputIndex = tabs->currentIndex();
    if (!programIsBuilded) {
        buildProgram();
    }
    if (!programIsBuilded) {
       return;
    }

    stopAction->setEnabled(true);
    debugAction->setEnabled(false);
    buildAction->setEnabled(false);

    ((Tab *) tabs->currentWidget())->clearOutput();

    printLogWithTime(tr("The program is executing...") + '\n', Theme::color("fontcolor"));
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
    programStopped = false;
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
            programStopped = true;
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
        programStopped = true;
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
    if (!debugger && linuxTargetSelected()) {
        printLogWithTime(tr("Debugging Linux programs from Windows is not supported yet. "
                            "You can build and run them (F9).") + '\n', Theme::color("errorcolor"));
        return;
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

    MiDebugger::Config cfg;
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
    cfg.listing = Common::pathInTemp("program.lst");
    cfg.assembler = assembler;

    debugger = new MiDebugger(cfg, this);
    connect(debugger, &MiDebugger::stopped, this, &MainWindow::onDebugStopped);
    connect(debugger, &MiDebugger::crashed, this, &MainWindow::onDebugCrashed);
    connect(debugger, &MiDebugger::finished, this, &MainWindow::onDebugFinished);
    connect(debugger, &MiDebugger::running, this, &MainWindow::onDebugRunning);
    connect(debugger, &MiDebugger::programOutput, this, [this](const QString &t) { printOutput(t); });
    connect(debugger, &MiDebugger::consoleOutput, this, [this](const QString &t) {
        printLog(t, Theme::color("fontcolor"));
    });
    connect(debugger, &MiDebugger::log, this, [this](const QString &t, bool error) {
        printLog(t, Theme::color(error ? "errorcolor" : "fontcolor"));
    });
    connect(debugger, &MiDebugger::ended, this, &MainWindow::debugExit, Qt::QueuedConnection);
    connect(code, &CodeEditor::breakpointsChanged, debugger, [this](quint64 line, bool on) {
        if (debugger)
            debugger->setBreakpoint(int(line), on);
    });
    connect(code, &RuQPlainTextEdit::addWatchSignal, debugPanels, &DebugPanels::addWatch);
    debugPanels->attach(debugger);

    code->setDebugEnabled();
    enableDebugActions();
    showAnyCommandWidget();
    debugDock->show();
    debugDock->raise();

    QList<int> lines;
    for (quint64 l : *(code->getBreakpoints()))
        lines << int(l);
    debugger->start(lines);
}

void MainWindow::onDebugStopped(quint64 pc, int line)
{
    if (!debugger)
        return;
    changeDebugActionToStart();
    CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
    if (line > 0) {
        code->updateDebugLine(line);
    } else {
        code->updateDebugLine(-2);
        printLogWithTime(tr("Stopped outside your code at 0x%1 (see Disassembly).").arg(pc, 0, 16) + '\n',
                         Theme::color("fontcolor"));
        debugPanels->showTab(DebugPanels::DisassemblyTab);
    }
}

void MainWindow::onDebugCrashed(const QString &signal, const QString &meaning, quint64 pc, int line)
{
    changeDebugActionToStart();
    CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
    if (line > 0)
        code->updateDebugLine(line);
    const QString where = line > 0 ? tr("line %1").arg(line) : QString("0x%1").arg(pc, 0, 16);
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
    debugAction->setText(tr("Pause"));
    debugAction->setIcon(Theme::icon("pause"));
    debugAction->setProperty("asmonIcon", "pause");
    //! F5 stays active while running: it pauses the program.
    if (tabs && tabs->count() > 0)
        ((Tab *) tabs->currentWidget())->code->updateDebugLine(-2);
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

    debugToolBar->show();
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
    if (debugToolBar)
        debugToolBar->hide();
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
    debugDock->show();
    debugDock->raise();
    debugPanels->showTab(DebugPanels::MemoryTab);
}

void MainWindow::saveWatches(DebugTableWidget *)
{
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
    debugDock->show();
    debugDock->raise();
    debugPanels->showTab(DebugPanels::RegistersTab);
}

void MainWindow::setShowRegistersToUnchecked()
{
}

void MainWindow::debugExit()
{
    if (!debugger)
        return;
    MiDebugger *d = debugger;
    debugger = 0;
    d->disconnect(this);
    d->stop();
    d->deleteLater();
    debugPanels->attach(nullptr);
    if (tabs && tabs->count() > 0) {
        CodeEditor *code = ((Tab *) tabs->currentWidget())->code;
        code->disconnect(debugPanels);
        code->setDebugDisabled();
    }
    closeAnyCommandWidget();
    debugDock->hide();
    printLogWithTime(tr("Debugging finished.") + '\n', Theme::color("successcolor"));
    disableDebugActions();
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
    printLog("> " + command + "\n", QColor(Theme::isDark() ? "#4fc1ff" : "#2047f7"));
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
    settings.setValue("logsplitterstate", splitter->saveState());
}

void MainWindow::openSettings()
{
    //! Initialize
    if (!settingsWindow) {
        settingsWindow = new QWidget(this, Qt::Window);
        settingsWindow->setWindowModality(Qt::WindowModal);
        settingsUi.setupUi(settingsWindow);
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

        backupSettings();
        initAssemblerSettings(true);
    } else {
        initAssemblerSettings(false);
    }

    //! Set settings
    //! Common
    settingsUi.startWindow->setCurrentIndex(settings.value("startwindow", 0).toInt());
    settingsUi.language->setCurrentIndex(settings.value("language", 0).toInt());
    settingsUi.fontComboBox->setCurrentFont(QFont(settings.value("fontfamily",
                                                                 Theme::defaultCodeFont()).value<QString>()));
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
    settingsWindow->show();
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
    settingsWindow->close();
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
    settings.setValue("startwindow", settingsUi.startWindow->currentIndex());
    settings.setValue("language", settingsUi.language->currentIndex());
    startText = settingsStartTextEditor->toPlainText();
    settings.setValue("allregisters", settingsUi.registersYesRadioButton->isChecked());
    settings.setValue("insertdebugstring", settingsUi.insertDebugStringCheckBox->isChecked());

    //! Change fonts
    settings.setValue("fontfamily", settingsUi.fontComboBox->currentFont().family());
    settings.setValue("fontsize", settingsUi.fontSizeSpinBox->value());
    for (int i = 0; i < tabs->count(); i++) {
        Tab *tab = (Tab *) tabs->widget(i);
        tab->setFonts();
    }
    QFont compilerOutFont;
    compilerOutFont.setPointSize(settings.value("fontsize", 11).toInt());
    compilerOut->setFont(compilerOutFont);

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
    settingsWindow->close();
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
    foreach (const QUrl &url, event->mimeData()->urls())
        openFile(url.toLocalFile());
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
