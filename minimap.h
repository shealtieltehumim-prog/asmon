/****************************************************************************
** Asmon — fork of SASM (GPL-3.0).
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#ifndef MINIMAP_H
#define MINIMAP_H

#include <QImage>
#include <QTimer>
#include <QWidget>

class CodeEditor;

/**
 * @file minimap.h
 * VS Code's minimap: the whole file drawn small on the right of the code (one line = 2 px, one
 * character = 1 px, in the syntax colours), with the visible part shaded. A click jumps there, the
 * shaded part can be dragged. Folded lines are left out, build errors show as red lines. The picture
 * is rebuilt a moment after the text or its colours change, not on every paint.
 */
class Minimap : public QWidget
{
public:
    explicit Minimap(CodeEditor *editor);
    static constexpr int kWidth = 96;     //!< logical pixels
    static constexpr int kLine = 2;       //!< pixels per line
    void scheduleRebuild();
    //! for the self test: the scroll value a click at y would give
    int scrollValueForY(int y) const;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void rebuild();
    int contentHeight() const { return m_lines * kLine; }
    int imageOffset() const;              //!< the first image row shown at the top of the widget
    QRect sliderRect() const;

    CodeEditor *m_editor;
    QImage m_image;
    int m_lines = 0;
    QTimer m_timer;
    bool m_dragging = false;
    int m_dragDelta = 0;
    bool m_hover = false;
};

#endif
