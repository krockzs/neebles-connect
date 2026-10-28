// NEEBLES Connect Q07C: session-owned RFB worker and immutable UI frames.
// RFB sockets, authentication, framebuffer mutation and outbound input stay
// strictly in the worker. QWidget never invokes LibVNCClient network APIs.
#include "VncPane.h"

#include <QClipboard>
#include <QEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
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
#include <rfb/rfbclient.h>
#include <rfb/keysym.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <sys/socket.h>

namespace {
int g_ownerTag;
constexpr std::size_t kMaxFramebuffer = 256ULL * 1024 * 1024;
constexpr std::size_t kMaxInputs = 1024;
int buttonMask(Qt::MouseButtons buttons) {
    int mask = 0;
    if (buttons.testFlag(Qt::LeftButton)) mask |= rfbButton1Mask;
    if (buttons.testFlag(Qt::MiddleButton)) mask |= rfbButton2Mask;
    if (buttons.testFlag(Qt::RightButton)) mask |= rfbButton3Mask;
    return mask;
}
}

struct VncSessionState {
    std::atomic_bool cancelled{false};
    std::atomic_bool authenticated{false};
    std::atomic_bool finished{false};
    std::mutex mutex;
    std::mutex socketMutex;
    int socket = -1;
    QByteArray secret; // Worker-only after start; scrubbed on use/exit.
    std::vector<std::uint8_t> pixels; // Worker-only RFB framebuffer.
    std::deque<VncScreen::Input> inputs;
    QString status = QStringLiteral("VNC · conectando…");
    QImage frame; // Deep-copied, never aliases mutable RFB memory.
    std::uint64_t frameRevision = 0;
    QVector<VncPane::Monitor> monitors;
    std::uint64_t topologyRevision = 0;

    void setStatus(const QString& message) {
        std::lock_guard<std::mutex> lock(mutex);
        status = message;
    }
    void stop() {
        cancelled.store(true);
        // shutdown() wakes reads while worker exclusively owns close()/cleanup.
        std::lock_guard<std::mutex> lock(socketMutex);
        if (socket >= 0) ::shutdown(socket, SHUT_RDWR);
    }
    void enqueue(VncScreen::Input event) {
        if (cancelled.load() || !authenticated.load()) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (event.kind == VncScreen::Input::Kind::Pointer && event.motion &&
            !inputs.empty() && inputs.back().kind == VncScreen::Input::Kind::Pointer &&
            inputs.back().motion) {
            inputs.back() = event;
            return;
        }
        if (inputs.size() >= kMaxInputs) {
            auto it = std::find_if(inputs.begin(), inputs.end(), [](const auto& item) {
                return item.kind == VncScreen::Input::Kind::Pointer && item.motion;
            });
            if (it != inputs.end()) inputs.erase(it);
            else {
                // Never silently discard a key release or mouse button release.
                cancelled.store(true);
                return;
            }
        }
        inputs.push_back(event);
    }
};

namespace {
VncSessionState* stateOf(rfbClient* client) {
    return static_cast<VncSessionState*>(rfbClientGetClientData(client, &g_ownerTag));
}
rfbBool allocate(rfbClient* client) {
    auto* state = stateOf(client);
    if (!state || state->cancelled.load() || client->width <= 0 || client->height <= 0) return FALSE;
    const std::uint64_t bytes = std::uint64_t(client->width) * std::uint64_t(client->height) * 4U;
    if (bytes > kMaxFramebuffer || bytes > std::numeric_limits<std::size_t>::max()) return FALSE;
    try { state->pixels.assign(std::size_t(bytes), 0); }
    catch (const std::exception&) { return FALSE; }
    client->frameBuffer = state->pixels.data();
    return TRUE;
}
void updated(rfbClient*, int, int, int, int) {
    // Deep frame snapshots occur after HandleRFBServerMessage() returns.
}
char* password(rfbClient* client) {
    auto* state = stateOf(client);
    if (!state || state->cancelled.load()) return nullptr;
    QByteArray bytes = std::move(state->secret);
    state->secret.clear();
    char* result = static_cast<char*>(std::malloc(std::size_t(bytes.size()) + 1));
    if (result) {
        std::memcpy(result, bytes.constData(), std::size_t(bytes.size()));
        result[bytes.size()] = '\0';
    }
    bytes.fill('\0');
    return result;
}
void desktopLayout(rfbClient* client, unsigned int /*reason*/, unsigned int result,
                   int width, int height, unsigned int count,
                   const rfbExtDesktopScreen* screens) {
    auto* state = stateOf(client);
    if (!state || state->cancelled.load() || result != 0) return;
    QVector<VncPane::Monitor> next;
    const QRect full(0, 0, width, height);
    if (width > 0 && height > 0 && count > 0 && count <= 64 && screens &&
        width == client->width && height == client->height) {
        for (unsigned int i=0; i<count; ++i) {
            const auto& s = screens[i];
            const QRect rect(int(s.x),int(s.y),int(s.width),int(s.height));
            bool duplicate=false;
            for (const auto& existing: next)
                if (existing.id == s.id) {duplicate=true;break;}
            if (duplicate || rect.isEmpty() || !full.contains(rect)) {next.clear();break;}
            next.append({s.id,rect,s.flags});
        }
        if (next.size() != int(count)) next.clear();
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    // No UI rebuild when the server repeats an unchanged layout in a probe.
    bool changed = state->monitors.size() != next.size();
    if (!changed) for (int i=0; i<next.size(); ++i) {
        if (next[i].id != state->monitors[i].id ||
            next[i].geometry != state->monitors[i].geometry ||
            next[i].flags != state->monitors[i].flags) {
            changed=true;break;
        }
    }
    if (changed) {
        state->monitors = std::move(next);
        ++state->topologyRevision;
    }
}
void publishFrame(rfbClient* client, const std::shared_ptr<VncSessionState>& state) {
    if (state->cancelled.load() || !client->frameBuffer ||
        client->width <= 0 || client->height <= 0 ||
        client->format.bitsPerPixel != 32 || client->format.bigEndian) return;
    const QImage view(client->frameBuffer, client->width, client->height,
                      client->width * 4, QImage::Format_RGB32);
    const QImage owned = view.copy();
    if (owned.isNull()) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    state->frame = owned;
    ++state->frameRevision;
}
void runSession(std::shared_ptr<VncSessionState> state, QByteArray host, int port) {
    rfbClient* client = rfbGetClient(8,3,4);
    if (!client) {
        state->setStatus(QStringLiteral("VNC · error al crear cliente"));
        state->secret.fill('\0'); state->secret.clear();
        state->finished.store(true);
        return;
    }
    client->format.bigEndian = FALSE;
    client->format.trueColour = TRUE;
    client->format.redMax=255;client->format.greenMax=255;client->format.blueMax=255;
    client->format.redShift=16;client->format.greenShift=8;client->format.blueShift=0;
    client->MallocFrameBuffer = &allocate;
    client->GotFrameBufferUpdate = &updated;
    client->GetPassword = &password;
    client->GotNeeblesScreenLayout = &desktopLayout;
    client->canHandleNewFBSize=TRUE;
    // Seconds. The worker may wait, but never the Qt thread.
    client->connectTimeout=5;
    client->readTimeout=5;
    rfbClientSetClientData(client, &g_ownerTag, state.get());
    QByteArray name = QByteArrayLiteral("neebles-connect-desktop");
    QByteArray endpoint = host + ':' + QByteArray::number(port);
    char* argv[] = {name.data(),endpoint.data(),nullptr};
    int argc=2;
    if (!rfbInitClient(client,&argc,argv)) {
        // LibVNCClient frees client on initialization failure.
        state->secret.fill('\0'); state->secret.clear();
        state->setStatus(QStringLiteral("VNC · conexión o autenticación fallida"));
        state->finished.store(true);
        return;
    }
    state->secret.fill('\0'); state->secret.clear();
    const bool formatOk = client->format.bitsPerPixel==32 && !client->format.bigEndian &&
        client->format.redShift==16 && client->format.greenShift==8 &&
        client->format.blueShift==0;
    if (!formatOk) {
        state->setStatus(QStringLiteral("VNC · formato RGB32 incompatible"));
    } else if (!state->cancelled.load()) {
        {
            std::lock_guard<std::mutex> lock(state->socketMutex);
            state->socket=client->sock;
        }
        state->authenticated.store(true);
        state->setStatus(QStringLiteral("VNC · conectado"));
        publishFrame(client,state);
        auto lastFrame=std::chrono::steady_clock::now()-std::chrono::seconds(1);
        auto lastReceive=std::chrono::steady_clock::now();
        auto lastProbe=lastReceive;
        while(!state->cancelled.load()) {
            std::deque<VncScreen::Input> events;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                events.swap(state->inputs);
            }
            bool failed=false;
            for (const auto& e: events) {
                if (state->cancelled.load()) break;
                rfbBool ok = e.kind == VncScreen::Input::Kind::Key ?
                    SendKeyEvent(client,e.key,e.down ? TRUE : FALSE) :
                    SendPointerEvent(client,e.x,e.y,e.mask);
                if (!ok) {failed=true;break;}
            }
            if (failed) break;
            const int ready=WaitForMessage(client, 10000); // 10ms, worker only.
            if (ready<0 || (ready>0 && !HandleRFBServerMessage(client))) break;
            const auto now=std::chrono::steady_clock::now();
            if (ready>0) lastReceive=now;
            // A silent network blackhole must eventually become disconnected.
            // A 1x1 nonincremental update asks the RFB server for a response;
            // it DOES NOT request a change to the remote screen resolution.
            if (now-lastReceive>std::chrono::seconds(6) &&
                now-lastProbe>std::chrono::seconds(6)) {
                if (!SendFramebufferUpdateRequest(client,0,0,1,1,FALSE)) break;
                lastProbe=now;
            }
            if (now-lastReceive>std::chrono::seconds(20)) break;
            if (ready>0 && now-lastFrame>=
                    std::chrono::milliseconds(40)) {
                publishFrame(client,state);
                lastFrame=std::chrono::steady_clock::now();
            }
        }
        if (!state->cancelled.load())
            state->setStatus(QStringLiteral("VNC · remoto desconectado o error RFB"));
    }
    state->authenticated.store(false);
    {
        std::lock_guard<std::mutex> lock(state->socketMutex);
        state->socket=-1;
    }
    client->frameBuffer=nullptr; // Framebuffer belongs to state, not LibVNCClient.
    rfbClientCleanup(client);
    state->pixels.clear();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->frame=QImage();
        ++state->frameRevision;
        state->monitors.clear();
        ++state->topologyRevision;
        state->inputs.clear();
    }
    state->finished.store(true);
}
} // namespace

VncScreen::VncScreen(QWidget* parent): QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(0,0);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
}
void VncScreen::setFrame(QImage frame) {
    const QSize old=frame_.size();
    frame_=std::move(frame);
    if (old != frame_.size()) updateGeometry();
    update();
}
void VncScreen::setViewport(QRect viewport) {
    if (viewport_ == viewport) return;
    releaseKeys();
    viewport_=viewport;
    resized();
}
void VncScreen::setInputHandler(std::function<void(Input)> handler) {
    releaseKeys();
    send_=std::move(handler);
}
void VncScreen::resized() {updateGeometry();update();}
QRect VncScreen::remoteViewport() const {
    if (frame_.isNull()) return {};
    const QRect full=frame_.rect();
    return viewport_.isEmpty() ? full : viewport_.intersected(full);
}
QRect VncScreen::fittedFrame() const {
    const QRect visible=remoteViewport();
    if (visible.isEmpty() || width()<=0 || height()<=0) return {};
    QSize size=visible.size();
    size.scale(this->size(),Qt::KeepAspectRatio);
    if (size.isEmpty()) return {};
    return QRect(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
}
QPoint VncScreen::remotePoint(const QPointF& local) const {
    const QRect fitted=fittedFrame(), source=remoteViewport();
    if (fitted.isEmpty() || source.isEmpty()) return {};
    const double sx=double(source.width())/double(fitted.width());
    const double sy=double(source.height())/double(fitted.height());
    return QPoint(source.x()+std::clamp(int(std::floor((local.x()-fitted.x())*sx)),0,source.width()-1),
                  source.y()+std::clamp(int(std::floor((local.y()-fitted.y())*sy)),0,source.height()-1));
}
void VncScreen::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(),QColor("#09090B"));
    const QRect fitted=fittedFrame();
    if (frame_.isNull() || fitted.isEmpty()) {
        painter.setPen(QColor("#A78BFA"));
        painter.drawText(rect(),Qt::AlignCenter,QStringLiteral("VNC desconectado"));
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
    painter.drawImage(QRectF(fitted),frame_,QRectF(remoteViewport()));
}
std::uint64_t VncScreen::keyId(const QKeyEvent* event) {
    const std::uint64_t scan=event->nativeScanCode();
    return scan ? scan | (std::uint64_t(1)<<32) : std::uint32_t(event->key());
}
std::uint32_t VncScreen::keysym(const QKeyEvent* event) {
    switch(event->key()) {
    case Qt::Key_Backspace:return XK_BackSpace;
    case Qt::Key_Tab:return XK_Tab;
    case Qt::Key_Backtab:return XK_ISO_Left_Tab;
    case Qt::Key_Return:case Qt::Key_Enter:return XK_Return;
    case Qt::Key_Escape:return XK_Escape;
    case Qt::Key_Delete:return XK_Delete;
    case Qt::Key_Insert:return XK_Insert;
    case Qt::Key_Home:return XK_Home;
    case Qt::Key_End:return XK_End;
    case Qt::Key_PageUp:return XK_Page_Up;
    case Qt::Key_PageDown:return XK_Page_Down;
    case Qt::Key_Left:return XK_Left;
    case Qt::Key_Right:return XK_Right;
    case Qt::Key_Up:return XK_Up;
    case Qt::Key_Down:return XK_Down;
    case Qt::Key_Shift:return XK_Shift_L;
    case Qt::Key_Control:return XK_Control_L;
    case Qt::Key_Alt:return XK_Alt_L;
    case Qt::Key_Meta:return XK_Super_L;
    default:break;
    }
    if (event->key()>=Qt::Key_F1 && event->key()<=Qt::Key_F12)
        return XK_F1+std::uint32_t(event->key()-Qt::Key_F1);
    if (!event->text().isEmpty()) {
        const std::uint32_t cp=event->text().front().unicode();
        if(cp>=32 && cp<=255)return cp;
        if(cp>255)return 0x01000000U|cp;
    }
    if(event->key()>=Qt::Key_A && event->key()<=Qt::Key_Z)
        return std::uint32_t('a'+event->key()-Qt::Key_A);
    return 0;
}
bool VncScreen::event(QEvent* event) {
    const auto kind=event->type();
    if (send_ && (kind==QEvent::ShortcutOverride || kind==QEvent::KeyPress ||
                  kind==QEvent::KeyRelease)) {
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Tab || key->key()==Qt::Key_Backtab) {
            if(kind==QEvent::KeyPress) down(key);
            else if(kind==QEvent::KeyRelease) up(key);
            else key->accept();
            return key->isAccepted();
        }
    }
    return QWidget::event(event);
}
bool VncScreen::focusNextPrevChild(bool) {return false;}
void VncScreen::down(QKeyEvent* event) {
    if (!send_) return;
    event->accept();
    if (event->isAutoRepeat()) return;
    const auto id=keyId(event);
    const auto mods=event->modifiers();
    const bool plain=!event->text().isEmpty() && event->text().front().isPrint() &&
        !(mods&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier));
    const std::uint32_t key=keysym(event);
    if (!key) return;
    if (plain) {
        if(std::find(pulsed_.begin(),pulsed_.end(),id)!=pulsed_.end())return;
        send_({Input::Kind::Key,key,true});
        send_({Input::Kind::Key,key,false});
        pulsed_.push_back(id);
    } else {
        if(std::any_of(held_.begin(),held_.end(),[id](const auto& h){return h.id==id;}))return;
        send_({Input::Kind::Key,key,true});
        held_.push_back({id,key});
    }
}
void VncScreen::up(QKeyEvent* event) {
    if(!send_)return;
    event->accept();
    if(event->isAutoRepeat())return;
    const auto id=keyId(event);
    const auto it=std::find(pulsed_.begin(),pulsed_.end(),id);
    if(it!=pulsed_.end()){pulsed_.erase(it);return;}
    const auto held=std::find_if(held_.begin(),held_.end(),[id](const auto& h){return h.id==id;});
    if(held!=held_.end()){
        send_({Input::Kind::Key,held->keysym,false});
        held_.erase(held);
    }
}
void VncScreen::releaseKeys() {
    if(send_)for(const auto& held:held_)send_({Input::Kind::Key,held.keysym,false});
    held_.clear();pulsed_.clear();
}
void VncScreen::keyPressEvent(QKeyEvent* e){down(e);}
void VncScreen::keyReleaseEvent(QKeyEvent* e){up(e);}
void VncScreen::focusOutEvent(QFocusEvent* e){releaseKeys();QWidget::focusOutEvent(e);}
void VncScreen::sendMouse(QMouseEvent* e) {
    if(!send_)return;
    const QRect fitted=fittedFrame();
    if(fitted.isEmpty())return;
    if(!fitted.contains(e->position().toPoint()) &&
       (e->type()==QEvent::MouseButtonPress ||
       (e->type()==QEvent::MouseMove && e->buttons()==Qt::NoButton)))return;
    const QPoint pos=remotePoint(e->position());
    Input in{};in.kind=Input::Kind::Pointer;
    in.x=pos.x();in.y=pos.y();in.mask=buttonMask(e->buttons());
    in.motion=e->type()==QEvent::MouseMove;
    send_(in);
    e->accept();
}
void VncScreen::mousePressEvent(QMouseEvent* e){setFocus();sendMouse(e);}
void VncScreen::mouseReleaseEvent(QMouseEvent* e){sendMouse(e);}
void VncScreen::mouseMoveEvent(QMouseEvent* e){sendMouse(e);}
void VncScreen::wheelEvent(QWheelEvent* e) {
    if(!send_ || !fittedFrame().contains(e->position().toPoint()))return;
    const int delta=e->angleDelta().y();if(delta==0)return;
    const QPoint pos=remotePoint(e->position());
    Input press{};press.kind=Input::Kind::Pointer;
    press.x=pos.x();press.y=pos.y();
    const int base=buttonMask(e->buttons());
    press.mask=base|(delta>0?rfbButton4Mask:rfbButton5Mask);
    send_(press);
    press.mask=base;send_(press);
    e->accept();
}

VncPane::VncPane(QString host,int port,QWidget* parent)
    : QWidget(parent),host_(std::move(host)),port_(port) {
    auto* layout=new QVBoxLayout(this);
    auto* toolbar=new QHBoxLayout();
    toolbar->addWidget(new QLabel(QStringLiteral("VNC · %1:%2").arg(host_).arg(port_),this));
    toolbar->addStretch();
    connect_=new QPushButton(QStringLiteral("Conectar"),this);
    disconnect_=new QPushButton(QStringLiteral("Desconectar"),this);
    toolbar->addWidget(connect_);toolbar->addWidget(disconnect_);
    layout->addLayout(toolbar);
    screen_=new VncScreen(this);
    layout->addWidget(screen_,1);
    status_=new QLabel(QStringLiteral("VNC · desconectado"),this);
    layout->addWidget(status_);
    timer_=new QTimer(this);timer_->setInterval(40);
    QObject::connect(timer_,&QTimer::timeout,this,[this](){poll();});
    QObject::connect(connect_,&QPushButton::clicked,this,[this](){
        if (connectRequestHandler_) connectRequestHandler_();
        else connectNow();
    });
    QObject::connect(disconnect_,&QPushButton::clicked,this,[this](){disconnectNow();});
    updateUi(QStringLiteral("Desconectado"));
}
VncPane::~VncPane(){disconnectNow();}
bool VncPane::connected() const {
    return session_ && !session_->cancelled.load() && session_->authenticated.load();
}
void VncPane::updateUi(const QString& text) {
    const bool busy=session_ && !session_->finished.load();
    connect_->setEnabled(!busy);
    disconnect_->setEnabled(busy);
    status_->setText(text);
}
void VncPane::connectNow(const QString& oneShotPassword) {
    if(accessGuard_ && !accessGuard_())return;
    if(session_ && !session_->finished.load())return;
    if(host_.trimmed().isEmpty() || port_<1 || port_>65535 ||
       host_.contains(':') || host_.contains('/') || host_.contains(' ')) {
        updateUi(QStringLiteral("VNC · dirección inválida"));return;
    }
    QString provided=oneShotPassword;
    if(provided.isEmpty() && credentialProvider_ && !credentialProvider_(provided)) {
        provided.fill(QChar('\0'));return;
    }
    if(provided.isEmpty()) {
        bool accepted=false;
        provided=QInputDialog::getText(this,QStringLiteral("Credenciales VNC"),
            QStringLiteral("Contraseña del servidor VNC:"),QLineEdit::Password,{},&accepted);
        if(!accepted){provided.fill(QChar('\0'));return;}
    }
    auto state=std::make_shared<VncSessionState>();
    state->secret=provided.toUtf8();
    provided.fill(QChar('\0'));provided.clear();
    session_=state;
    observedFrame_=0;observedTopology_=0;
    monitors_.clear();selectedMonitor_=-1;selectedMonitorId_=0;
    ++topologyRevision_;
    screen_->setFrame({});screen_->setViewport({});
    std::weak_ptr<VncSessionState> weak=state;
    screen_->setInputHandler([weak](VncScreen::Input input) {
        if(auto current=weak.lock())current->enqueue(input);
    });
    updateUi(QStringLiteral("VNC · conectando…"));
    timer_->start();
    std::thread([state,host=host_.toUtf8(),port=port_]() mutable {
        runSession(std::move(state),std::move(host),port);
    }).detach();
}
void VncPane::poll() {
    auto state=session_;
    if(!state)return;
    QImage frame;
    QString status;
    QVector<Monitor> next;
    std::uint64_t f=0,t=0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        f=state->frameRevision;
        t=state->topologyRevision;
        if(f!=observedFrame_)frame=state->frame;
        if(t!=observedTopology_)next=state->monitors;
        status=state->status;
    }
    if(f!=observedFrame_){observedFrame_=f;screen_->setFrame(frame);}
    if(t!=observedTopology_){
        observedTopology_=t;
        if(selectedMonitor_>=0){
            int found=-1;
            for(int i=0;i<next.size();++i)
                if(next[i].id==selectedMonitorId_){found=i;break;}
            selectedMonitor_=found;
            if(found<0)selectedMonitorId_=0;
        }
        monitors_=std::move(next);
        ++topologyRevision_;
        screen_->setViewport(selectedViewport());
    }
    updateUi(status);
    if(state->finished.load()){
        screen_->setInputHandler({});
        screen_->setFrame({});
        screen_->setViewport({});
        monitors_.clear();selectedMonitor_=-1;selectedMonitorId_=0;
        ++topologyRevision_;
        session_.reset();
        timer_->stop();
        connect_->setEnabled(true);disconnect_->setEnabled(false);
    }
}
QRect VncPane::selectedViewport() const {
    if(selectedMonitor_>=0 && selectedMonitor_<monitors_.size())
        return monitors_.at(selectedMonitor_).geometry;
    return {};
}
bool VncPane::selectMonitor(int index) {
    if(!connected() || index < -1 || index>=monitors_.size())return false;
    selectedMonitor_=index;
    selectedMonitorId_=index>=0?monitors_[index].id:0;
    screen_->setViewport(selectedViewport());
    return true;
}
bool VncPane::copySelectedMonitor() const {
    if(!connected())return false;
    const QRect selected=selectedViewport();
    // Always use immutable Qt-side snapshot, never read worker's RFB pixels.
    const auto state=session_;
    if(!state)return false;
    QImage frame;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        frame=state->frame;
    }
    if(frame.isNull())return false;
    const QRect rect=selected.isEmpty()?frame.rect():selected;
    if(!frame.rect().contains(rect))return false;
    QGuiApplication::clipboard()->setImage(frame.copy(rect));
    return true;
}
void VncPane::disconnectNow() {
    if(timer_)timer_->stop();
    if(screen_){screen_->setInputHandler({});screen_->setFrame({});screen_->setViewport({});}
    auto old=std::move(session_);
    if(old)old->stop(); // Bounded, no join() and no QObject access by worker.
    monitors_.clear();selectedMonitor_=-1;selectedMonitorId_=0;
    ++topologyRevision_;observedFrame_=0;observedTopology_=0;
    if(status_)updateUi(QStringLiteral("VNC · desconectado"));
}
