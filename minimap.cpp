/****************************************************************************
** Asmon — fork of SASM (GPL-3.0).
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "minimap.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSettings>
#include <QTextBlock>
#include <QTextLayout>

#include "codeeditor.h"
#include "theme.h"

Minimap::Minimap(CodeEditor *editor) : QWidget(editor), m_editor(editor)
{
    setLayoutDirection(Qt::LeftToRight);
    setCursor(Qt::ArrowCursor);
    setMouseTracking(true);
    m_timer.setSingleShot(true);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        rebuild();
        update();
    });
    //! the text, its colours (the highlighter re-lays blocks out) and folding change the picture
    connect(editor->document(), &QTextDocument::contentsChanged, this, &Minimap::scheduleRebuild);
    connect(editor->document()->documentLayout(), &QAbstractTextDocumentLayout::update, this,
            [this](const QRectF &) { scheduleRebuild(); });
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { update(); });
    connect(editor->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) { update(); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this]() { update(); });
    scheduleRebuild();
}

void Minimap::scheduleRebuild()
{
    if (!m_timer.isActive())
        m_timer.start();
}

void Minimap::rebuild()
{
    QTextDocument *doc = m_editor->document();
    int lines = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (b.isVisible())
            ++lines;
    m_lines = lines;
    const int h = qBound(1, lines * kLine, 32000);
    m_image = QImage(kWidth, h, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::transparent);

    const QColor plain = Theme::color("codetextcolor");
    const int tab = qMax(1, QSettings().value("tabwidth", 4).toInt());
    const int maxCols = kWidth - 6;
    int row = 0;
    QVector<QRgb> colors;
    for (QTextBlock b = doc->begin(); b.isValid() && row * kLine < h; b = b.next()) {
        if (!b.isVisible())
            continue;
        const QString t = b.text();
        colors.fill(0, t.size());
        colors.resize(t.size());
        QColor base = plain;
        base.setAlphaF(0.55);
        for (int i = 0; i < t.size(); ++i)
            colors[i] = base.rgba();
        if (QTextLayout *tl = b.layout()) {
            for (const QTextLayout::FormatRange &fr : tl->formats()) {
                if (!fr.format.hasProperty(QTextFormat::ForegroundBrush))
                    continue;
                QColor c = fr.format.foreground().color();
                c.setAlphaF(0.75);
                for (int i = fr.start; i < fr.start + fr.length && i < t.size(); ++i)
                    if (i >= 0)
                        colors[i] = c.rgba();
            }
        }
        int col = 0;
        const int y = row * kLine;
        for (int i = 0; i < t.size() && col < maxCols; ++i) {
            const QChar ch = t.at(i);
            if (ch == '\t') {
                col += tab - col % tab;
                continue;
            }
            if (ch.category() == QChar::Other_Format)   // invisible direction marks
                continue;
            if (!ch.isSpace()) {
                const QRgb c = qPremultiply(colors[i]);
                for (int dy = 0; dy < kLine - 1 + 1 && y + dy < h; ++dy)
                    reinterpret_cast<QRgb *>(m_image.scanLine(y + dy))[4 + col] = c;
            }
            ++col;
        }
        ++row;
    }
}

int Minimap::imageOffset() const
{
    const int c = contentHeight();
    const int hgt = height();
    if (c <= hgt)
        return 0;
    const QScrollBar *sb = m_editor->verticalScrollBar();
    const int max = qMax(1, sb->maximum());
    return int(qint64(c - hgt) * sb->value() / max);
}

QRect Minimap::sliderRect() const
{
    const QScrollBar *sb = m_editor->verticalScrollBar();
    const int top = sb->value() * kLine - imageOffset();
    const int h = qMax(kLine * 2, sb->pageStep() * kLine);
    return QRect(0, top, width(), h);
}

int Minimap::scrollValueForY(int y) const
{
    const QScrollBar *sb = m_editor->verticalScrollBar();
    const int line = (y + imageOffset()) / kLine;
    return qBound(sb->minimum(), line - sb->pageStep() / 2, sb->maximum());
}

void Minimap::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::color("backgroundcolor"));
    if (m_image.isNull())
        return;
    const int off = imageOffset();
    p.drawImage(QPoint(0, 0), m_image, QRect(0, off, kWidth, height()));

    //! build errors: a red line at the right edge
    QTextDocument *doc = m_editor->document();
    const auto &diag = m_editor->diagnostics;
    if (!diag.isEmpty()) {
        int row = 0;
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            if (!b.isVisible())
                continue;
            if (diag.contains(b.blockNumber() + 1)) {
                const int y = row * kLine - off;
                if (y >= -kLine && y < height())
                    p.fillRect(QRect(width() - 4, y, 4, kLine + 1),
                               diag[b.blockNumber() + 1].error ? QColor("#f14c4c") : QColor("#cca700"));
            }
            ++row;
        }
    }
    //! the cursor line
    {
        int row = 0;
        const int cur = m_editor->textCursor().blockNumber();
        for (QTextBlock b = doc->begin(); b.isValid() && b.blockNumber() <= cur; b = b.next())
            if (b.isVisible() && b.blockNumber() < cur)
                ++row;
        QColor c = Theme::color("linenumberactivecolor");
        c.setAlpha(60);
        p.fillRect(QRect(0, row * kLine - off, width(), kLine), c);
    }
    //! the visible part (VS Code: shown always, stronger under the mouse or while dragged)
    QColor s(121, 121, 121, m_dragging ? 90 : (m_hover ? 70 : 40));
    p.fillRect(sliderRect(), s);
}

void Minimap::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int y = int(e->position().y());
    const QRect s = sliderRect();
    if (s.contains(0, y)) {
        m_dragDelta = y - s.top();
    } else {
        m_editor->verticalScrollBar()->setValue(scrollValueForY(y));
        m_dragDelta = sliderRect().height() / 2;
    }
    m_dragging = true;
    update();
}

void Minimap::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    //! slider top = v * kLine - offset(v), offset(v) = (C - H) v / max: solve for v
    QScrollBar *sb = m_editor->verticalScrollBar();
    const int top = int(e->position().y()) - m_dragDelta;
    const int c = contentHeight(), hgt = height(), max = qMax(1, sb->maximum());
    double perLine = kLine;
    if (c > hgt)
        perLine = kLine - double(c - hgt) / max;
    if (perLine <= 0.01)
        perLine = 0.01;
    sb->setValue(qBound(sb->minimum(), int(top / perLine + 0.5), sb->maximum()));
}

void Minimap::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
    update();
}

void Minimap::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void Minimap::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}
