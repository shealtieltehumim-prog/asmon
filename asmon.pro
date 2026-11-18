#-------------------------------------------------
#
# Project created by QtCreator 2013-03-22T02:19:57
#
#-------------------------------------------------

QT       += core gui

lessThan(QT_MAJOR_VERSION, 6): error("Asmon requires Qt 6")
QT += widgets svg
CONFIG += c++17

# build number given by CI (GitHub run number); 0 for builds made by hand
ASMON_BUILD = $$(ASMON_BUILD)
isEmpty(ASMON_BUILD): ASMON_BUILD = 0
DEFINES += ASMON_BUILD=$$ASMON_BUILD
win32: LIBS += -lshell32

TARGET = asmon
TEMPLATE = app

isEmpty(PREFIX) {
 PREFIX = /usr
}

BINDIR = $$PREFIX/bin
DATADIR = $$PREFIX/share

bsd {
    binfile.files += $$OUT_PWD/$$TARGET
    binfile.path = $$BINDIR
    # the binary does not exist yet when qmake runs in a clean build directory
    binfile.CONFIG += no_check_exist executable
    toolfiles.files += BSD/bin/*
    toolfiles.path = $$BINDIR
    toolfiles.CONFIG += nostrip executable
    data.files += BSD/share/asmon/*
    data.path = $$DATADIR/asmon/
    shortcutfiles.files += BSD/share/applications/asmon.desktop
    shortcutfiles.path = $$DATADIR/applications/
    docfiles.files += BSD/share/doc/asmon/changelog.gz
    docfiles.files += BSD/share/doc/asmon/copyright
    docfiles.path = $$DATADIR/doc/asmon/
} else {
    binfile.files += $$OUT_PWD/$$TARGET
    binfile.path = $$BINDIR
    # the binary does not exist yet when qmake runs in a clean build directory
    binfile.CONFIG += no_check_exist executable
    toolfiles.files += Linux/bin/*
    toolfiles.path = $$BINDIR
    toolfiles.CONFIG += nostrip executable
    data.files += Linux/share/asmon/*
    data.path = $$DATADIR/asmon/
    shortcutfiles.files += Linux/share/applications/asmon.desktop
    shortcutfiles.path = $$DATADIR/applications/
    docfiles.files += Linux/share/doc/asmon/changelog.gz
    docfiles.files += Linux/share/doc/asmon/copyright
    docfiles.path = $$DATADIR/doc/asmon/
}

!bsd {
    iconfile.files += Linux/share/asmon/asmon.png
    iconfile.path = $$DATADIR/icons/hicolor/256x256/apps/
    INSTALLS += iconfile
}
INSTALLS += binfile
INSTALLS += toolfiles
INSTALLS += data
INSTALLS += shortcutfiles
INSTALLS += docfiles

include(singleapplication/qtsingleapplication.pri)

SOURCES += main.cpp\
    terminalwidget.cpp \
    symbols.cpp \
    searchpanel.cpp \
    processpicker.cpp \
    updater.cpp \
    editcommands.cpp \
    framelesshelper.cpp \
    retools.cpp \
    debugpanels.cpp \
    midebugger.cpp \
    gdbmi.cpp \
    linuxtarget.cpp \
    buildoutputview.cpp \
    commandpalette.cpp \
    modebar.cpp \
    theme.cpp \
        mainwindow.cpp \
    tab.cpp \
    highlighter.cpp \
    codeeditor.cpp \
    finddialog.cpp \
    ruqtextedit.cpp \
    ruqplaintextedit.cpp \
    getstartedwidget.cpp \
    debuganycommandwidget.cpp \
    assembler.cpp \
    nasm.cpp \
    gas.cpp \
    common.cpp \
    fasm.cpp \
    signallocker.cpp \
    masm.cpp \
    gccbasedassembler.cpp

HEADERS  += mainwindow.h \
    regexp.h \
    theme.h \
    terminalwidget.h \
    symbols.h \
    searchpanel.h \
    processpicker.h \
    updater.h \
    framelesshelper.h \
    retools.h \
    debugpanels.h \
    debugbackend.h \
    midebugger.h \
    gdbmi.h \
    linuxtarget.h \
    buildoutputview.h \
    commandpalette.h \
    modebar.h \
    tab.h \
    highlighter.h \
    codeeditor.h \
    finddialog.h \
    ruqtextedit.h \
    ruqplaintextedit.h \
    getstartedwidget.h \
    debuganycommandwidget.h \
    assembler.h \
    nasm.h \
    gas.h \
    common.h \
    fasm.h \
    signallocker.h \
    masm.h \
    gccbasedassembler.h

FORMS += settings.ui

RESOURCES += \
    Templates.qrc \
    Icons.qrc \
    Fonts.qrc \
    Languages.qrc \
    Images.qrc \
    Help.qrc

RC_FILE = icon.rc

CODECFORTR      = UTF-8
TRANSLATIONS    += language_ru.ts \
                   language_tr.ts \
                   language_zh.ts \
                   language_de.ts \
                   language_it.ts \
                   language_pl.ts \
                   language_he.ts \
                   language_es.ts \
                   language_pt_PT.ts \
                   language_fr.ts \
                   language_pt_BR.ts \
                   language_ja.ts
