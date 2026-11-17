#-------------------------------------------------
#
# Project created by QtCreator 2013-03-22T02:19:57
#
#-------------------------------------------------

QT       += core gui

lessThan(QT_MAJOR_VERSION, 6): error("Asmon requires Qt 6")
QT += widgets svg
CONFIG += c++17

TARGET = asmon
TEMPLATE = app

isEmpty(PREFIX) {
 PREFIX = /usr
}

BINDIR = $$PREFIX/bin
DATADIR = $$PREFIX/share

bsd {
    binfile.files += $$OUT_PWD/$$TARGET
    binfile.files += BSD/bin/*
    binfile.path = $$BINDIR
    data.files += BSD/share/asmon/*
    data.path = $$DATADIR/asmon/
    shortcutfiles.files += BSD/share/applications/asmon.desktop
    shortcutfiles.path = $$DATADIR/applications/
    docfiles.files += BSD/share/doc/asmon/changelog.gz
    docfiles.files += BSD/share/doc/asmon/copyright
    docfiles.path = $$DATADIR/doc/asmon/
} else {
    binfile.files += $$OUT_PWD/$$TARGET
    binfile.files += Linux/bin/*
    binfile.path = $$BINDIR
    data.files += Linux/share/asmon/*
    data.path = $$DATADIR/asmon/
    shortcutfiles.files += Linux/share/applications/asmon.desktop
    shortcutfiles.path = $$DATADIR/applications/
    docfiles.files += Linux/share/doc/asmon/changelog.gz
    docfiles.files += Linux/share/doc/asmon/copyright
    docfiles.path = $$DATADIR/doc/asmon/
}

INSTALLS += binfile
INSTALLS += data
INSTALLS += shortcutfiles
INSTALLS += docfiles

include(singleapplication/qtsingleapplication.pri)

SOURCES += main.cpp\
    linuxtarget.cpp \
    buildoutputview.cpp \
    commandpalette.cpp \
    theme.cpp \
        mainwindow.cpp \
    tab.cpp \
    highlighter.cpp \
    codeeditor.cpp \
    debugger.cpp \
    finddialog.cpp \
    ruqtextedit.cpp \
    ruqplaintextedit.cpp \
    getstartedwidget.cpp \
    debugtablewidget.cpp \
    watchsettingswidget.cpp \
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
    linuxtarget.h \
    buildoutputview.h \
    commandpalette.h \
    tab.h \
    highlighter.h \
    codeeditor.h \
    debugger.h \
    finddialog.h \
    ruqtextedit.h \
    ruqplaintextedit.h \
    getstartedwidget.h \
    debugtablewidget.h \
    watchsettingswidget.h \
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
