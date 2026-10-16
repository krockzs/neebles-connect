#include "SshTerminal.h"
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>
#include <utility>

SshTerminal::SshTerminal(QWidget* parent): QWidget(parent) {
    setObjectName(QStringLiteral("sshTerminal"));
    setFocusPolicy(Qt::StrongFocus);
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
        vterm_screen_enable_altscreen(screen_,1);
        vterm_screen_reset(screen_,1);
    }
}

SshTerminal::~SshTerminal() { if (vt_) vterm_free(vt_); }

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
    QPainter p(this);
    p.fillRect(rect(),QColor(QStringLiteral("#09090B")));
    if (!screen_) return;
    p.setFont(font());
    const int baseline=QFontMetrics(font()).ascent();
    for (int r=0; r<rows_; ++r) for (int c=0; c<cols_; ++c) {
        VTermScreenCell cell{};
        const VTermPos pos{r,c};
        if (!vterm_screen_get_cell(screen_,pos,&cell)) continue;
        if (cell.width == 0) continue;
        if (cell.attrs.reverse) p.fillRect(c*cellW_,r*cellH_,cellW_,cellH_,QColor(QStringLiteral("#E4E4E7")));
        p.setPen(cell.attrs.reverse ? QColor(QStringLiteral("#09090B")) : QColor(QStringLiteral("#E4E4E7")));
        QString text;
        for (const auto ch: cell.chars) {
            if (!ch) break;
            char32_t codepoint=static_cast<char32_t>(ch);
            text += QString::fromUcs4(&codepoint,1);
        }
        if (!text.isEmpty()) p.drawText(c*cellW_,r*cellH_+baseline,text);
    }
    // Cursor is derived from libvterm's state rather than output escape strings.
    VTermPos cursor{0,0};
    vterm_state_get_cursorpos(vterm_obtain_state(vt_),&cursor);
    if (hasFocus() && cursor.row>=0 && cursor.row<rows_ && cursor.col>=0 && cursor.col<cols_)
        p.fillRect(cursor.col*cellW_,(cursor.row+1)*cellH_-2,cellW_,2,QColor(QStringLiteral("#22D3EE")));
}

bool SshTerminal::event(QEvent* event) {
    // Qt normally consumes Tab/Shift+Tab to move focus before keyPressEvent.
    // A live embedded terminal receives these keys just like any other input.
    const auto kind = event->type();
    if (send_ && (kind == QEvent::ShortcutOverride ||
                  kind == QEvent::KeyPress || kind == QEvent::KeyRelease)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
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
    if (ctrl && shift && key==Qt::Key_V) {
        input = QApplication::clipboard()->text().toUtf8();
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
