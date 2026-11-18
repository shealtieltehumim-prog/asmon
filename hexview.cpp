#include "hexview.h"

#include "theme.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

namespace {
constexpr int kBytesPerRow = 16;
// character columns: "00000000", two spaces, 16 x "xx " (and a gap in the middle), two spaces, ascii
constexpr int kOffsetChars = 8;
constexpr int kHexStart = kOffsetChars + 2;
int hexColumn(int i) { return kHexStart + i * 3 + (i >= 8 ? 1 : 0); }
constexpr int kAsciiStart = kHexStart + kBytesPerRow * 3 + 1 + 2;
}

HexView::HexView(QWidget *parent) : QAbstractScrollArea(parent)
{
    setLayoutDirection(Qt::LeftToRight);   // bytes and addresses read left to right in every language
    setFont(Theme::codeFont());
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setCursor(Qt::IBeamCursor);
    const QFontMetrics fm(font());
    charWidth_ = fm.horizontalAdvance(QLatin1Char('0'));
    lineHeight_ = fm.height() + 2;
    verticalScrollBar()->setSingleStep(1);
}

void HexView::setImage(std::shared_ptr<ra::Image> image)
{
    image_ = std::move(image);
    cursor_ = 0;
    pendingNibble_ = -1;
    updateScrollBar();
    viewport()->update();
}

void HexView::updateScrollBar()
{
    const int rows = int((size() + kBytesPerRow - 1) / kBytesPerRow);
    const int visible = std::max(1, viewport()->height() / lineHeight_);
    verticalScrollBar()->setRange(0, std::max(0, rows - visible));
    verticalScrollBar()->setPageStep(visible);
}

void HexView::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBar();
}

void HexView::gotoOffset(quint64 offset)
{
    if (!image_ || size() == 0)
        return;
    moveCursor(qint64(std::min<quint64>(offset, size() - 1)));
}

void HexView::moveCursor(qint64 offset)
{
    if (!image_ || size() == 0)
        return;
    offset = std::max<qint64>(0, std::min<qint64>(offset, qint64(size()) - 1));
    cursor_ = quint64(offset);
    pendingNibble_ = -1;
    const int row = int(cursor_ / kBytesPerRow);
    const int visible = std::max(1, viewport()->height() / lineHeight_);
    int top = verticalScrollBar()->value();
    if (row < top || row >= top + visible)
        top = std::max(0, row - visible / 3);
    verticalScrollBar()->setValue(top);
    viewport()->update();
    emit cursorMoved(cursor_);
}

void HexView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), Theme::color("backgroundcolor"));
    if (!image_)
        return;
    p.setFont(font());
    const QColor text = Theme::color("fontcolor");
    QColor muted = text;
    muted.setAlpha(140);
    const QColor changed = Theme::isDark() ? QColor("#f9c74f") : QColor("#b5590e");
    const QColor sel = palette().color(QPalette::Highlight);
    const int firstRow = verticalScrollBar()->value();
    const int rows = viewport()->height() / lineHeight_ + 2;
    const int ascent = QFontMetrics(font()).ascent();
    for (int r = 0; r < rows; ++r) {
        const quint64 offset = quint64(firstRow + r) * kBytesPerRow;
        if (offset >= size())
            break;
        const int y = r * lineHeight_;
        uint8_t row[kBytesPerRow];
        const size_t got = image_->readFile(offset, row, kBytesPerRow);
        p.setPen(muted);
        p.drawText(0, y + ascent + 1, QString("%1").arg(offset, kOffsetChars, 16, QLatin1Char('0')));
        for (size_t i = 0; i < got; ++i) {
            const quint64 at = offset + i;
            uint8_t orig = row[i];
            image_->original(at, orig);
            const bool isPatched = orig != row[i];
            const int hx = hexColumn(int(i)) * charWidth_, ax = (kAsciiStart + int(i)) * charWidth_;
            if (at == cursor_) {
                p.fillRect(hx - 1, y, charWidth_ * 2 + 2, lineHeight_, sel);
                p.fillRect(ax, y, charWidth_, lineHeight_, sel);
            }
            p.setPen(isPatched ? changed : text);
            const QString hex = (at == cursor_ && pendingNibble_ >= 0)
                                    ? QString("%1_").arg(pendingNibble_, 1, 16)
                                    : QString("%1").arg(row[i], 2, 16, QLatin1Char('0'));
            p.drawText(hx, y + ascent + 1, hex);
            const QChar ch = (row[i] >= 0x20 && row[i] < 0x7f) ? QChar(row[i]) : QChar('.');
            p.setPen(isPatched ? changed : (ch == '.' ? muted : text));
            p.drawText(ax, y + ascent + 1, QString(ch));
        }
    }
}

void HexView::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (!image_)
        return;
    const int row = verticalScrollBar()->value() + e->position().toPoint().y() / lineHeight_;
    const int col = e->position().toPoint().x() / charWidth_;
    int index = -1;
    if (col >= kAsciiStart && col < kAsciiStart + kBytesPerRow) {
        index = col - kAsciiStart;
    } else {
        for (int i = 0; i < kBytesPerRow; ++i)
            if (col >= hexColumn(i) && col < hexColumn(i) + 3)
                index = i;
    }
    if (index >= 0)
        moveCursor(qint64(row) * kBytesPerRow + index);
}

void HexView::keyPressEvent(QKeyEvent *e)
{
    if (!image_) {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    const int page = std::max(1, viewport()->height() / lineHeight_) * kBytesPerRow;
    switch (e->key()) {
    case Qt::Key_Left: moveCursor(qint64(cursor_) - 1); return;
    case Qt::Key_Right: moveCursor(qint64(cursor_) + 1); return;
    case Qt::Key_Up: moveCursor(qint64(cursor_) - kBytesPerRow); return;
    case Qt::Key_Down: moveCursor(qint64(cursor_) + kBytesPerRow); return;
    case Qt::Key_PageUp: moveCursor(qint64(cursor_) - page); return;
    case Qt::Key_PageDown: moveCursor(qint64(cursor_) + page); return;
    case Qt::Key_Home: moveCursor(qint64(cursor_ / kBytesPerRow) * kBytesPerRow); return;
    case Qt::Key_End: moveCursor(qint64(cursor_ / kBytesPerRow) * kBytesPerRow + kBytesPerRow - 1); return;
    case Qt::Key_Delete:
        image_->revertPatch(cursor_);
        pendingNibble_ = -1;
        viewport()->update();
        emit patched();
        return;
    case Qt::Key_Escape:
        pendingNibble_ = -1;
        viewport()->update();
        return;
    default:
        break;
    }
    const QString t = e->text();
    if (t.size() == 1 && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        bool ok = false;
        const int v = t.toInt(&ok, 16);
        if (ok) {
            if (pendingNibble_ < 0) {
                pendingNibble_ = v;
            } else {
                image_->patch(cursor_, { uint8_t((pendingNibble_ << 4) | v) });
                pendingNibble_ = -1;
                moveCursor(qint64(cursor_) + 1);
                emit patched();
            }
            viewport()->update();
            return;
        }
    }
    QAbstractScrollArea::keyPressEvent(e);
}
