/****************************************************************************
** Asmon — GPL-3.0.
**
** HexView: the bytes of a program file as a classic hex editor (offset, 16
** bytes in hex, ASCII). Typing two hex digits changes a byte; the change is a
** patch kept in memory (see ra::Image) and shown in a different colour, and is
** only written when the user saves a copy of the file.
****************************************************************************/
#ifndef ASMON_HEXVIEW_H
#define ASMON_HEXVIEW_H

#include "native/analysis.h"

#include <QAbstractScrollArea>
#include <memory>

class HexView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit HexView(QWidget *parent = nullptr);
    void setImage(std::shared_ptr<ra::Image> image);
    void gotoOffset(quint64 offset);
    quint64 cursorOffset() const { return cursor_; }
    //! the bytes changed from outside (a patch made in the disassembly)
    void refresh() { viewport()->update(); }

signals:
    void patched();
    void cursorMoved(quint64 offset);

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void updateScrollBar();
    void moveCursor(qint64 offset);
    quint64 size() const { return image_ ? image_->fileSize() : 0; }

    std::shared_ptr<ra::Image> image_;
    quint64 cursor_ = 0;
    int pendingNibble_ = -1;     //!< the first typed hex digit of a byte, or -1
    int charWidth_ = 8, lineHeight_ = 16;
};

#endif // ASMON_HEXVIEW_H
