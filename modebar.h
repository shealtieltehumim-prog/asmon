/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** ModeBar: the row under the menu with the three modes of the window
** (Write | Debug | Reverse engineering) and, at its other end, the action
** buttons of the active mode. ReversePage: the page of the third mode.
****************************************************************************/
#ifndef ASMON_MODEBAR_H
#define ASMON_MODEBAR_H

#include <QList>
#include <QPair>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QToolButton;

class ModeBar : public QWidget
{
    Q_OBJECT
public:
    enum Mode { Write = 0, Debug = 1, Reverse = 2 };

    explicit ModeBar(QWidget *parent = nullptr);

    Mode mode() const { return m_mode; }
    QString modeName(int m) const;   //!< the (translated) text of a mode's tab
    //! Switches the active tab and shows only the buttons of that mode.
    void setMode(Mode mode);

    //! A button for an action (it follows the action's text, icon and enabled state),
    //! shown only while `mode` is active. The same action may be added to several modes.
    void addAction(Mode mode, QAction *action);
    //! Any other widget, shown only while `mode` is active.
    void addWidget(Mode mode, QWidget *widget);

signals:
    void modeChanged(int mode);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *e) override;

private:
    void showButtonsOf(Mode mode);
    //! Narrow windows: when the tabs and the buttons of the mode do not fit with their labels, the
    //! action buttons show only their icon (the label moves to the tooltip). Wide windows are unchanged.
    void updateCompact();

    bool m_compact = false;
    bool m_updating = false;

    Mode m_mode = Write;
    QToolButton *m_tabs[3] = { nullptr, nullptr, nullptr };
    QHBoxLayout *m_buttons = nullptr;
    QList<QPair<Mode, QWidget *>> m_items;
};

#endif // ASMON_MODEBAR_H
