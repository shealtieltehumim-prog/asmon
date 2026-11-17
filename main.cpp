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
    QTranslator translator, qtTranslator, addTranslator;
    //! No first-run language dialog: Hebrew is the default (index 7, see settings.ui).
    if (!settings.contains("language"))
        settings.setValue("language", 7);
    if (settings.value("language", 0).toInt() == 0) { //russian language
        translator.load(":/translations/language_ru.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_ru.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 2) { //turkish language
        translator.load(":/translations/language_tr.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_tr.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 3) { //chinese language
        translator.load(":/translations/language_zh.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_zh.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 4) { //german language
        translator.load(":/translations/language_de.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_de.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 5) { //italian language
        translator.load(":/translations/language_it.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_it.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 6) { //polish language
        translator.load(":/translations/language_pl.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_pl.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 7) { //hebrew language
        translator.load(":/translations/language_he.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_he.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 8) { //spanish language
        translator.load(":/translations/language_es.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_es.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 9) { //portuguese portuguese language
        translator.load(":/translations/language_pt_PT.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_pt_PT.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 10) { //french language
        translator.load(":/translations/language_fr.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_fr.qm");
        a.installTranslator(&qtTranslator);
    } else if (settings.value("language", 0).toInt() == 11) { // brazilian portuguese language
        translator.load(":/translations/language_pt_BR.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_pt_PT.qm");
        a.installTranslator(&qtTranslator);

        addTranslator.load(":/translations/qt_pt_BR.qm");
        a.installTranslator(&addTranslator);
    } else if (settings.value("language", 0).toInt() == 12) {  // japanese language
        translator.load(":/translations/language_ja.qm");
        a.installTranslator(&translator);

        qtTranslator.load(":/translations/qt_ja.qm");
        a.installTranslator(&qtTranslator);
    }

    MainWindow w(a.arguments());

    QObject::connect(&a, SIGNAL(messageReceived(const QString&)), &w, SLOT(onMessageReceived(const QString&)));

    w.show();
    w.activateWindow();
    return a.exec();
}
