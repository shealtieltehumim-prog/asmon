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

#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QSettings>
#include <QPushButton>
#include <QtSingleApplication>
#include <QFile>
#include "theme.h"
#include <QThread>

/**
 * @file main.cpp
 *
 */

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QCoreApplication::setOrganizationName("Asmon");
    QCoreApplication::setApplicationName("Asmon");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QtSingleApplication a(argc, argv);
    //! Portable mode: a file named "portable" next to the executable keeps
    //! the settings in "<exe dir>/data/Asmon/Asmon.ini" instead of the user profile.
    if (QFile::exists(QCoreApplication::applicationDirPath() + "/portable"))
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QCoreApplication::applicationDirPath() + "/data");
    QSettings settings;
    Theme::ensureInitialized(settings);
    Theme::apply(a);
    //! Restart requested by the app itself (e.g. after a language switch):
    //! wait until the previous instance has exited before going on.
    if (a.arguments().contains("--asmon-restart")) {
        for (int i = 0; i < 50 && a.isRunning(); ++i)
            QThread::msleep(100);
    }
    if (a.isRunning()) {
        //we already have one instance of our application
        if (argc <= 1) {
            a.sendMessage(QString("activate"));
        } else {
            for (int i = 1; i < argc; ++i) {
                a.sendMessage(QString("run") + QString(argv[i]));
            }
        }
        return 0;
    }
    //! No first-run language dialog: Hebrew is the default (index 7, see settings.ui).
    if (!settings.contains("language"))
        settings.setValue("language", 7);

    //! Language index -> translation file suffix, in the order of the settings combo box.
    //! Index 1 is English (the source language, no translation file).
    static const char *const kLanguages[] = { "ru", "", "tr", "zh", "de", "it", "pl", "he",
                                              "es", "pt_PT", "fr", "pt_BR", "ja" };
    const int languageIndex = settings.value("language", 7).toInt();
    QTranslator translator, qtTranslator, addTranslator;
    if (languageIndex >= 0 && languageIndex < int(sizeof(kLanguages) / sizeof(*kLanguages))
            && kLanguages[languageIndex][0] != '\0') {
        const QString code = QString::fromLatin1(kLanguages[languageIndex]);
        if (translator.load(":/translations/language_" + code + ".qm"))
            a.installTranslator(&translator);
        //! Brazilian Portuguese uses the European Qt file plus its own additions.
        const QString qtCode = (code == "pt_BR") ? QString("pt_PT") : code;
        if (qtTranslator.load(":/translations/qt_" + qtCode + ".qm"))
            a.installTranslator(&qtTranslator);
        if (code == "pt_BR" && addTranslator.load(":/translations/qt_pt_BR.qm"))
            a.installTranslator(&addTranslator);
    }

    //! Asmon: right-to-left interface for Hebrew. Code editors stay left-to-right
    //! (see CodeEditor), so assembly reads normally while menus, docks and dialogs mirror.
    if (languageIndex == 7)
        a.setLayoutDirection(Qt::RightToLeft);

    MainWindow w(a.arguments());

    QObject::connect(&a, SIGNAL(messageReceived(const QString&)), &w, SLOT(onMessageReceived(const QString&)));

    w.show();
    w.activateWindow();
    return a.exec();
}
