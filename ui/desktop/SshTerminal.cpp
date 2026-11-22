#include "SshTerminal.h"
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStringList>
#include <algorithm>
#include <utility>

SshTerminal::SshTerminal(QWidget* parent): QWidget(parent) {
    setObjectName(QStringLiteral("sshTerminal"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(240, 120);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    const QFontMetrics fm(font());
    cellW_ = std::max(1, fm.horizontalAdvance(QChar('M')));
    cellH_ = std::max(1, fm.height());
    vt_ = vterm_new(rows_,cols_);
    if (vt_) {
        vterm_set_utf8(vt_,1);
        screen_ = vterm_obtain_screen(vt_);
        static const VTermScreenCallbacks callbacks = []() {
            VTermScreenCallbacks result{};
            result.sb_pushline = &SshTerminal::pushScrollback;
            result.sb_popline = &SshTerminal::popScrollback;
            return result;
        }();
        vterm_screen_set_callbacks(screen_, &callbacks, this);
        vterm_screen_enable_altscreen(screen_,1);
        vterm_screen_reset(screen_,1);
    }
}

SshTerminal::~SshTerminal() { if (vt_) vterm_free(vt_); }

int SshTerminal::pushScrollback(int cols, const VTermScreenCell* cells, void* user) {
    auto* terminal = static_cast<SshTerminal*>(user);
    if (!terminal || !cells || cols <= 0) return 0;
    const int width = std::min(cols, kHistoryWidthLimit);
    QVector<VTermScreenCell> line;
    line.reserve(width);
    for (int col = 0; col < width; ++col) line.append(cells[col]);
    terminal->history_.append(std::move(line));
    if (terminal->scrollOffset_ > 0) ++terminal->scrollOffset_;
    if (terminal->history_.size() > kHistoryLimit) {
        terminal->history_.removeFirst();
        if (terminal->scrollOffset_ > 0) --terminal->scrollOffset_;
        if (terminal->hasSelection_ || terminal->selecting_) {
            --terminal->selectionAnchor_.row;
            --terminal->selectionEnd_.row;
            if (terminal->selectionAnchor_.row < 0 || terminal->selectionEnd_.row < 0)
                terminal->clearSelection();
        }
    }
    return 1;
}

int SshTerminal::popScrollback(int cols, VTermScreenCell* cells, void* user) {
    auto* terminal = static_cast<SshTerminal*>(user);
    if (!terminal || !cells || cols <= 0 || terminal->history_.isEmpty()) return 0;
    const QVector<VTermScreenCell> line = terminal->history_.takeLast();
    for (int col = 0; col < cols; ++col)
        cells[col] = col < line.size() ? line.at(col) : VTermScreenCell{};
    terminal->scrollOffset_ = std::min(terminal->scrollOffset_, int(terminal->history_.size()));
    terminal->clearSelection();
    return 1;
}

void SshTerminal::clearSession() {
    history_.clear();
    scrollOffset_ = 0;
    clearSelection();
    if (screen_) {
        vterm_screen_reset(screen_, 1);
        vterm_screen_flush_damage(screen_);
    }
    update();
}

void SshTerminal::clearSelection() {
    selecting_ = false;
    hasSelection_ = false;
    selectionAnchor_ = {};
    selectionEnd_ = {};
}

SshTerminal::CellPosition SshTerminal::positionAt(const QPoint& point) const {
    const int visibleRow = std::clamp(point.y() / cellH_, 0, rows_ - 1);
    return {int(history_.size()) - scrollOffset_ + visibleRow,
            std::clamp(point.x() / cellW_, 0, cols_)};
}

bool SshTerminal::cellAt(int logicalRow, int col, VTermScreenCell* cell) const {
    if (!cell || col < 0 || col >= cols_ || logicalRow < 0) return false;
    const int historySize = int(history_.size());
    if (logicalRow < historySize) {
        const auto& line = history_.at(logicalRow);
        *cell = col < line.size() ? line.at(col) : VTermScreenCell{};
        return true;
    }
    if (!screen_ || logicalRow - historySize >= rows_) return false;
    return vterm_screen_get_cell(screen_, {logicalRow - historySize, col}, cell) != 0;
}

QString SshTerminal::cellText(const VTermScreenCell& cell) const {
    if (cell.width == 0) return {};
    QString result;
    for (const auto ch : cell.chars) {
        if (!ch) break;
        char32_t codepoint = static_cast<char32_t>(ch);
        result += QString::fromUcs4(&codepoint, 1);
    }
    return result.isEmpty() ? QStringLiteral(" ") : result;
}

QString SshTerminal::selectionText() const {
    if (!hasSelection_) return {};
    auto first = selectionAnchor_;
    auto last = selectionEnd_;
    if (last.row < first.row || (last.row == first.row && last.col < first.col))
        std::swap(first, last);
    QStringList lines;
    for (int row = std::max(0, first.row); row <= last.row && row < int(history_.size()) + rows_; ++row) {
        const int begin = row == first.row ? first.col : 0;
        const int end = row == last.row ? std::min(cols_, last.col + 1) : cols_;
        QString text;
        for (int col = begin; col < end; ++col) {
            VTermScreenCell cell{};
            if (!cellAt(row, col, &cell)) continue;

            // libvterm usa width=0 para la segunda columna
            // de un caracter ancho. Si la seleccion comienza
            // ahi, incluir el glifo desde su primera celda.
            if (cell.width == 0) {
                if (col == begin && col > 0) {
                    VTermScreenCell leading{};
                    if (cellAt(row, col - 1, &leading) &&
                        leading.width > 1)
                        text += cellText(leading);
                }
                continue;
            }
            text += cellText(cell);
        }
        while (text.endsWith(QChar(' '))) text.chop(1);
        lines.append(text);
    }
    return lines.join(QChar('\n'));
}

bool SshTerminal::copySelectionToClipboard() const {
    const QString selected = selectionText();
    if (selected.isEmpty()) return false;
    QApplication::clipboard()->setText(selected, QClipboard::Clipboard);
    return true;
}

bool SshTerminal::copyAllTextToClipboard() const {
    if (!screen_) return false;

    // El boton Copy exporta el texto disponible de la sesion,
    // incluido el scrollback retenido en memoria.
    // Ctrl+Shift+C conserva su semantica de copiar seleccion.
    QStringList lines;
    const int total = int(history_.size()) + rows_;

    for (int row = 0; row < total; ++row) {
        QString line;

        for (int col = 0; col < cols_; ++col) {
            VTermScreenCell cell{};
            if (!cellAt(row, col, &cell)) continue;
            if (cell.width == 0) continue;
            line += cellText(cell);
        }

        while (line.endsWith(QChar(' ')))
            line.chop(1);

        lines.append(line);
    }

    while (!lines.isEmpty() && lines.first().isEmpty())
        lines.removeFirst();

    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    if (lines.isEmpty()) return false;

    const QString output = lines.join(QChar('\n'));
    if (output.trimmed().isEmpty()) return false;

    QApplication::clipboard()->setText(
        output, QClipboard::Clipboard);
    return true;
}

bool SshTerminal::pasteClipboardText() {
    if (!send_) return false;

    const QString text = QApplication::clipboard()->text(
        QClipboard::Clipboard);

    if (text.isEmpty()) return false;

    QByteArray bytes = text.toUtf8();
    if (bytes.isEmpty()) return false;

    send_(std::move(bytes));
    return true;
}

void SshTerminal::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        setFocus();
        pasteClipboardText();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    setFocus();
    selectionAnchor_ = positionAt(event->position().toPoint());
    selectionEnd_ = selectionAnchor_;
    hasSelection_ = false;
    selecting_ = true;
    update();
    event->accept();
}

void SshTerminal::mouseMoveEvent(QMouseEvent* event) {
    if (!selecting_) { QWidget::mouseMoveEvent(event); return; }
    selectionEnd_ = positionAt(event->position().toPoint());
    hasSelection_ = selectionAnchor_.row != selectionEnd_.row ||
                    selectionAnchor_.col != selectionEnd_.col;
    update();
    event->accept();
}

void SshTerminal::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !selecting_) {
        QWidget::mouseReleaseEvent(event); return;
    }
    selectionEnd_ = positionAt(event->position().toPoint());
    hasSelection_ = selectionAnchor_.row != selectionEnd_.row ||
                    selectionAnchor_.col != selectionEnd_.col;
    selecting_ = false;
    update();
    event->accept();
}

void SshTerminal::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) { QWidget::wheelEvent(event); return; }
    scrollOffset_ = std::clamp(scrollOffset_ + steps * 3, 0, int(history_.size()));
    clearSelection();
    update();
    event->accept();
}

void SshTerminal::receive(const QByteArray& bytes) {
    if (!vt_ || bytes.isEmpty()) return;
    vterm_input_write(vt_,bytes.constData(),static_cast<size_t>(bytes.size()));
    vterm_screen_flush_damage(screen_);
    update();
}

void SshTerminal::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    resizeGrid();
}

void SshTerminal::resizeGrid() {
    const int c = std::max(20,width()/cellW_);
    const int r = std::max(5,height()/cellH_);
    if (c == cols_ && r == rows_) return;
    cols_=c;rows_=r;
    if (vt_) {vterm_set_size(vt_,rows_,cols_);vterm_screen_flush_damage(screen_);}
    if (resize_) resize_(rows_,cols_);
    update();
}

void SshTerminal::paintEvent(QPaintEvent*) {
    QPainter painter(this);

    const QColor defaultForeground(
        QStringLiteral("#E4E4E7"));
    const QColor defaultBackground(
        QStringLiteral("#09090B"));

    painter.fillRect(rect(), defaultBackground);
    if (!screen_ || !vt_) return;

    const QFont baseFont = font();
    const int baseline = QFontMetrics(baseFont).ascent();

    // Resuelve los índices ANSI mediante la paleta
    // certificada de libvterm, preservando RGB real.
    const auto resolveColor =
        [this, &defaultForeground, &defaultBackground](
            VTermColor color, bool foreground) -> QColor {

        if (foreground &&
            VTERM_COLOR_IS_DEFAULT_FG(&color))
            return defaultForeground;

        if (!foreground &&
            VTERM_COLOR_IS_DEFAULT_BG(&color))
            return defaultBackground;

        vterm_screen_convert_color_to_rgb(
            screen_, &color);

        return QColor(
            color.rgb.red,
            color.rgb.green,
            color.rgb.blue);
    };

    const int firstVisible =
        int(history_.size()) - scrollOffset_;

    CellPosition selectionFirst = selectionAnchor_;
    CellPosition selectionLast = selectionEnd_;

    if (selectionLast.row < selectionFirst.row ||
        (selectionLast.row == selectionFirst.row &&
         selectionLast.col < selectionFirst.col))
        std::swap(selectionFirst, selectionLast);

    for (int row = 0; row < rows_; ++row) {
        const int logicalRow = firstVisible + row;

        for (int col = 0; col < cols_; ++col) {
            VTermScreenCell cell{};

            if (!cellAt(logicalRow, col, &cell) ||
                cell.width == 0)
                continue;

            QColor foreground = resolveColor(cell.fg, true);
            QColor background = resolveColor(cell.bg, false);

            if (cell.attrs.reverse)
                std::swap(foreground, background);

            const int columns =
                std::max(1, int(cell.width));

            const QRect cellRect(
                col * cellW_,
                row * cellH_,
                columns * cellW_,
                cellH_);

            painter.fillRect(cellRect, background);

            if (cell.attrs.conceal)
                continue;

            const QString text = cellText(cell);
            if (text.isEmpty())
                continue;

            QFont cellFont = baseFont;
            cellFont.setBold(cell.attrs.bold != 0);
            cellFont.setItalic(cell.attrs.italic != 0);
            cellFont.setUnderline(
                cell.attrs.underline !=
                VTERM_UNDERLINE_OFF);
            cellFont.setStrikeOut(cell.attrs.strike != 0);

            painter.save();
            painter.setClipRect(cellRect);
            painter.setFont(cellFont);
            painter.setPen(foreground);
            painter.drawText(
                col * cellW_,
                row * cellH_ + baseline,
                text);
            painter.restore();
        }

        // Selección dibujada por encima del texto,
        // sin cambiar su contenido ni el portapapeles.
        if (hasSelection_ &&
            logicalRow >= selectionFirst.row &&
            logicalRow <= selectionLast.row) {

            int left =
                logicalRow == selectionFirst.row
                    ? selectionFirst.col : 0;

            int right =
                logicalRow == selectionLast.row
                    ? selectionLast.col + 1 : cols_;

            // No partir un glifo de dos columnas al
            // dibujar el resaltado de seleccion.
            if (left > 0 && left < cols_) {
                VTermScreenCell firstCell{};
                if (cellAt(logicalRow, left, &firstCell) &&
                    firstCell.width == 0)
                    --left;
            }

            if (right > 0 && right < cols_) {
                VTermScreenCell lastCell{};
                if (cellAt(logicalRow, right - 1, &lastCell) &&
                    lastCell.width > 1)
                    right = std::min(
                        cols_,
                        right + int(lastCell.width) - 1);
            }

            if (right > left) {
                painter.fillRect(
                    left * cellW_,
                    row * cellH_,
                    (right - left) * cellW_,
                    cellH_,
                    QColor(59, 130, 246, 90));
            }
        }
    }

    // Cursor original preservado.
    VTermPos cursor{0, 0};
    vterm_state_get_cursorpos(
        vterm_obtain_state(vt_), &cursor);

    if (scrollOffset_ == 0 &&
        hasFocus() &&
        cursor.row >= 0 &&
        cursor.row < rows_ &&
        cursor.col >= 0 &&
        cursor.col < cols_) {

        painter.fillRect(
            cursor.col * cellW_,
            (cursor.row + 1) * cellH_ - 2,
            cellW_,
            2,
            QColor(QStringLiteral("#22D3EE")));
    }
}

bool SshTerminal::event(QEvent* event) {
    // Qt normally consumes Tab/Shift+Tab to move focus before keyPressEvent.
    // A live embedded terminal receives these keys just like any other input.
    const auto kind = event->type();
    if (send_ && (kind == QEvent::ShortcutOverride ||
                  kind == QEvent::KeyPress || kind == QEvent::KeyRelease)) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool copyShortcut = key->key() == Qt::Key_C &&
            (key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) ==
            (Qt::ControlModifier | Qt::ShiftModifier);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab || copyShortcut) {
            if (kind == QEvent::KeyPress) keyPressEvent(key);
            else key->accept(); // No separate SSH key-up packet.
            return key->isAccepted();
        }
    }
    return QWidget::event(event);
}

void SshTerminal::keyPressEvent(QKeyEvent* event) {
    if (!send_) {event->ignore();return;}
    QByteArray input;
    const int key=event->key();
    const bool ctrl=event->modifiers() & Qt::ControlModifier;
    const bool shift=event->modifiers() & Qt::ShiftModifier;
    if (ctrl && shift && key == Qt::Key_C) {
        if (copySelectionToClipboard()) event->accept();
        else event->ignore();
        return;
    }
    if (ctrl && shift && key==Qt::Key_V) {
        if (pasteClipboardText()) event->accept();
        else event->ignore();
        return;
    } else if (ctrl && key>=Qt::Key_A && key<=Qt::Key_Z) {
        input.append(static_cast<char>(key-Qt::Key_A+1));
    } else {
        switch(key) {
            case Qt::Key_Return:case Qt::Key_Enter:input="\r";break;
            case Qt::Key_Backspace:input="\x7f";break;
            case Qt::Key_Tab:input=shift ? "\x1b[Z" : "\t";break;
            case Qt::Key_Backtab:input="\x1b[Z";break;
            case Qt::Key_Escape:input="\x1b";break;
            case Qt::Key_Up:input="\x1b[A";break;
            case Qt::Key_Down:input="\x1b[B";break;
            case Qt::Key_Right:input="\x1b[C";break;
            case Qt::Key_Left:input="\x1b[D";break;
            case Qt::Key_Home:input="\x1b[H";break;
            case Qt::Key_End:input="\x1b[F";break;
            case Qt::Key_Delete:input="\x1b[3~";break;
            case Qt::Key_PageUp:input="\x1b[5~";break;
            case Qt::Key_PageDown:input="\x1b[6~";break;
            default:input=event->text().toUtf8();break;
        }
    }
    if (!input.isEmpty()) {send_(std::move(input));event->accept();}
    else event->ignore();
}
