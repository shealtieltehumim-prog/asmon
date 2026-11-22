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
    folding.cpp \
    minimap.cpp \
    selftest.cpp \
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
    linemap.cpp \
    nativedebugger.cpp \
    native/pe.cpp \
    native/disasm.cpp \
    native/regs.cpp \
    native/expr.cpp \
    native/engine.cpp \
    native/debuggee.cpp \
    gdbmi.cpp \
    linuxtarget.cpp \
    buildoutputview.cpp \
    commandpalette.cpp \
    modebar.cpp \
    reversepage.cpp \
    licensesdialog.cpp \
    decompiler.cpp \
    hexview.cpp \
    native/analysis.cpp \
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
    spacedlayout.h \
    folding.h \
    minimap.h \
    selftest.h \
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
    linemap.h \
    nativedebugger.h \
    midebugger.h \
    gdbmi.h \
    linuxtarget.h \
    buildoutputview.h \
    commandpalette.h \
    modebar.h \
    reversepage.h \
    licensesdialog.h \
    decompiler.h \
    hexview.h \
    native/analysis.h \
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

# Zydis (MIT, third_party/zydis): the disassembler of the native debugger and the reverse-engineering view
CONFIG += object_parallel_to_source
DEFINES += ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD ZYDIS_DISABLE_ENCODER
INCLUDEPATH += $$PWD/third_party/zydis/include $$PWD/third_party/zydis/zycore/include $$PWD/third_party/zydis/src
SOURCES += \
    third_party/zydis/src/Decoder.c \
    third_party/zydis/src/DecoderData.c \
    third_party/zydis/src/Disassembler.c \
    third_party/zydis/src/Formatter.c \
    third_party/zydis/src/FormatterATT.c \
    third_party/zydis/src/FormatterBase.c \
    third_party/zydis/src/FormatterBuffer.c \
    third_party/zydis/src/FormatterIntel.c \
    third_party/zydis/src/MetaInfo.c \
    third_party/zydis/src/Mnemonic.c \
    third_party/zydis/src/Register.c \
    third_party/zydis/src/Segment.c \
    third_party/zydis/src/SharedData.c \
    third_party/zydis/src/String.c \
    third_party/zydis/src/Utils.c \
    third_party/zydis/src/Zydis.c \
    third_party/zydis/zycore/src/Allocator.c \
    third_party/zydis/zycore/src/ArgParse.c \
    third_party/zydis/zycore/src/Bitset.c \
    third_party/zydis/zycore/src/Format.c \
    third_party/zydis/zycore/src/List.c \
    third_party/zydis/zycore/src/String.c \
    third_party/zydis/zycore/src/Vector.c \
    third_party/zydis/zycore/src/Zycore.c
