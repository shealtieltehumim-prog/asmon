/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** Theme: one place for the colour scheme (dark by default, light as an
** alternative) and the flat, quiet application style sheet.
** Editor colours keep using SASM's existing settings keys
** ("keywordscolor", "backgroundcolor", ...), so the colour buttons in the
** settings dialog keep working.
****************************************************************************/
#ifndef ASMON_THEME_H
#define ASMON_THEME_H

#include <QColor>
#include <QMap>
#include <QString>

class QApplication;
class QIcon;
class QSettings;

namespace Theme {

//! "dark" or "light"
QString current();
bool isDark();

//! Default colours for every editor colour key of a theme.
QMap<QString, QColor> editorDefaults(bool dark);

//! Colour of a settings key, falling back to the current theme's default.
QColor color(const QString &key);

//! Stores the theme name and resets all editor colours to its defaults.
void setTheme(QSettings &settings, bool dark);

//! On first run: writes the dark theme and a good monospace font.
void ensureInitialized(QSettings &settings);

//! Applies palette + style sheet to the whole application.
void apply(QApplication &app);

//! Flat icon from :/icons/<name>.svg, tinted with the theme's text colour.
QIcon icon(const QString &name);

//! Best available monospace font family for code.
QString defaultCodeFont();

} // namespace Theme

#endif // ASMON_THEME_H
