// NEEBLES Connect — development-only VNC viewport.
// NOT a Boss-governed runtime; compiled in the controlled Esbirro Qt 6.8.2 lab.
#include <QApplication>
#include <QByteArray>
#include <QDateTime>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <rfb/rfbclient.h>
#include <rfb/keysym.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <cstring>
#include <exception>
#include <limits>
#include <vector>

namespace {
int g_clientTag;
void diag(const char* kind, const char* fields = "") {
    std::fprintf(stderr, "[NC_DIAG] ms=%lld kind=%s %s\n",
                 static_cast<long long>(QDateTime::currentMSecsSinceEpoch()), kind, fields);
    std::fflush(stderr);
}


uint32_t remoteKey(const QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Backspace: return XK_BackSpace;
    case Qt::Key_Tab: return XK_Tab;
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
        return XK_F1 + static_cast<uint32_t>(event->key() - Qt::Key_F1);

    const QString content = event->text();
    if (!content.isEmpty()) {
        const uint32_t cp = content.front().unicode();
        if (cp >= 32 && cp <= 255) return cp;
        if (cp > 255) return 0x01000000U | cp; // X11 Unicode keysym encoding
    }
    // Ctrl+letter often emits a nonprintable character in Qt text().
    if (event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
        return static_cast<uint32_t>('a' + event->key() - Qt::Key_A);
    return 0;
}

int mouseMask(Qt::MouseButtons buttons) {
    int mask = 0;
    if (buttons.testFlag(Qt::LeftButton)) mask |= rfbButton1Mask;
    if (buttons.testFlag(Qt::MiddleButton)) mask |= rfbButton2Mask;
    if (buttons.testFlag(Qt::RightButton)) mask |= rfbButton3Mask;
    return mask;
}
} // namespace

class VncScreen final : public QWidget {
public:
    explicit VncScreen(QWidget* parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setMinimumSize(640, 360);
    }
    void setClient(rfbClient* client) {
        if (client_ != client) releaseAllKeys();
        client_ = client;
        if (client && client->width > 0 && client->height > 0)
            setFixedSize(client->width, client->height);
        else {
            setMinimumSize(640, 360);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            resize(640, 360);
        }
        update();
    }
    void framebufferResized() {
        if (client_ && client_->width > 0 && client_->height > 0)
            setFixedSize(client_->width, client_->height);
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(17, 14, 28));
        if (!client_ || !client_->frameBuffer || client_->width <= 0 || client_->height <= 0) {
            painter.setPen(QColor(207, 195, 225));
            painter.drawText(rect(), Qt::AlignCenter,
                             QStringLiteral("NEEBLES Connect — VNC prototype\nDisconnected"));
            return;
        }
        // 8/3/4 LibVNCClient format on the supported amd64 little-endian target.
        // Validate the negotiated format in a real compiled runtime before release.
        const QImage image(client_->frameBuffer, client_->width, client_->height,
                           client_->width * 4, QImage::Format_RGB32);
        painter.drawImage(QPoint(0, 0), image);
    }
    void mousePressEvent(QMouseEvent* event) override { sendMouse(event); setFocus(); }
    void mouseReleaseEvent(QMouseEvent* event) override { sendMouse(event); }
    void mouseMoveEvent(QMouseEvent* event) override { sendMouse(event); }
    void wheelEvent(QWheelEvent* event) override {
        if (!client_) return;
        const int wheel = event->angleDelta().y();
        if (wheel == 0) return;
        const auto p = event->position().toPoint();
        const int base = mouseMask(event->buttons());
        const int wheelMask = wheel > 0 ? rfbButton4Mask : rfbButton5Mask;
        SendPointerEvent(client_, p.x(), p.y(), base | wheelMask);
        SendPointerEvent(client_, p.x(), p.y(), base);
        event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (client_) keyEvent("qt_down", event);
        sendKeyDown(event);
    }
    void keyReleaseEvent(QKeyEvent* event) override {
        if (client_) keyEvent("qt_up", event);
        sendKeyUp(event);
    }
    void focusOutEvent(QFocusEvent* event) override {
        char line[80];
        std::snprintf(line, sizeof(line), "held=%zu", heldKeys_.size());
        diag("focus_out", line);
        releaseAllKeys();
        QWidget::focusOutEvent(event);
    }
private:
    struct HeldKey {
        std::uint64_t identity;
        std::uint32_t keysym;
    };
    static std::uint64_t physicalIdentity(const QKeyEvent* event) {
        // Preserve the physical key even when layout/modifiers change the text
        // between Qt keyDown and keyUp. 0 scan codes fall back to Qt::Key.
        const std::uint64_t scan = event->nativeScanCode();
        return scan ? (scan | (std::uint64_t(1) << 32))
                    : static_cast<std::uint32_t>(event->key());
    }
    // Ephemeral numeric IDs hide actual key symbols, text and scan codes.
    int diagKeyId(std::uint64_t identity) {
        for (const auto& row : diagIds_)
            if (row.first == identity) return row.second;
        const int id = static_cast<int>(diagIds_.size()) + 1;
        diagIds_.push_back(std::make_pair(identity, id));
        return id;
    }
    void keyLine(const char* action, int id, int ok) {
        char line[100];
        std::snprintf(line,sizeof(line),"id=%d ok=%d held=%zu", id, ok, heldKeys_.size());
        diag(action,line);
    }
    void keyEvent(const char* action,const QKeyEvent* event) {
        char line[100];
        std::snprintf(line,sizeof(line),"id=%d repeat=%d held=%zu",
                      diagKeyId(physicalIdentity(event)), int(event->isAutoRepeat()), heldKeys_.size());
        diag(action,line);
    }
    void releaseAllKeys() {
        if (client_)
            for (const auto& held : heldKeys_) {
                const rfbBool ok=SendKeyEvent(client_, held.keysym, FALSE);
                keyLine("release_focus_or_disconnect", diagKeyId(held.identity), int(ok));
            }
        heldKeys_.clear();
        pulseKeys_.clear();
    }
    void sendKeyDown(QKeyEvent* event) {
        if (!client_) return;
        const auto identity=physicalIdentity(event);
        const int id=diagKeyId(identity);
        if (event->isAutoRepeat()) {
            keyLine("ignore_auto_down", id, 1); event->accept(); return;
        }
        // The remote OS must not see a text key held while Qt is busy
        // decoding large 1920x1080 frame updates on the GUI thread.
        // Exclude Ctrl/Alt/Meta shortcuts; allow Shift+printable.
        const auto mods=event->modifiers();
        const bool plainText=!event->text().isEmpty()
            && event->text().front().isPrint()
            && !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (plainText) {
            const auto seen=std::find(pulseKeys_.begin(),pulseKeys_.end(),identity);
            if (seen != pulseKeys_.end()) {
                keyLine("pulse_duplicate_ignored",id,TRUE);
            } else {
                const std::uint32_t sym=remoteKey(event);
                if (!sym) keyLine("pulse_unmapped",id,FALSE);
                else {
                    const rfbBool down=SendKeyEvent(client_,sym,TRUE);
                    keyLine("pulse_down",id,int(down));
                    // Release immediately, without depending on a future Qt
                    // keyReleaseEvent that might be delayed by VNC decoding.
                    const rfbBool up=down ? SendKeyEvent(client_,sym,FALSE) : FALSE;
                    keyLine("pulse_up",id,int(up));
                    if (down && up) pulseKeys_.push_back(identity);
                }
            }
            event->accept();
            return;
        }
        const auto already=std::find_if(heldKeys_.begin(),heldKeys_.end(),
            [identity](const HeldKey& key){return key.identity==identity;});
        if (already != heldKeys_.end()) {
            keyLine("ignore_duplicate_down", id, 1);
        } else {
            const std::uint32_t sym=remoteKey(event);
            if (!sym) keyLine("unmapped_down", id, 0);
            else {
                const rfbBool ok=SendKeyEvent(client_, sym, TRUE);
                keyLine("send_down", id, int(ok));
                if (ok) heldKeys_.push_back(HeldKey{identity,sym});
            }
        }
        event->accept();
    }
    void sendKeyUp(QKeyEvent* event) {
        if (!client_) return;
        const auto identity=physicalIdentity(event);
        const int id=diagKeyId(identity);
        if (event->isAutoRepeat()) {
            keyLine("ignore_auto_up", id, 1); event->accept(); return;
        }
        const auto pulsed=std::find(pulseKeys_.begin(),pulseKeys_.end(),identity);
        if (pulsed != pulseKeys_.end()) {
            pulseKeys_.erase(pulsed);
            keyLine("pulse_physical_up",id,TRUE);
            event->accept();
            return;
        }
        const auto it=std::find_if(heldKeys_.begin(),heldKeys_.end(),
            [identity](const HeldKey& key){return key.identity==identity;});
        if (it == heldKeys_.end()) keyLine("unmatched_up", id, 0);
        else {
            const rfbBool ok=SendKeyEvent(client_, it->keysym, FALSE);
            keyLine("send_up", id, int(ok));
            heldKeys_.erase(it);
        }
        event->accept();
    }
    void sendMouse(const QMouseEvent* event) {
        if (!client_) return;
        const QPoint p = event->position().toPoint();
        SendPointerEvent(client_, p.x(), p.y(), mouseMask(event->buttons()));
    }
    rfbClient* client_ = nullptr;
    std::vector<HeldKey> heldKeys_;
    std::vector<std::uint64_t> pulseKeys_;
    std::vector<std::pair<std::uint64_t,int>> diagIds_;
};

class VncWindow final : public QWidget {
public:
    VncWindow() {
        setWindowTitle(QStringLiteral("NEEBLES Connect | VNC development prototype"));
        resize(1100, 720);
        auto* layout = new QVBoxLayout(this);
        auto* controls = new QHBoxLayout;
        host_ = new QLineEdit(this);
        host_->setPlaceholderText(QStringLiteral("Tailscale IPv4, e.g. 100.x.x.x"));
        host_->setText(QStringLiteral("100.97.205.102"));
        host_->setToolTip(QStringLiteral("Use only hosts you are authorized to access."));
        port_ = new QSpinBox(this);
        port_->setRange(1, 65535);
        port_->setValue(5900);
        connectButton_ = new QPushButton(QStringLiteral("Connect"), this);
        disconnectButton_ = new QPushButton(QStringLiteral("Disconnect"), this);
        disconnectButton_->setEnabled(false);
        controls->addWidget(new QLabel(QStringLiteral("Host:"), this));
        controls->addWidget(host_, 1);
        controls->addWidget(new QLabel(QStringLiteral("Port:"), this));
        controls->addWidget(port_);
        controls->addWidget(connectButton_);
        controls->addWidget(disconnectButton_);
        layout->addLayout(controls);
        auto* area = new QScrollArea(this);
        screen_ = new VncScreen;
        area->setWidget(screen_);
        area->setWidgetResizable(false);
        area->setAlignment(Qt::AlignCenter);
        layout->addWidget(area, 1);
        status_ = new QLabel(QStringLiteral("Disconnected — credentials are never saved"), this);
        layout->addWidget(status_);
        timer_.setInterval(16);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
        QObject::connect(connectButton_, &QPushButton::clicked, this, [this] { open(); });
        QObject::connect(disconnectButton_, &QPushButton::clicked, this, [this] { closeClient(); });
    }
    ~VncWindow() override { closeClient(); }

    void onNewFramebuffer(rfbClient* client) {
        if (client != client_ || client->width <= 0 || client->height <= 0) return;
        diagFormat("framebuffer_alloc",client);
        // Limit a single RGBA32 framebuffer to 256 MiB.
        const std::uint64_t count = std::uint64_t(client->width) * std::uint64_t(client->height) * 4;
        if (count > 256ULL * 1024 * 1024) {
            client->frameBuffer = nullptr;
            return;
        }
        try {
            framebuffer_.assign(static_cast<std::size_t>(count), 0);
        } catch (const std::exception&) {
            client->frameBuffer = nullptr;
            return;
        }
        client->frameBuffer = framebuffer_.data();
        screen_->framebufferResized();
    }
    void onUpdate(rfbClient* client, int x, int y, int w, int h) {
        if (client != client_ || !client->frameBuffer) return;
        screen_->update(x, y, w, h);
        // Statistics only: no screenshot, pixels, credentials or text are saved.
        ++frameUpdates_;
        if (frameUpdates_ <= 6 || frameUpdates_ % 120 == 0) {
            std::uint64_t sumR=0, sumG=0, sumB=0;
            unsigned samples=0;
            if (client->width > 0 && client->height > 0 &&
                client->format.bitsPerPixel == 32 && framebuffer_.size() >=
                static_cast<std::size_t>(client->width)*static_cast<std::size_t>(client->height)*4) {
                const int stride=client->width*4;
                for (int gy=0;gy<8;++gy) for(int gx=0;gx<8;++gx) {
                    const int xx=((gx*2+1)*client->width)/16;
                    const int yy=((gy*2+1)*client->height)/16;
                    const auto* p=client->frameBuffer+std::size_t(yy)*stride+std::size_t(xx)*4;
                    sumR+=p[2]; sumG+=p[1]; sumB+=p[0]; ++samples;
                }
            }
            char line[256];
            std::snprintf(line,sizeof(line),
                "count=%llu rect_w=%d rect_h=%d mean_RGB32_r=%u mean_RGB32_g=%u mean_RGB32_b=%u samples=%u",
                static_cast<unsigned long long>(frameUpdates_),w,h,
                samples?unsigned(sumR/samples):0,samples?unsigned(sumG/samples):0,
                samples?unsigned(sumB/samples):0,samples);
            diag("frame_update",line);
        }
    }
    static void diagFormat(const char* label,const rfbClient* client) {
        if (!client) return;
        char line[260];
        std::snprintf(line,sizeof(line),
            "bpp=%d depth=%d be=%d true=%d rmax=%d gmax=%d bmax=%d rshift=%d gshift=%d bshift=%d width=%d height=%d",
            int(client->format.bitsPerPixel),int(client->format.depth),
            int(client->format.bigEndian),int(client->format.trueColour),
            int(client->format.redMax),int(client->format.greenMax),
            int(client->format.blueMax),int(client->format.redShift),
            int(client->format.greenShift),int(client->format.blueShift),
            client->width,client->height);
        diag(label,line);
    }
    char* password() {
        bool accepted = false;
        QString value = QInputDialog::getText(
            this, QStringLiteral("VNC authentication"),
            QStringLiteral("VNC password (not stored):"),
            QLineEdit::Password, QString(), &accepted);
        if (!accepted) value.clear();
        QByteArray bytes = value.toUtf8();
        // LibVNCClient owns and frees the returned malloc buffer.
        char* result = static_cast<char*>(std::malloc(static_cast<std::size_t>(bytes.size()) + 1));
        if (!result) return nullptr;
        std::memcpy(result, bytes.constData(), static_cast<std::size_t>(bytes.size()));
        result[bytes.size()] = '\0';
        bytes.fill('\0');
        value.fill(QChar('\0'));
        return result;
    }
private:
    static VncWindow* owner(rfbClient* client) {
        return static_cast<VncWindow*>(rfbClientGetClientData(client, &g_clientTag));
    }
    static rfbBool resizeFramebuffer(rfbClient* client) {
        if (auto* self = owner(client)) {
            self->onNewFramebuffer(client);
            return client->frameBuffer ? TRUE : FALSE;
        }
        return FALSE;
    }
    static void updated(rfbClient* client, int x, int y, int w, int h) {
        if (auto* self = owner(client)) self->onUpdate(client, x, y, w, h);
    }
    static char* promptPassword(rfbClient* client) {
        if (auto* self = owner(client)) return self->password();
        return nullptr;
    }
    void open() {
        if (client_) return;
        const QString host = host_->text().trimmed();
        if (host.isEmpty() || host.contains(':') || host.contains(' ') || host.contains('/')) {
            QMessageBox::warning(this, QStringLiteral("Invalid host"),
                                 QStringLiteral("Enter an authorized IPv4 address or DNS hostname."));
            return;
        }
        client_ = rfbGetClient(8, 3, 4);
        if (!client_) {
            QMessageBox::critical(this, QStringLiteral("VNC"), QStringLiteral("rfbGetClient failed."));
            return;
        }
        // Qt QImage::Format_RGB32 uses 0xFFRRGGBB (on little endian BGRA bytes).
        // LibVNCClient defaults to the inverse R/B shift order for 8/3/4.
        // Request RGB32 explicitly BEFORE SetPixelFormat negotiation.
        client_->format.bigEndian = FALSE;
        client_->format.trueColour = TRUE;
        client_->format.redMax = 255;
        client_->format.greenMax = 255;
        client_->format.blueMax = 255;
        client_->format.redShift = 16;
        client_->format.greenShift = 8;
        client_->format.blueShift = 0;
        diagFormat("before_handshake",client_);
        client_->MallocFrameBuffer = &VncWindow::resizeFramebuffer;
        client_->GotFrameBufferUpdate = &VncWindow::updated;
        client_->GetPassword = &VncWindow::promptPassword;
        client_->canHandleNewFBSize = TRUE;
        rfbClientSetClientData(client_, &g_clientTag, this);
        status_->setText(QStringLiteral("Connecting…"));
        connectButton_->setEnabled(false);
        QApplication::processEvents();
        QByteArray program = QByteArrayLiteral("neebles-connect-vnc");
        QByteArray destination = host.toUtf8() + QByteArrayLiteral(":")
                               + QByteArray::number(port_->value());
        char* args[] = { program.data(), destination.data(), nullptr };
        int argc = 2;
        rfbClient* attempted = client_;
        if (!rfbInitClient(attempted, &argc, args)) {
            // API contract: rfbInitClient has already released attempted on failure.
            client_ = nullptr;
            screen_->setClient(nullptr);
            framebuffer_.clear();
            status_->setText(QStringLiteral("Connection or authentication failed"));
            connectButton_->setEnabled(true);
            QMessageBox::warning(this, QStringLiteral("VNC"),
                                 QStringLiteral("Connection failed. Check the host, port, and VNC password."));
            return;
        }
        diagFormat("after_handshake",client_);
        if (client_->format.bitsPerPixel != 32 || client_->format.bigEndian ||
            client_->format.redShift != 16 || client_->format.greenShift != 8 ||
            client_->format.blueShift != 0 || client_->format.redMax != 255 ||
            client_->format.greenMax != 255 || client_->format.blueMax != 255) {
            closeClient();
            QMessageBox::warning(this, QStringLiteral("Unexpected pixel format"),
                                 QStringLiteral("VNC did not retain RGB32 pixel format."));
            return;
        }
        screen_->setClient(client_);
        frameUpdates_=0;
        diag("connected","transport=VNC");
        screen_->setFocus();
        host_->setEnabled(false);
        port_->setEnabled(false);
        disconnectButton_->setEnabled(true);
        status_->setText(QStringLiteral("Connected to %1:%2 (development-only, via existing network)")
                             .arg(host).arg(port_->value()));
        timer_.start();
    }
    void poll() {
        if (!client_) return;
        const qint64 began=QDateTime::currentMSecsSinceEpoch();
        const int result=WaitForMessage(client_,1000); // microseconds, NOT ms
        if (result < 0 || (result > 0 && !HandleRFBServerMessage(client_))) {
            closeClient();
            status_->setText(QStringLiteral("Server disconnected or protocol error"));
        }
        const qint64 elapsed=QDateTime::currentMSecsSinceEpoch()-began;
        if (elapsed >= 80) {
            char line[100];
            std::snprintf(line,sizeof(line),"duration_ms=%lld result=%d",
                static_cast<long long>(elapsed),result);
            diag("poll_stall",line);
        }
    }
    void closeClient() {
        if(client_) { char line[80]; std::snprintf(line,sizeof(line),"updates=%llu", static_cast<unsigned long long>(frameUpdates_)); diag("disconnect",line); }
        timer_.stop();
        screen_->setClient(nullptr);
        if (client_) {
            rfbClient* old = client_;
            client_ = nullptr;
            // Our vector owns the framebuffer; LibVNCClient must not free it.
            old->frameBuffer = nullptr;
            rfbClientCleanup(old);
        }
        framebuffer_.clear();
        host_->setEnabled(true);
        port_->setEnabled(true);
        connectButton_->setEnabled(true);
        disconnectButton_->setEnabled(false);
        status_->setText(QStringLiteral("Disconnected — credentials are never saved"));
    }
    QLineEdit* host_ = nullptr;
    QSpinBox* port_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QLabel* status_ = nullptr;
    VncScreen* screen_ = nullptr;
    QTimer timer_;
    rfbClient* client_ = nullptr;
    std::vector<std::uint8_t> framebuffer_;
    std::uint64_t frameUpdates_ = 0;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NEEBLES Connect VNC prototype"));
    VncWindow window;
    window.show();
    return app.exec();
}
