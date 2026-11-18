#include "licensesdialog.h"
#include "common.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

QString LicensesDialog::licensesDir()
{
    QString env = qEnvironmentVariable("ASMON_LICENSES_DIR");
    if (!env.isEmpty())
        return env;
    const QStringList candidates = { Common::applicationDataPath() + "/licenses",
                                     QCoreApplication::applicationDirPath() + "/licenses" };
    for (const QString &c : candidates)
        if (QFileInfo(c).isDir())
            return c;
    return candidates.first();
}

LicensesDialog::LicensesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Licenses and credits"));
    setObjectName("licensesDialog");
    resize(980, 620);

    list_ = new QListWidget(this);
    list_->setMinimumWidth(270);
    list_->setMaximumWidth(380);
    view_ = new QTextBrowser(this);
    view_->setOpenExternalLinks(true);
    view_->setLineWrapMode(QTextEdit::WidgetWidth);

    QHBoxLayout *lay = new QHBoxLayout(this);
    lay->addWidget(list_);
    lay->addWidget(view_, 1);

    const QString dir = licensesDir();
    QStringList names = QDir(dir).entryList(QDir::Files, QDir::Name);
    // the overview first, then the versions, then the license texts
    auto take = [&names](const QString &n) { return names.removeAll(n) > 0; };
    auto add = [&](const QString &label, const QString &file) {
        QListWidgetItem *it = new QListWidgetItem(label, list_);
        it->setData(Qt::UserRole, dir + "/" + file);
        it->setToolTip(file);
    };
    if (take("THIRD_PARTY.md"))
        add(tr("Components and credits"), "THIRD_PARTY.md");
    if (take("VERSIONS.txt"))
        add(tr("Tool versions in this build"), "VERSIONS.txt");
    for (const QString &n : names)
        add(QFileInfo(n).completeBaseName(), n);

    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur) {
        if (cur)
            showFile(cur->data(Qt::UserRole).toString());
    });

    if (list_->count() == 0) {
        view_->setPlainText(tr("The licenses folder was not found (%1).\n\n"
                               "The full list of components and their licenses is at\n"
                               "https://github.com/shealtieltehumim-prog/asmon/blob/main/THIRD_PARTY.md")
                            .arg(QDir::toNativeSeparators(dir)));
    } else {
        list_->setCurrentRow(0);
    }
}

void LicensesDialog::showFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        view_->setPlainText(tr("Could not open %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    const QString text = QString::fromUtf8(f.readAll());
    if (path.endsWith(".md", Qt::CaseInsensitive)) {
        view_->setMarkdown(text);
    } else {
        view_->setPlainText(text);
    }
    // license texts are English / LTR technical text even in the Hebrew interface
    view_->document()->setDefaultTextOption(QTextOption(Qt::AlignLeft));
    view_->setLayoutDirection(Qt::LeftToRight);
}
