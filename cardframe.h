#ifndef CARDFRAME_H
#define CARDFRAME_H

#include <QApplication>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include "theme.h"

/**
 * @file cardframe.h
 * A rounded "card" that holds one widget (the code editor, the bottom panel): the card is
 * painted slightly lighter than the window with a thin border, so the panels look separate
 * from each other and from the window, like the floating panels of modern editors.
 */
class CardFrame : public QFrame
{
public:
    static constexpr int kRadius = 10;
    static constexpr int kInset = 3; // keeps the child's square corners inside the rounded outline

    explicit CardFrame(QWidget *content, QWidget *parent = nullptr) : QFrame(parent)
    {
        setAttribute(Qt::WA_StyledBackground, false);
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(kInset, kInset, kInset, kInset);
        l->setSpacing(0);
        l->addWidget(content);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = QApplication::palette();
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
        p.fillPath(path, Theme::color("backgroundcolor")); // follows the selected colour theme
        p.setPen(QPen(pal.color(QPalette::Mid), 1));
        p.drawPath(path);
    }
};

#endif
