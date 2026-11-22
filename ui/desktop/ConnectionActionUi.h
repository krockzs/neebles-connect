#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRectF>
#include <QSize>
#include <QString>

namespace ConnectionActionUi {

enum class Action {
    Connect,
    Disconnect
};

inline QIcon icon(Action action) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor color(
        action == Action::Connect
            ? QStringLiteral("#22C55E")
            : QStringLiteral("#EF4444"));

    if (action == Action::Connect) {
        QPen pen(color, 2.6, Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        // Enchufe verde.
        painter.drawLine(QPointF(11, 4), QPointF(11, 11));
        painter.drawLine(QPointF(21, 4), QPointF(21, 11));
        painter.drawRoundedRect(
            QRectF(8, 11, 16, 11), 3, 3);
        painter.drawLine(QPointF(16, 22), QPointF(16, 29));
    } else {
        // Stop rojo.
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(
            QRectF(8, 8, 16, 16), 2, 2);
    }

    painter.end();
    return QIcon(pixmap);
}

inline void configure(
    QPushButton* button,
    Action action,
    const QString& accessibleLabel
) {
    if (!button) return;

    // SIN texto visible.
    button->setText(QString());
    button->setIcon(icon(action));
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(36, 32);

    // Identificación al pasar el mouse y accesibilidad.
    button->setToolTip(accessibleLabel);
    button->setAccessibleName(accessibleLabel);

    if (action == Action::Connect) {
        button->setStyleSheet(QStringLiteral(
            "QPushButton {"
            " background: #09090B;"
            " border: 1px solid #15803D;"
            " border-radius: 5px;"
            " padding: 0;"
            "}"
            "QPushButton:hover:enabled {"
            " background: #052E16;"
            " border-color: #22C55E;"
            "}"
            "QPushButton:disabled {"
            " background: #09090B;"
            " border-color: #52525B;"
            "}"
        ));
    } else {
        button->setStyleSheet(QStringLiteral(
            "QPushButton {"
            " background: #09090B;"
            " border: 1px solid #991B1B;"
            " border-radius: 5px;"
            " padding: 0;"
            "}"
            "QPushButton:hover:enabled {"
            " background: #450A0A;"
            " border-color: #EF4444;"
            "}"
            "QPushButton:disabled {"
            " background: #09090B;"
            " border-color: #52525B;"
            "}"
        ));
    }
}

} // namespace ConnectionActionUi
