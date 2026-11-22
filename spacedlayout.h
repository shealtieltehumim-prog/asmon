/****************************************************************************
** Asmon — fork of SASM (GPL-3.0).
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#ifndef SPACEDLAYOUT_H
#define SPACEDLAYOUT_H

#include <QPlainTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

/**
 * @file spacedlayout.h
 * Line height for QPlainTextEdit. Qt's plain-text layout ignores QTextBlockFormat::lineHeight
 * and the block margins, so every line is exactly as tall as the font (cramped next to VS Code,
 * where a 14 px font gets a 19 px line). This layout keeps everything QPlainTextEdit relies on
 * (it is still a QPlainTextDocumentLayout) and only moves the lines of each block down by half of
 * the extra spacing and reports the block that much taller: the text sits in the middle of a taller
 * row, the cursor, the selection, hit testing and scrolling follow the moved lines.
 */
class SpacedLayout : public QPlainTextDocumentLayout
{
public:
    explicit SpacedLayout(QTextDocument *doc) : QPlainTextDocumentLayout(doc) {}

    void setSpacing(qreal s)
    {
        if (qFuzzyCompare(s + 1, m_spacing + 1))
            return;
        m_spacing = qMax<qreal>(0, s);
        if (document())
            document()->markContentsDirty(0, document()->characterCount());
    }
    qreal spacing() const { return m_spacing; }

    QRectF blockBoundingRect(const QTextBlock &block) const override
    {
        QRectF r = QPlainTextDocumentLayout::blockBoundingRect(block); // lays the block out if needed
        if (m_spacing <= 0 || !block.isValid() || !block.isVisible())
            return r;
        QTextLayout *tl = block.layout();
        if (!tl || tl->lineCount() == 0)
            return r;
        qreal y = m_spacing / 2;
        for (int i = 0; i < tl->lineCount(); ++i) {
            QTextLine line = tl->lineAt(i);
            if (qAbs(line.position().y() - y) > 0.01)
                line.setPosition(QPointF(line.position().x(), y));
            y += line.height() + m_spacing;
        }
        qreal h = y - m_spacing / 2;                 // n lines, n gaps: half above, half below
        if (!block.next().isValid())
            h += document()->documentMargin();
        r.setHeight(h);
        return r;
    }

private:
    qreal m_spacing = 0;
};

#endif
