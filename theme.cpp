/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QPalette>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QSettings>
#include <QStyleFactory>

namespace {

struct Chrome {
    QString window, editor, panel, border, text, muted, accent, hover, selection, selectionText, input;
};

Chrome chrome(bool dark)
{
    if (dark)
        // true black: the editor is #000000 and the frame around it is almost black
        return { "#111111", "#000000", "#0c0c0c", "#2b2b2b", "#cccccc", "#9d9d9d",
                 "#0078d4", "#2a2d2e", "#264f78", "#ffffff", "#1b1b1b" };
    return { "#f8f8f8", "#ffffff", "#f8f8f8", "#e5e5e5", "#1f1f1f", "#6e6e6e",
             "#005fb8", "#e8e8e8", "#add6ff", "#000000", "#ffffff" };
}

const char *kSyntaxKeys[] = { "keywords", "registers", "numbers", "memory", "labels",
                              "comments", "system", "iomacro", "quotation" };

} // namespace

namespace Theme {

QString current()
{
    return QSettings().value("theme", "dark").toString();
}

bool isDark()
{
    return current() != "light";
}

QMap<QString, QColor> editorDefaults(bool dark)
{
    QMap<QString, QColor> m;
    const Chrome c = chrome(dark);
    const QColor bg(c.editor);
    QList<QColor> fg;
    if (dark)
        // neon on black (the owner's Zed colours): instructions and directives magenta, registers cyan,
        // numbers orange, label definitions yellow; strings, I/O macros and comments chosen to match
        fg << QColor("#ff2bff") << QColor("#00f0ff") << QColor("#ff9100") << QColor("#00f0ff")
           << QColor("#ffff00") << QColor("#6b7a80") << QColor("#ff2bff") << QColor("#bf5fff")
           << QColor("#39ff14");
    else
        fg << QColor("#0050e6") << QColor("#c2189c") << QColor("#d45a00") << QColor("#0086b3")
           << QColor("#8a6d00") << QColor("#2e7d32") << QColor("#00897b") << QColor("#6f42c1")
           << QColor("#d32f2f");
    for (int i = 0; i < 9; ++i) {
        m.insert(QString(kSyntaxKeys[i]) + "color", fg[i]);
        m.insert(QString(kSyntaxKeys[i]) + "colorbg", bg);
    }
    m.insert("backgroundcolor", bg);
    m.insert("fontcolor", QColor(dark ? "#d4d4d4" : "#1f1f1f"));
    //! plain text in the code editor only (names, punctuation); the log and the other panels keep fontcolor
    m.insert("codetextcolor", QColor(dark ? "#00f0ff" : "#1f1f1f"));
    m.insert("linenumberpanelcolor", bg);
    m.insert("linenumberfontcolor", QColor(dark ? "#4e5a5f" : "#6e7681"));
    m.insert("linenumberactivecolor", QColor(dark ? "#d0d4da" : "#1f1f1f"));
    m.insert("currentlinecolor", QColor(dark ? "#111111" : "#f2f6ff"));
    m.insert("debuglinecolor", QColor(dark ? "#4b4214" : "#ffe9a8"));
    m.insert("successcolor", QColor(dark ? "#4ec9a0" : "#107c10"));
    m.insert("errorcolor", QColor(dark ? "#f48771" : "#c72e2e"));
    return m;
}

QColor color(const QString &key)
{
    QSettings s;
    const QVariant v = s.value(key);
    if (!v.isValid())
        return editorDefaults(isDark()).value(key, QColor(Qt::black));
    if (v.typeId() == QMetaType::QString)
        return QColor(v.toString());
    return v.value<QColor>(); // settings written by older versions

}

void setTheme(QSettings &settings, bool dark)
{
    settings.setValue("theme", dark ? "dark" : "light");
    const QMap<QString, QColor> d = editorDefaults(dark);
    for (auto it = d.constBegin(); it != d.constEnd(); ++it)
        settings.setValue(it.key(), it.value().name()); // "#rrggbb": readable INI
    settings.setValue("currentlinemode", true);
    //! the neon dark theme uses no bold and no italic; the light theme keeps them
    for (const char *k : { "keywords", "labels", "system" })
        settings.setValue(QString(k) + "bold", !dark);
    settings.setValue("commentsitalic", !dark);
}

//! QSS can only load images from files: the tinted close icon is written
//! next to the settings (one file per theme).
static QString closeIconPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QString("/close-%1.png").arg(isDark() ? "dark" : "light");
    if (!QFile::exists(path))
        icon("close").pixmap(32, 32).save(path, "PNG");
    return QDir::fromNativeSeparators(path);
}

QIcon icon(const QString &name)
{
    QFile f(":/icons/" + name + ".svg");
    if (!f.open(QIODevice::ReadOnly))
        return QIcon();
    const QByteArray svg = f.readAll();
    auto render = [&svg](const QString &tint, int px, bool monochrome) {
        QString text = QString::fromUtf8(svg);
        text.replace("currentColor", tint);
        if (monochrome) // disabled state: grey out the accent colours too
            text.replace(QRegularExpression("#[0-9a-fA-F]{6}"), tint);
        const QByteArray data = text.toUtf8();
        QSvgRenderer r(data);
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        r.render(&p);
        return pm;
    };
    const bool dark = isDark();
    QIcon ic;
    for (int px : { 16, 20, 24, 32, 40, 48 }) {
        ic.addPixmap(render(dark ? "#c5c5c5" : "#424242", px, false), QIcon::Normal);
        ic.addPixmap(render(dark ? "#5a5a5a" : "#b8b8b8", px, true), QIcon::Disabled);
    }
    return ic;
}

QString defaultCodeFont()
{
    const QStringList families = QFontDatabase::families();
    //! Cascadia Mono: Visual Studio's editor font. Bundled (fonts/, OFL) with Hebrew letters, so every
    //! machine gets the same font; Consolas (VS Code on Windows) only if the bundled one failed to load.
    for (const char *f : { "Cascadia Mono", "Consolas", "JetBrains Mono", "DejaVu Sans Mono",
                           "Liberation Mono", "Courier New" }) {
        if (families.contains(QString(f)))
            return QString(f);
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

static bool rubikLoaded = false;

void loadBundledFonts()
{
    for (const char *f : { ":/fonts/Rubik-Regular.ttf", ":/fonts/Rubik-Medium.ttf", ":/fonts/Rubik-Bold.ttf" })
        rubikLoaded = (QFontDatabase::addApplicationFont(f) >= 0) || rubikLoaded;
    for (const char *f : { ":/fonts/JetBrainsMono-Regular.ttf", ":/fonts/JetBrainsMono-Bold.ttf",
                           ":/fonts/JetBrainsMono-Italic.ttf" })
        QFontDatabase::addApplicationFont(f);
    //! More monospace fonts to choose from in Settings (all free licenses, see fonts/*-OFL.txt).
    for (const char *f : { ":/fonts/CascadiaCode-Regular.ttf", ":/fonts/CascadiaMono-Regular.ttf",
                           ":/fonts/CascadiaMono-Bold.ttf", ":/fonts/SourceCodePro-Regular.otf",
                           ":/fonts/SourceCodePro-Bold.otf", ":/fonts/Hack-Regular.ttf",
                           ":/fonts/Hack-Bold.ttf", ":/fonts/FiraCode-Regular.ttf",
                           ":/fonts/FiraCode-Bold.ttf" })
        QFontDatabase::addApplicationFont(f);
}

QFont uiFont()
{
    QFont f = QApplication::font();
    if (rubikLoaded) {
        f.setFamilies(QStringList() << "Rubik" << f.family());
        f.setPointSizeF(10);
    }
    f.setHintingPreference(QFont::PreferVerticalHinting);
    return f;
}

QFont codeFont(int pointSize)
{
    QSettings settings;
    QFont f;
    f.setFamilies(QStringList() << settings.value("fontfamily", defaultCodeFont()).toString()
                                << "JetBrains Mono" << "Rubik");
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(pointSize > 0 ? pointSize : settings.value("fontsize", 11).toInt());
    f.setHintingPreference(QFont::PreferVerticalHinting);
    return f;
}

void ensureInitialized(QSettings &settings)
{
    //! a new installation starts on the current default font: the old one-time migrations below were
    //! written for existing users and moved a fresh, empty profile to JetBrains Mono first
    if (!settings.contains("fontversion") && !settings.contains("fontfamily")) {
        settings.setValue("fontfamily", defaultCodeFont());
        settings.setValue("fontversion", 5);
    }
    //! fonts v2: JetBrains Mono is bundled; move users off the old automatic choice
    if (settings.value("fontversion", 1).toInt() < 2) {
        const QString f = settings.value("fontfamily").toString();
        if (f.isEmpty() || QStringList({ "Cascadia Mono", "Consolas", "DejaVu Sans Mono", "Liberation Mono",
                                         "Courier New", "Courier" }).contains(f))
            settings.setValue("fontfamily", "JetBrains Mono");
        settings.setValue("fontversion", 2);
    }
    //! fonts v3: Consolas (when installed) is the default, like VS Code; only moves users still on the
    //! automatic JetBrains Mono choice, a deliberate pick is kept.
    if (settings.value("fontversion", 1).toInt() < 3) {
        if (settings.value("fontfamily").toString() == "JetBrains Mono" && QFontDatabase::families().contains("Consolas"))
            settings.setValue("fontfamily", "Consolas");
        settings.setValue("fontversion", 3);
    }
    //! fonts v4: the owner asked that the code font be the VS Code one. One-time: everyone moves to
    //! Consolas when it is installed (Windows); a font picked in Settings after this is kept.
    if (settings.value("fontversion", 1).toInt() < 4) {
        if (QFontDatabase::families().contains("Consolas"))
            settings.setValue("fontfamily", "Consolas");
        settings.setValue("fontversion", 4);
    }
    //! fonts v5: Visual Studio's Cascadia Mono (bundled, Hebrew included) replaces the v4 default;
    //! only users still on that automatic choice move.
    if (settings.value("fontversion", 1).toInt() < 5) {
        const QString f = settings.value("fontfamily").toString();
        if ((f.isEmpty() || f == "Consolas") && QFontDatabase::families().contains("Cascadia Mono"))
            settings.setValue("fontfamily", "Cascadia Mono");
        settings.setValue("fontversion", 5);
    }
    //! colours v2: stronger syntax colours; only replaces colours that are still the old defaults
    if (settings.value("colorversion", 1).toInt() < 2) {
        static const char *oldDark[] = { "#569cd6", "#c586c0", "#b5cea8", "#4fc1ff", "#dcdcaa", "#6a9955", "#4ec9b0", "#9cdcfe", "#ce9178" };
        static const char *oldLight[] = { "#0000ff", "#9a3cc8", "#098658", "#0070c1", "#795e26", "#008000", "#267f99", "#001080", "#a31515" };
        const QMap<QString, QColor> nd = editorDefaults(true), nl = editorDefaults(false);
        for (int i = 0; i < 9; ++i) {
            const QString key = QString(kSyntaxKeys[i]) + "color";
            const QString cur = settings.value(key).toString().toLower();
            if (cur == oldDark[i])
                settings.setValue(key, nd.value(key).name());
            else if (cur == oldLight[i])
                settings.setValue(key, nl.value(key).name());
        }
        for (const char *k : { "labels", "system" })
            if (!settings.contains(QString(k) + "bold"))
                settings.setValue(QString(k) + "bold", true);
        if (!settings.contains("commentsitalic"))
            settings.setValue("commentsitalic", true);
        settings.setValue("colorversion", 2);
    }
    //! colours v3: the dark theme is true black. Only replaces values that are still the old dark
    //! defaults (a background picked in Settings or an imported theme is kept).
    if (settings.value("colorversion", 1).toInt() < 3) {
        if (settings.value("theme", "dark").toString() != "light") {
            const QMap<QString, QColor> nd = editorDefaults(true);
            QStringList keys = { "backgroundcolor", "linenumberpanelcolor" };
            for (const char *k : kSyntaxKeys)
                keys << QString(k) + "colorbg";
            for (const QString &key : keys)
                if (settings.value(key).toString().toLower() == "#1f1f1f")
                    settings.setValue(key, nd.value(key).name());
            if (settings.value("currentlinecolor").toString().toLower() == "#262626")
                settings.setValue("currentlinecolor", nd.value("currentlinecolor").name());
        }
        settings.setValue("colorversion", 3);
    }
    //! colours v4: the dark theme is neon on black. Only replaces values that are still the previous
    //! dark defaults; an imported theme keeps its own text colour in the editor.
    if (settings.value("colorversion", 1).toInt() < 4) {
        if (settings.value("theme", "dark").toString() != "light" && settings.contains("theme")) {
            static const char *prev[] = { "#3794ff", "#ff79c6", "#ffb86c", "#35d4ff", "#f1fa5c", "#6a9955", "#2de2b8", "#bd93f9", "#ff6e6e" };
            const QMap<QString, QColor> nd = editorDefaults(true);
            bool allDefault = true;
            for (int i = 0; i < 9; ++i) {
                const QString key = QString(kSyntaxKeys[i]) + "color";
                if (settings.value(key).toString().toLower() == prev[i])
                    settings.setValue(key, nd.value(key).name());
                else
                    allDefault = false;
            }
            auto move = [&](const char *key, std::initializer_list<const char *> olds) {
                const QString cur = settings.value(key).toString().toLower();
                for (const char *o : olds)
                    if (cur == o)
                        settings.setValue(key, nd.value(key).name());
            };
            move("linenumberfontcolor", { "#858585" });
            move("currentlinecolor", { "#121212", "#262626" });
            const QString fg = settings.value("fontcolor").toString().toLower();
            if (!settings.contains("codetextcolor") && !fg.isEmpty() && fg != "#d4d4d4")
                settings.setValue("codetextcolor", fg);       // an imported theme's own text colour
            if (allDefault) {                                  // the stock theme: no bold, no italic
                for (const char *k : { "keywords", "labels", "system" })
                    settings.setValue(QString(k) + "bold", false);
                settings.setValue("commentsitalic", false);
            }
        }
        settings.setValue("colorversion", 4);
    }
    if (settings.contains("theme"))
        return;
    setTheme(settings, true);
    if (!settings.contains("fontfamily"))
        settings.setValue("fontfamily", defaultCodeFont());
    if (!settings.contains("fontsize"))
        settings.setValue("fontsize", 11);
}

void apply(QApplication &app)
{
    const bool dark = isDark();
    const Chrome c = chrome(dark);

    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, QColor(c.window));
    p.setColor(QPalette::WindowText, QColor(c.text));
    p.setColor(QPalette::Base, QColor(c.editor));
    p.setColor(QPalette::AlternateBase, QColor(c.panel));
    p.setColor(QPalette::ToolTipBase, QColor(c.panel));
    p.setColor(QPalette::ToolTipText, QColor(c.text));
    p.setColor(QPalette::PlaceholderText, QColor(c.muted));
    p.setColor(QPalette::Text, QColor(c.text));
    p.setColor(QPalette::Button, QColor(c.input));
    p.setColor(QPalette::ButtonText, QColor(c.text));
    p.setColor(QPalette::BrightText, QColor("#f14c4c"));
    p.setColor(QPalette::Link, QColor(c.accent));
    p.setColor(QPalette::Highlight, QColor(c.selection));
    p.setColor(QPalette::HighlightedText, QColor(c.selectionText));
    p.setColor(QPalette::Mid, QColor(c.border));
    p.setColor(QPalette::Light, QColor(c.border));    // Fusion draws tab-bar bases and frames with Light (white by default)
    p.setColor(QPalette::Midlight, QColor(c.border));
    p.setColor(QPalette::Dark, QColor(c.border));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(c.muted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c.muted));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(c.muted));
    app.setPalette(p);

    //! Asmon: VS Code look. Flat menus, tabs and buttons (no pills, no rounded cards), 1 px separators,
    //! a blue status bar (orange while debugging), square scroll bars, 2-4 px corners on inputs.
    QString qss = QString(R"(
QMainWindow, QDialog { background: %1; }
QMainWindow::separator { background: %4; width: 1px; height: 1px; }
QMenuBar { background: %1; color: %5; border: none; padding: 0px 4px; }
QMenuBar::item { background: transparent; padding: 5px 9px; margin: 3px 0px; border-radius: 4px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: %8; color: %5; }
QMenu { background: %3; color: %5; border: 1px solid %4; padding: 4px 0px; }
QMenu::item { padding: 5px 30px 5px 22px; margin: 0px 4px; border-radius: 3px; }
QMenu::item:selected { background: %7; color: #ffffff; }
QMenu::item:disabled { color: %6; }
QMenu::separator { height: 1px; background: %4; margin: 4px 0px; }
QToolBar { background: %1; border: none; spacing: 2px; padding: 2px 6px; }
QToolBar::separator { width: 1px; background: %4; margin: 6px 4px; }
QToolButton { background: transparent; color: %5; border: 1px solid transparent; border-radius: 4px; padding: 4px; margin: 1px; }
QToolButton:hover { background: %8; }
QToolButton:pressed, QToolButton:checked { background: %8; }
QWidget#modeBar { background: %1; border-bottom: 1px solid %4; }
QToolButton#modeTab { color: %6; background: transparent; border: none; border-bottom: 2px solid transparent;
               border-radius: 0px; padding: 8px 14px 6px 14px; margin: 0px; }
QToolButton#modeTab:hover { color: %5; background: transparent; }
QToolButton#modeTab:checked { color: %5; background: transparent; border-bottom: 2px solid %7; }
QToolButton#modeAction { padding: 3px 10px; margin: 3px 1px; }
QToolButton#modeAction[compact="true"] { padding: 3px 6px; margin: 3px 0px; }
QToolButton#modeAction:disabled { color: %6; }
QLabel#cardHeading { color: %6; font-size: 9pt; }
QLabel#cardHint { color: %6; }
QTabWidget::pane { border: none; background: transparent; }
QTabBar { background: %1; }
QTabBar::tab { background: %1; color: %6; padding: 7px 12px 7px 14px; margin: 0px; border: none;
               border-top: 1px solid transparent; border-left: 1px solid %4; border-radius: 0px; min-height: 18px; }
QTabBar::tab:selected { background: %2; color: %5; border-top: 1px solid %7; }
QTabBar::tab:hover:!selected { color: %5; background: %13; }
QDockWidget { color: %6; titlebar-close-icon: none; }
QDockWidget::title { background: %3; padding: 6px 10px; border-bottom: 1px solid %4; }
QPlainTextEdit, QTextEdit { border: none; selection-background-color: %9; }
QLineEdit, QSpinBox, QComboBox { background: %10; color: %5; border: 1px solid %4;
               border-radius: 3px; padding: 4px 8px; selection-background-color: %9; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid %7; }
QComboBox QAbstractItemView { background: %3; border: 1px solid %4; selection-background-color: %7; }
QPushButton { background: %10; color: %5; border: 1px solid %4; border-radius: 3px; padding: 5px 16px; }
QPushButton:hover { background: %8; }
QPushButton:default { background: %7; color: #ffffff; border: 1px solid %7; }
QTableWidget, QTableView, QListWidget, QTreeView { background: %2; alternate-background-color: %3;
               border: none; gridline-color: %4; selection-background-color: %9; }
QHeaderView::section { background: %3; color: %6; border: none; border-bottom: 1px solid %4;
               padding: 4px 6px; }
QScrollBar:vertical { background: transparent; width: 14px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 14px; margin: 0; }
QScrollBar::handle:vertical { background: %11; border-radius: 0px; margin: 0px; min-height: 30px; }
QScrollBar::handle:horizontal { background: %11; border-radius: 0px; margin: 0px; min-width: 30px; }
QScrollBar::handle:hover { background: %6; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QSplitter::handle { background: %4; }
QStatusBar { background: #007acc; color: #ffffff; border: none; min-height: 22px; }
QStatusBar[debugging="true"] { background: #cc6633; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #ffffff; padding: 0 8px; }
QStatusBar QComboBox { background: transparent; border: none; color: #ffffff; padding: 0 6px; min-height: 0; }
QStatusBar QComboBox:hover, QStatusBar QToolButton:hover { background: rgba(255,255,255,40); color: #ffffff; border-radius: 0px; }
QStatusBar QToolButton { color: #ffffff; border-radius: 0px; margin: 0px; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::close-button { image: url(%12); subcontrol-position: right; border-radius: 3px; margin: 2px; }
QTabBar::close-button:hover { background: %8; }
QTabWidget#bottomPanel { background: %3; }
QTabWidget#bottomPanel::pane { border: none; background: %3; }
QTabWidget#bottomPanel QTabBar { background: %3; }
QTabWidget#bottomPanel QTabBar::tab { background: transparent; color: %6; border: none; border-bottom: 1px solid transparent;
               padding: 7px 2px 5px 2px; margin: 0px 9px; font-size: 9pt; }
QTabWidget#bottomPanel QTabBar::tab:selected { color: %5; border-bottom: 1px solid %7; background: transparent; }
QTabWidget#bottomPanel QTabBar::tab:hover:!selected { color: %5; background: transparent; }
QDockWidget > QWidget { border: none; }
QTreeView { outline: 0; }
QTreeView::item { padding: 3px 2px; border-radius: 0px; }
QTreeView::item:hover { background: %8; }
QTreeView::item:selected { background: %9; color: %5; }
QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }
QMessageBox { min-width: 480px; min-height: 170px; }
QMessageBox QLabel { min-width: 340px; font-size: 11pt; padding: 6px 4px; }
QMessageBox QPushButton, QDialogButtonBox QPushButton { min-width: 96px; min-height: 30px; font-size: 10pt; }
QProgressDialog { min-width: 460px; min-height: 130px; }
QToolTip { background: %3; color: %5; border: 1px solid %4; padding: 6px 8px; }
QCheckBox, QRadioButton, QLabel, QGroupBox { color: %5; }
QGroupBox { border: 1px solid %4; border-radius: 4px; margin-top: 12px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; right: 10px; padding: 0 4px; }
)")
        .arg(c.window, c.editor, c.panel, c.border, c.text, c.muted, c.accent, c.hover, c.selection)
        .arg(c.input, dark ? QString("#353535") : QString("#c9c9c9"), closeIconPath())
        .arg(dark ? QString("#161616") : QString("#ececec"));
    app.setStyleSheet(qss);
}

void recolorDocument(QTextDocument *doc, const QHash<QRgb, QColor> &map)
{
    if (!doc || map.isEmpty())
        return;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || !f.charFormat().hasProperty(QTextFormat::ForegroundBrush))
                continue;
            const QRgb old = f.charFormat().foreground().color().rgb();
            if (!map.contains(old))
                continue;
            QTextCursor c(doc);
            c.setPosition(f.position());
            c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
            QTextCharFormat fmt;
            fmt.setForeground(map.value(old));
            c.mergeCharFormat(fmt);
        }
    }
}

} // namespace Theme
