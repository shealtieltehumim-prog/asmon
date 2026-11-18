/****************************************************************************
** Asmon — GPL-3.0.
**
** ReversePage: the page of the Reverse engineering mode. Three cards in a row
** (right to left in the Hebrew interface): functions / strings / imports of the
** opened program, its disassembly and hex view, and (a later stage) the
** decompiled code. The analysis itself is native/analysis.* (no Qt).
****************************************************************************/
#ifndef ASMON_REVERSEPAGE_H
#define ASMON_REVERSEPAGE_H

#include "native/analysis.h"

#include <QWidget>
#include <memory>

class HexView;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;

class ReversePage : public QWidget
{
    Q_OBJECT
public:
    explicit ReversePage(QWidget *parent = nullptr);
    ~ReversePage() override;

    //! Opens a PE file (exe, dll) and analyses it in the background.
    void openProgram(const QString &path);
    bool hasProgram() const { return bool(image_); }
    QString programPath() const;
    bool hasPatches() const;
    //! Asks for a file name and writes a copy of the program with the patches.
    bool saveCopy();
    //! Selects the function or address (a hex string, with or without 0x).
    bool goToAddress(const QString &text);

signals:
    void stateChanged();                  //!< a program was opened, or the patches changed
    void message(const QString &text);    //!< a line for the status bar

private:
    void analysisDone(bool ok, const QString &error, std::shared_ptr<ra::Image> image,
                      std::shared_ptr<ra::Result> result);
    void fillLists();
    void fillFunctions();
    void fillStrings();
    void fillImports();
    void fillInfo();
    void goTo(quint64 va, bool remember = true);
    void showFunction(const ra::Function &f);
    void showRegion(quint64 va);
    void showLines(const std::vector<ra::Line> &lines, const QString &title);
    void refreshDisassembly();
    void selectRow(quint64 va);
    quint64 currentVa() const;
    void followBranch();
    void goBack();
    void showXrefs(quint64 va);
    void renameAt(quint64 va);
    void commentAt(quint64 va);
    void patchAt(quint64 va);
    void nopAt(quint64 va);
    void revertAt(quint64 va);
    void disassemblyMenu(const QPoint &pos);
    void stringsMenu(const QPoint &pos);
    void loadProject();
    void saveProject();
    QString projectPath() const;
    QString addressText(quint64 va) const;

    std::shared_ptr<ra::Image> image_;
    std::shared_ptr<ra::Result> result_;
    QString path_;

    QTabWidget *listTabs_ = nullptr;
    QLabel *fileLabel_ = nullptr;
    QLineEdit *functionFilter_ = nullptr, *stringFilter_ = nullptr, *importFilter_ = nullptr;
    QListWidget *functionList_ = nullptr;
    QTableWidget *stringTable_ = nullptr, *importTable_ = nullptr;
    QPlainTextEdit *info_ = nullptr;
    QTabWidget *viewTabs_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QTableWidget *disassembly_ = nullptr;
    HexView *hex_ = nullptr;
    QLineEdit *hexGoto_ = nullptr;

    std::vector<ra::Line> lines_;
    std::vector<quint64> targets_;          //!< direct branch target of each shown line (0: none)
    quint64 currentFunction_ = 0;
    QList<quint64> history_;
    bool filling_ = false;
};

#endif // ASMON_REVERSEPAGE_H
