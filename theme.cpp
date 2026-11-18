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
        return { "#181818", "#1f1f1f", "#181818", "#2b2b2b", "#cccccc", "#8b8b8b",
                 "#0078d4", "#2a2d2e", "#264f78", "#ffffff", "#313131" };
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
        fg << QColor("#569cd6") << QColor("#c586c0") << QColor("#b5cea8") << QColor("#4fc1ff")
           << QColor("#dcdcaa") << QColor("#6a9955") << QColor("#4ec9b0") << QColor("#9cdcfe")
           << QColor("#ce9178");
    else
        fg << QColor("#0000ff") << QColor("#9a3cc8") << QColor("#098658") << QColor("#0070c1")
           << QColor("#795e26") << QColor("#008000") << QColor("#267f99") << QColor("#001080")
           << QColor("#a31515");
    for (int i = 0; i < 9; ++i) {
        m.insert(QString(kSyntaxKeys[i]) + "color", fg[i]);
        m.insert(QString(kSyntaxKeys[i]) + "colorbg", bg);
    }
    m.insert("backgroundcolor", bg);
    m.insert("fontcolor", QColor(dark ? "#d4d4d4" : "#1f1f1f"));
    m.insert("linenumberpanelcolor", bg);
    m.insert("linenumberfontcolor", QColor(dark ? "#6e7681" : "#8a8a8a"));
    m.insert("currentlinecolor", QColor(dark ? "#262626" : "#f2f6ff"));
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
    for (const char *f : { "Cascadia Mono", "Consolas", "JetBrains Mono", "DejaVu Sans Mono",
                           "Liberation Mono", "Courier New" }) {
        if (families.contains(QString(f)))
            return QString(f);
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

void ensureInitialized(QSettings &settings)
{
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
    p.setColor(QPalette::Dark, QColor(c.border));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(c.muted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c.muted));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(c.muted));
    app.setPalette(p);

    QString qss = QString(R"(
QMainWindow, QDialog { background: %1; }
QMainWindow::separator { background: %4; width: 1px; height: 1px; }
QMenuBar { background: %1; color: %5; border: none; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 4px; }
QMenuBar::item:selected { background: %8; }
QMenu { background: %3; color: %5; border: 1px solid %4; padding: 4px; }
QMenu::item { padding: 5px 24px; border-radius: 4px; }
QMenu::item:selected { background: %7; color: #ffffff; }
QMenu::item:disabled { color: %6; }
QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }
QToolBar { background: %1; border: none; spacing: 2px; padding: 2px 6px; }
QToolBar::separator { width: 1px; background: %4; margin: 6px 4px; }
QToolButton { background: transparent; border: none; border-radius: 4px; padding: 4px; }
QToolButton:hover { background: %8; }
QToolButton:pressed, QToolButton:checked { background: %4; }
QTabWidget::pane { border: none; border-top: 1px solid %4; }
QTabBar { background: %1; }
QTabBar::tab { background: %1; color: %6; padding: 6px 14px; border: none;
               border-bottom: 2px solid transparent; }
QTabBar::tab:selected { background: %2; color: %5; border-bottom: 2px solid %7; }
QTabBar::tab:hover:!selected { color: %5; background: %8; }
QDockWidget { color: %6; titlebar-close-icon: none; }
QDockWidget::title { background: %3; padding: 6px 8px; border-bottom: 1px solid %4; }
QPlainTextEdit, QTextEdit { border: none; selection-background-color: %9; }
QLineEdit, QSpinBox, QComboBox { background: %10; color: %5; border: 1px solid %4;
               border-radius: 4px; padding: 4px 6px; selection-background-color: %9; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid %7; }
QComboBox QAbstractItemView { background: %3; border: 1px solid %4; selection-background-color: %7; }
QPushButton { background: %10; color: %5; border: 1px solid %4; border-radius: 4px; padding: 5px 14px; }
QPushButton:hover { background: %8; }
QPushButton:default { background: %7; color: #ffffff; border: 1px solid %7; }
QTableWidget, QTableView, QListWidget, QTreeView { background: %2; alternate-background-color: %3;
               border: none; gridline-color: %4; selection-background-color: %9; }
QHeaderView::section { background: %3; color: %6; border: none; border-bottom: 1px solid %4;
               padding: 4px 6px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: %11; border-radius: 5px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: %6; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QSplitter::handle { background: %4; }
QStatusBar { background: %1; color: %6; border-top: 1px solid %4; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %6; padding: 0 8px; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::close-button { image: url(%12); subcontrol-position: right; border-radius: 3px; margin: 2px; }
QTabBar::close-button:hover { background: %8; }
QTabWidget#bottomPanel::pane { border: none; border-top: 1px solid %4; }
QTabWidget#bottomPanel QTabBar::tab { padding: 5px 12px; font-size: 9pt; }
QDockWidget > QWidget { border: none; }
QTreeView { outline: 0; }
QTreeView::item { padding: 3px 2px; border-radius: 4px; }
QTreeView::item:hover { background: %8; }
QTreeView::item:selected { background: %9; color: %5; }
QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }
QToolTip { background: %3; color: %5; border: 1px solid %4; padding: 4px; }
QCheckBox, QRadioButton, QLabel, QGroupBox { color: %5; }
QGroupBox { border: 1px solid %4; border-radius: 6px; margin-top: 10px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; right: 10px; padding: 0 4px; }
)")
        .arg(c.window, c.editor, c.panel, c.border, c.text, c.muted, c.accent, c.hover, c.selection)
        .arg(c.input, dark ? QString("#424242") : QString("#c4c4c4"), closeIconPath());
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
