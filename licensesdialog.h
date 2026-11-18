#ifndef LICENSESDIALOG_H
#define LICENSESDIALOG_H

#include <QDialog>

class QListWidget;
class QPlainTextEdit;
class QTextBrowser;

/**
 * Asmon: "Licenses and credits" screen (Help menu). Shows the files of the
 * <app>/licenses folder: the component list with credits (THIRD_PARTY.md), the exact tool
 * versions of this build (VERSIONS.txt) and the full license texts of every bundled component.
 * ASMON_LICENSES_DIR overrides the folder (tests).
 */
class LicensesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LicensesDialog(QWidget *parent = nullptr);
    static QString licensesDir();

private:
    void showFile(const QString &path);

    QListWidget *list_;
    QTextBrowser *view_;
};

#endif // LICENSESDIALOG_H
