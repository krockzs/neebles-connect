// NEEBLES Connect desktop milestone 1: VNC engine reused from the V4 laboratory.
// Temporary limitation: LibVNCClient polling still runs on the Qt GUI thread.
// This is NOT a certified Boss RuntimeLease runtime.
#include "VncPane.h"

#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRectF>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <rfb/keysym.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <utility>
#include <limits>

namespace {
int g_ownerTag;
int buttonMask(Qt::MouseButtons buttons) {
    int mask = 0;
    if (buttons.testFlag(Qt::LeftButton)) mask |= rfbButton1Mask;
    if (buttons.testFlag(Qt::MiddleButton)) mask |= rfbButton2Mask;
    if (buttons.testFlag(Qt::RightButton)) mask |= rfbButton3Mask;
    return mask;
}
} // namespace

VncScreen::VncScreen(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // Nunca fijar el widget al tamaño del framebuffer remoto.
    // El QVBoxLayout reajusta este visor cuando cambia la ventana o la barra lateral.
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VncScreen::setClient(rfbClient* client) {
    if (client_ != client) releaseKeys();
    client_ = client;
    resized();
}

void VncScreen::resized() {
    // Incluye DesktopSize de RFB: cambiar el framebuffer NO altera
    // la geometría Qt, que siempre depende del espacio disponible.
    updateGeometry();
    update();
}

QRect VncScreen::fittedFrame() const {
    if (!client_ || client_->width <= 0 || client_->height <= 0 ||
        width() <= 0 || height() <= 0) return {};
    QSize fitted(client_->width, client_->height);
    fitted.scale(size(), Qt::KeepAspectRatio);
    if (fitted.width() <= 0 || fitted.height() <= 0) return {};
    return QRect(QPoint((width() - fitted.width()) / 2,
                        (height() - fitted.height()) / 2), fitted);
}

QPoint VncScreen::remotePoint(const QPointF& local) const {
    const QRect fitted = fittedFrame();
    if (fitted.isEmpty() || !client_) return {};
    // Misma transformación geométrica para pintar y para los eventos del mouse.
    // La conversión inversa se limita a los píxeles válidos de RFB.
    const double sx = double(client_->width) / double(fitted.width());
    const double sy = double(client_->height) / double(fitted.height());
    const int x = std::clamp(int(std::floor((local.x() - fitted.x()) * sx)),
                             0, client_->width - 1);
    const int y = std::clamp(int(std::floor((local.y() - fitted.y()) * sy)),
                             0, client_->height - 1);
    return QPoint(x, y);
}

void VncScreen::remoteUpdated(int x, int y, int w, int h) {
    if (!client_ || w <= 0 || h <= 0) return;
    const QRect fitted = fittedFrame();
    if (fitted.isEmpty()) return;
    const double left   = std::clamp(double(x), 0.0, double(client_->width));
    const double top    = std::clamp(double(y), 0.0, double(client_->height));
    const double right  = std::clamp(double(x) + double(w), 0.0, double(client_->width));
    const double bottom = std::clamp(double(y) + double(h), 0.0, double(client_->height));
    if (right <= left || bottom <= top) return;
    const double sx = double(fitted.width()) / double(client_->width);
    const double sy = double(fitted.height()) / double(client_->height);
    // Margen de 2 px por interpolación; Qt recorta el repaint al área sucia.
    const QRect affected = QRectF(fitted.x() + left * sx,
                                  fitted.y() + top * sy,
                                  (right - left) * sx, (bottom - top) * sy)
                                    .toAlignedRect().adjusted(-2, -2, 2, 2)
                                    .intersected(rect());
    if (!affected.isEmpty()) update(affected);
}

void VncScreen::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#09090B"));
    const QRect fitted = fittedFrame();
    if (fitted.isEmpty() || !client_->frameBuffer) {
        painter.setPen(QColor("#A78BFA"));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("VNC desconectado"));
        return;
    }
    // Formato de píxeles V4 ya probado por el usuario: NO alterar negociación RGB.
    const QImage picture(client_->frameBuffer, client_->width, client_->height,
                         client_->width * 4, QImage::Format_RGB32);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRectF(fitted), picture, QRectF(picture.rect()));
}

std::uint64_t VncScreen::keyId(const QKeyEvent* event) {
    const std::uint64_t scan = event->nativeScanCode();
    return scan ? scan | (std::uint64_t(1) << 32) : std::uint32_t(event->key());
}

std::uint32_t VncScreen::keysym(const QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Backspace: return XK_BackSpace;
    case Qt::Key_Tab: return XK_Tab;
    case Qt::Key_Backtab: return XK_ISO_Left_Tab;
    case Qt::Key_Return:
    case Qt::Key_Enter: return XK_Return;
    case Qt::Key_Escape: return XK_Escape;
    case Qt::Key_Delete: return XK_Delete;
    case Qt::Key_Insert: return XK_Insert;
    case Qt::Key_Home: return XK_Home;
    case Qt::Key_End: return XK_End;
    case Qt::Key_PageUp: return XK_Page_Up;
    case Qt::Key_PageDown: return XK_Page_Down;
    case Qt::Key_Left: return XK_Left;
    case Qt::Key_Right: return XK_Right;
    case Qt::Key_Up: return XK_Up;
    case Qt::Key_Down: return XK_Down;
    case Qt::Key_Shift: return XK_Shift_L;
    case Qt::Key_Control: return XK_Control_L;
    case Qt::Key_Alt: return XK_Alt_L;
    case Qt::Key_Meta: return XK_Super_L;
    default: break;
    }
    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12)
        return XK_F1 + std::uint32_t(event->key() - Qt::Key_F1);
    if (!event->text().isEmpty()) {
        const std::uint32_t cp = event->text().front().unicode();
        if (cp >= 32 && cp <= 255) return cp;
        if (cp > 255) return 0x01000000U | cp;
    }
    if (event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
        return std::uint32_t('a' + event->key() - Qt::Key_A);
    return 0;
}

bool VncScreen::event(QEvent* event) {
    // QWidget processes Tab/Backtab as focus navigation before keyPressEvent().
    // A connected RFB screen owns those keys; prevent both shortcut handling
    // and automatic focus traversal without modifying any other keys.
    const auto kind = event->type();
    if (client_ && (kind == QEvent::ShortcutOverride ||
                    kind == QEvent::KeyPress || kind == QEvent::KeyRelease)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            if (kind == QEvent::KeyPress) down(key);
            else if (kind == QEvent::KeyRelease) up(key);
            else key->accept();
            return key->isAccepted();
        }
    }
    return QWidget::event(event);
}

bool VncScreen::focusNextPrevChild(bool) {
    // Tab debe llegar a VNC, no desplazar el foco a controles Qt.
    return false;
}

void VncScreen::down(QKeyEvent* event) {
    if (!client_) return;
    event->accept();
    if (event->isAutoRepeat()) return;
    const std::uint64_t id = keyId(event);
    const auto modifiers = event->modifiers();
    const bool plain = !event->text().isEmpty() && event->text().front().isPrint() &&
        !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (plain) {
        if (std::find(pulsed_.begin(), pulsed_.end(), id) != pulsed_.end()) return;
        const std::uint32_t key = keysym(event);
        if (key && SendKeyEvent(client_, key, TRUE) && SendKeyEvent(client_, key, FALSE))
            pulsed_.push_back(id);
        return;
    }
    if (std::any_of(held_.begin(), held_.end(), [id](const Held& k) { return k.id == id; }))
        return;
    const std::uint32_t key = keysym(event);
    if (key && SendKeyEvent(client_, key, TRUE)) held_.push_back({id, key});
}

void VncScreen::up(QKeyEvent* event) {
    if (!client_) return;
    event->accept();
    if (event->isAutoRepeat()) return;
    const std::uint64_t id = keyId(event);
    const auto pulse = std::find(pulsed_.begin(), pulsed_.end(), id);
    if (pulse != pulsed_.end()) { pulsed_.erase(pulse); return; }
    const auto held = std::find_if(held_.begin(), held_.end(),
                                   [id](const Held& key) { return key.id == id; });
    if (held != held_.end()) {
        SendKeyEvent(client_, held->keysym, FALSE);
        held_.erase(held);
    }
}

void VncScreen::releaseKeys() {
    if (client_) for (const Held& key : held_) SendKeyEvent(client_, key.keysym, FALSE);
    held_.clear();
    pulsed_.clear();
}

void VncScreen::keyPressEvent(QKeyEvent* e) { down(e); }
void VncScreen::keyReleaseEvent(QKeyEvent* e) { up(e); }
void VncScreen::focusOutEvent(QFocusEvent* e) { releaseKeys(); QWidget::focusOutEvent(e); }
void VncScreen::sendMouse(QMouseEvent* e) {
    if (!client_) return;
    const QRect fitted = fittedFrame();
    if (fitted.isEmpty()) return;
    // Nunca clicar una banda negra; si se suelta tras arrastrar fuera,
    // sí hay que liberar el botón remoto (coordenadas limitadas al borde).
    if (!fitted.contains(e->position().toPoint()) &&
        (e->type() == QEvent::MouseButtonPress ||
         (e->type() == QEvent::MouseMove && e->buttons() == Qt::NoButton))) return;
    const QPoint pos = remotePoint(e->position());
    SendPointerEvent(client_, pos.x(), pos.y(), buttonMask(e->buttons()));
    e->accept();
}
void VncScreen::mousePressEvent(QMouseEvent* e) { setFocus(); sendMouse(e); }
void VncScreen::mouseReleaseEvent(QMouseEvent* e) { sendMouse(e); }
void VncScreen::mouseMoveEvent(QMouseEvent* e) { sendMouse(e); }
void VncScreen::wheelEvent(QWheelEvent* e) {
    if (!client_ || !fittedFrame().contains(e->position().toPoint())) return;
    const int delta = e->angleDelta().y();
    if (delta == 0) return;
    const QPoint pos = remotePoint(e->position());
    const int base = buttonMask(e->buttons());
    SendPointerEvent(client_, pos.x(), pos.y(), base | (delta > 0 ? rfbButton4Mask : rfbButton5Mask));
    SendPointerEvent(client_, pos.x(), pos.y(), base);
    e->accept();
}

VncPane::VncPane(QString host, int port, QWidget* parent)
    : QWidget(parent), host_(std::move(host)), port_(port) {
    auto* layout = new QVBoxLayout(this);
    auto* toolbar = new QHBoxLayout();
    toolbar->addWidget(new QLabel(QStringLiteral("VNC · %1:%2").arg(host_).arg(port_), this));
    toolbar->addStretch();
    connect_ = new QPushButton(QStringLiteral("Conectar"), this);
    disconnect_ = new QPushButton(QStringLiteral("Desconectar"), this);
    toolbar->addWidget(connect_);
    toolbar->addWidget(disconnect_);
    layout->addLayout(toolbar);
    // Sin QScrollArea ni scrollbars: Fit permanente, controlado por el layout.
    // La barra lateral de MainWindow cambia el espacio disponible automáticamente.
    screen_ = new VncScreen(this);
    layout->addWidget(screen_, 1);
    status_ = new QLabel(QStringLiteral("Sin conexión · modo de desarrollo"), this);
    layout->addWidget(status_);
    timer_ = new QTimer(this);
    timer_->setInterval(16);
    QObject::connect(timer_, &QTimer::timeout, this, [this]() { poll(); });
    QObject::connect(connect_, &QPushButton::clicked, this, [this]() { connectNow(); });
    QObject::connect(disconnect_, &QPushButton::clicked, this, [this]() { disconnectNow(); });
    updateUi(QStringLiteral("Desconectado"));
}

VncPane::~VncPane() { disconnectNow(); }

VncPane* VncPane::owner(rfbClient* client) {
    return static_cast<VncPane*>(rfbClientGetClientData(client, &g_ownerTag));
}

rfbBool VncPane::allocate(rfbClient* client) {
    VncPane* self = owner(client);
    if (!self) return FALSE;
    self->handleAllocate(client);
    return client->frameBuffer ? TRUE : FALSE;
}

void VncPane::updateRect(rfbClient* client, int x, int y, int w, int h) {
    if (VncPane* self = owner(client)) self->handleUpdate(x, y, w, h);
}

char* VncPane::getPassword(rfbClient* client) {
    if (VncPane* self = owner(client)) return self->handlePassword();
    return nullptr;
}

void VncPane::handleAllocate(rfbClient* client) {
    if (client != client_ || client->width <= 0 || client->height <= 0) return;
    const std::uint64_t size = std::uint64_t(client->width) * std::uint64_t(client->height) * 4U;
    if (size > 256ULL * 1024 * 1024 || size > std::size_t(-1)) {
        client->frameBuffer = nullptr;
        return;
    }
    try { frame_.assign(std::size_t(size), 0); }
    catch (const std::exception&) { client->frameBuffer = nullptr; return; }
    client->frameBuffer = frame_.data();
    if (screen_) screen_->resized();
}

void VncPane::handleUpdate(int x, int y, int w, int h) {
    if (client_ && screen_) screen_->remoteUpdated(x, y, w, h);
}

char* VncPane::handlePassword() {
    QByteArray bytes = oneShotPassword_;
    oneShotPassword_.fill('\0');
    oneShotPassword_.clear();
    if (bytes.isEmpty()) {
        bool accepted = false;
        QString entered = QInputDialog::getText(this, QStringLiteral("Credenciales VNC"),
            QStringLiteral("Contraseña del servidor VNC:"), QLineEdit::Password, {}, &accepted);
        if (!accepted) return nullptr;
        bytes = entered.toUtf8();
        entered.fill(QChar('\0'));
    }
    char* result = static_cast<char*>(std::malloc(std::size_t(bytes.size()) + 1));
    if (result) {
        std::memcpy(result, bytes.constData(), std::size_t(bytes.size()));
        result[bytes.size()] = '\0';
    }
    bytes.fill('\0');
    return result;
}

void VncPane::updateUi(const QString& status) {
    connect_->setEnabled(!authenticated_);
    disconnect_->setEnabled(authenticated_);
    status_->setText(status);
}

void VncPane::connectNow(const QString& password) {
    if (accessGuard_ && !accessGuard_()) return;
    if (client_ || host_.trimmed().isEmpty() || port_ < 1 || port_ > 65535) return;
    if (host_.contains(':') || host_.contains('/') || host_.contains(' ')) {
        updateUi(QStringLiteral("Host inválido: se admite IPv4 o hostname DNS"));
        return;
    }
    QString provided=password;
    if (provided.isEmpty() && credentialProvider_ && !credentialProvider_(provided)) {
        provided.fill(QChar('\0')); return;
    }
    oneShotPassword_ = provided.toUtf8();
    provided.fill(QChar('\0')); provided.clear();
    client_ = rfbGetClient(8, 3, 4);
    if (!client_) { updateUi(QStringLiteral("Error al crear cliente RFB")); return; }
    client_->format.bigEndian = FALSE;
    client_->format.trueColour = TRUE;
    client_->format.redMax = 255;
    client_->format.greenMax = 255;
    client_->format.blueMax = 255;
    client_->format.redShift = 16;
    client_->format.greenShift = 8;
    client_->format.blueShift = 0;
    client_->MallocFrameBuffer = &VncPane::allocate;
    client_->GotFrameBufferUpdate = &VncPane::updateRect;
    client_->GetPassword = &VncPane::getPassword;
    client_->canHandleNewFBSize = TRUE;
    rfbClientSetClientData(client_, &g_ownerTag, this);
    connect_->setEnabled(false);
    status_->setText(QStringLiteral("Conectando…"));

    // Argv de LibVNCClient 0.9.15: host:PUERTO (no host::PUERTO).
    QByteArray program = QByteArrayLiteral("neebles-connect-desktop");
    QByteArray endpoint = host_.toUtf8() + QByteArrayLiteral(":") + QByteArray::number(port_);
    char* argv[] = { program.data(), endpoint.data(), nullptr };
    int argc = 2;
    rfbClient* attempted = client_;
    if (!rfbInitClient(attempted, &argc, argv)) {
        client_ = nullptr; // LibVNCClient liberó attempted en fallo.
        frame_.clear();
        authenticated_ = false;
        oneShotPassword_.fill('\0'); oneShotPassword_.clear();
        screen_->setClient(nullptr);
        updateUi(QStringLiteral("Falló conexión o autenticación RFB"));
        return;
    }
    oneShotPassword_.fill('\0'); oneShotPassword_.clear();
    if (client_->format.bitsPerPixel != 32 || client_->format.bigEndian ||
        client_->format.redShift != 16 || client_->format.greenShift != 8 ||
        client_->format.blueShift != 0) {
        disconnectNow();
        updateUi(QStringLiteral("Formato RGB32 no compatible con el visor actual"));
        return;
    }
    authenticated_ = true;
    screen_->setClient(client_);
    screen_->setFocus();
    timer_->start();
    updateUi(QStringLiteral("Conectado por RFB · validar colores/teclado"));
}

void VncPane::poll() {
    if (!client_) return;
    const int ready = WaitForMessage(client_, 1000); // microsegundos.
    if (ready < 0 || (ready > 0 && !HandleRFBServerMessage(client_))) {
        disconnectNow();
        updateUi(QStringLiteral("Servidor desconectado o error de protocolo"));
    }
}

void VncPane::disconnectNow() {
    if (timer_) timer_->stop();
    if (screen_) screen_->setClient(nullptr);
    if (client_) {
        rfbClient* old = client_;
        client_ = nullptr;
        old->frameBuffer = nullptr; // propiedad de frame_, no LibVNCClient.
        rfbClientCleanup(old);
    }
    frame_.clear();
    authenticated_ = false;
    oneShotPassword_.fill('\0'); oneShotPassword_.clear();
    if (status_) updateUi(QStringLiteral("Desconectado"));
}
