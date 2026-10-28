#include "SshPane.h"
#include "SshTerminal.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <libssh2.h>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#ifdef __linux__
#include <netinet/tcp.h>
#endif
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <thread>
#include <system_error>
#include <mutex>
#include <utility>

namespace {
constexpr int kWaitMs=100;
constexpr int kHandshakeTimeoutMs=12000;

QString hostKeyPath() {
    const auto root=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return root.isEmpty()?QString():QDir(root).filePath(QStringLiteral("ssh-hostkeys.dev.json"));
}

QString identityKey(const QString& host,int port) {
    return host.toLower()+QStringLiteral(":")+QString::number(port);
}

// Fingerprint pinning is per host:port. This is a development-only trust store.
// Existing pins are NEVER replaced automatically, including after a failed match.
bool reviewPin(const QString& host,int port,const QString& fp,QWidget* parent) {
    const QString path=hostKeyPath();
    if (path.isEmpty()) return false;
    QFile source(path);
    QJsonObject doc;
    if (source.exists()) {
        if (!source.open(QIODevice::ReadOnly)) return false;
        QJsonParseError error{};
        const QJsonDocument parsed=QJsonDocument::fromJson(source.readAll(),&error);
        if (error.error!=QJsonParseError::NoError || !parsed.isObject()) return false;
        doc=parsed.object();
    }
    const QString id=identityKey(host,port);
    if (doc.contains(id)) {
        if (doc.value(id).toString()==fp) return true;
        QMessageBox::critical(parent,QStringLiteral("SSH: clave de servidor cambiada"),
            QStringLiteral("La huella SSH de %1:%2 NO coincide con la que aceptaste antes.\n"
                           "Se rechazó la conexión ANTES de enviar credenciales.\n"
                           "No se sobrescribirá la huella automáticamente.").arg(host).arg(port));
        return false;
    }
    const auto answer=QMessageBox::question(parent,QStringLiteral("Verificar identidad SSH"),
        QStringLiteral("Servidor: %1:%2\nHuella SHA-256:\n%3\n\n"
                       "Comprueba esta huella por un canal independiente. "
                       "¿Confías en esta clave pública y deseas guardarla como referencia?")
            .arg(host).arg(port).arg(fp),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
    if (answer!=QMessageBox::Yes) return false;
    const QString folder=QFileInfo(path).absolutePath();
    if (!QDir().mkpath(folder)) return false;
    QFile::setPermissions(folder,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    doc.insert(id,fp);
    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly)) return false;
    if (!target.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)) return false;
    const QByteArray encoded=QJsonDocument(doc).toJson(QJsonDocument::Indented);
    if (target.write(encoded)!=encoded.size()) return false;
    return target.commit();
}

int connectSocket(const QByteArray& host,int port,std::atomic<bool>& active) {
    struct addrinfo hints{};
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    struct addrinfo* addresses=nullptr;
    const QByteArray service=QByteArray::number(port);
    if (::getaddrinfo(host.constData(),service.constData(),&hints,&addresses)!=0) return -1;
    int connected=-1;
    for(auto* addr=addresses;addr && active.load();addr=addr->ai_next) {
        int fd=::socket(addr->ai_family,addr->ai_socktype|SOCK_CLOEXEC,addr->ai_protocol);
        if (fd<0) continue;
        const int oldFlags=::fcntl(fd,F_GETFL,0);
        if (oldFlags<0 || ::fcntl(fd,F_SETFL,oldFlags|O_NONBLOCK)<0) {::close(fd);continue;}
        int result=::connect(fd,addr->ai_addr,addr->ai_addrlen);
        if (result==0) {connected=fd;break;}
        if (result<0 && errno==EINPROGRESS) {
            struct pollfd check{fd,POLLOUT,0};
            for (int tries=0;tries<50 && active.load();++tries) {
                result=::poll(&check,1,kWaitMs);
                if (result==0 || (result<0 && errno==EINTR)) continue;
                if (result>0) {
                    int error=0;socklen_t len=sizeof(error);
                    if (::getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)==0 && error==0) connected=fd;
                }
                break;
            }
        }
        if (connected>=0) {
            // Detect dead peers after remote restart / path loss.
            const int enabled=1;
            ::setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,&enabled,sizeof(enabled));
#ifdef __linux__
            const int idle=15, interval=5, probes=3;
            ::setsockopt(fd,IPPROTO_TCP,TCP_KEEPIDLE,&idle,sizeof(idle));
            ::setsockopt(fd,IPPROTO_TCP,TCP_KEEPINTVL,&interval,sizeof(interval));
            ::setsockopt(fd,IPPROTO_TCP,TCP_KEEPCNT,&probes,sizeof(probes));
#endif
            break;
        }
        ::close(fd);
    }
    ::freeaddrinfo(addresses);
    return connected;
}

bool retryUntil(const std::function<int()>& operation, std::atomic<bool>& active, int fd,
                int timeoutMs=kHandshakeTimeoutMs) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
    while(active.load() && std::chrono::steady_clock::now()<deadline) {
        const int rc=operation();
        if (rc==0) return true;
        if (rc!=LIBSSH2_ERROR_EAGAIN) return false;
        struct pollfd wait{fd,POLLIN|POLLOUT,0};
        ::poll(&wait,1,kWaitMs);
    }
    return false;
}
} // namespace

SshPane::SshPane(QString host,int port,QString username,QWidget* parent)
  : QWidget(parent),host_(std::move(host)),port_(port),user_(std::move(username)) {
    auto* layout=new QVBoxLayout(this);
    auto* controls=new QHBoxLayout();
    connect_=new QPushButton(QStringLiteral("Conectar SSH"),this);
    disconnect_=new QPushButton(QStringLiteral("Desconectar"),this);
    disconnect_->setEnabled(false);
    controls->addWidget(connect_);
    controls->addWidget(disconnect_);
    controls->addStretch();
    layout->addLayout(controls);
    status_=new QLabel(QStringLiteral("SSH · %1@%2:%3 · desconectado").arg(user_,host_).arg(port_),this);
    layout->addWidget(status_);
    terminal_=new SshTerminal(this);
    layout->addWidget(terminal_,1);
    terminal_->setSendHandler([this](QByteArray b){sendBytes(std::move(b));});
    terminal_->setResizeHandler([this](int rows,int cols){requestSize(rows,cols);});
    QObject::connect(connect_,&QPushButton::clicked,this,[this](){
        if (connectRequestHandler_) connectRequestHandler_();
        else connectNow();
    });
    QObject::connect(disconnect_,&QPushButton::clicked,this,[this](){disconnectNow();});
}

SshPane::~SshPane() { disconnectNow(); }

bool SshPane::copySelectedText() const {
    return terminal_ && connected() && terminal_->copySelectionToClipboard();
}

void SshPane::postStatus(const SessionPtr& state, const QPointer<SshPane>& owner,
                         const QString& status) {
    // The detached worker never dereferences QWidget. Queued callbacks run
    // on Qt's main thread and discard stale sessions after reconnect/close.
    if (auto* app = QCoreApplication::instance()) {
        QMetaObject::invokeMethod(app, [state,owner,status]() {
            if (!owner || owner->session_ != state || state->cancelled.load()) return;
            owner->status_->setText(status);
            if (!state->running.load()) {
                owner->connect_->setEnabled(true);
                owner->disconnect_->setEnabled(false);
            }
        }, Qt::QueuedConnection);
    }
}

void SshPane::postBytes(const SessionPtr& state, const QPointer<SshPane>& owner,
                        const QByteArray& bytes) {
    // Backpressure: never enqueue an unbounded number of terminal updates if
    // Qt temporarily cannot repaint. Dropped output does not affect SSH I/O.
    constexpr int kUiPendingLimit = 4 * 1024 * 1024;
    const int count = int(bytes.size());
    if (count == 0 || count > kUiPendingLimit) return;
    const int previous = state->pendingUiBytes.fetch_add(count);
    if (previous > kUiPendingLimit - count) {
        state->pendingUiBytes.fetch_sub(count);
        return;
    }
    if (auto* app = QCoreApplication::instance()) {
        QMetaObject::invokeMethod(app, [state,owner,bytes,count]() {
            state->pendingUiBytes.fetch_sub(count);
            if (!owner || owner->session_ != state || state->cancelled.load()) return;
            owner->terminal_->receive(bytes);
        }, Qt::QueuedConnection);
    } else {
        state->pendingUiBytes.fetch_sub(count);
    }
}

void SshPane::requestSize(int rows,int cols) {
    if (session_) {
        session_->rows.store(rows);
        session_->cols.store(cols);
    }
}

void SshPane::sendBytes(QByteArray bytes) {
    auto state=session_;
    if (!state || !state->running.load() || bytes.isEmpty()) return;
    std::lock_guard<std::mutex> lock(state->outputLock);
    // Bound terminal input queued during stalled/disconnected networks.
    constexpr size_t kMaxPendingWrites=256;
    if (state->outbound.size() < kMaxPendingWrites)
        state->outbound.push_back(std::move(bytes));
}

bool SshPane::reviewHostKey(const SessionPtr& state,
                            const QPointer<SshPane>& owner,
                            const QString& host, int port,
                            const QString& fingerprint) {
    auto verdict=std::make_shared<std::atomic<int>>(0);
    if (auto* app = QCoreApplication::instance()) {
        QMetaObject::invokeMethod(app, [state,owner,verdict,host,port,fingerprint]() {
            if (!owner || owner->session_ != state || !state->running.load()) {
                verdict->store(-1);
                return;
            }
            const bool accepted=reviewPin(host,port,fingerprint,owner);
            verdict->store(accepted?1:-1);
        }, Qt::QueuedConnection);
    } else return false;
    // Host-key verification can be cancelled immediately by Stop or by
    // destroying the pane. The deadline prevents a pending prompt from
    // retaining an SSH worker indefinitely.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);
    while (state->running.load() && verdict->load()==0 &&
           std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    return state->running.load() && verdict->load()==1;
}

void SshPane::connectNow(const QString& oneShotPassword) {
    if (accessGuard_ && !accessGuard_()) return;
    if (session_ && session_->running.load()) return;
    if (user_.trimmed().isEmpty()) {
        QMessageBox::warning(this,QStringLiteral("SSH"),QStringLiteral("SSH necesita un nombre de usuario."));
        return;
    }
    QString password=oneShotPassword;
    if (password.isEmpty() && credentialProvider_ && !credentialProvider_(password)) {
        password.fill(QChar('\0')); return;
    }
    if (password.isEmpty()) {
        bool ok=false;
        password=QInputDialog::getText(this,QStringLiteral("Autenticación SSH"),
            QStringLiteral("Clave SSH de %1@%2 (no se guardará):").arg(user_,host_),
            QLineEdit::Password,QString(),&ok);
        if (!ok) {password.fill(QChar('\0'));return;}
    }
    QByteArray secret=password.toUtf8();
    password.fill(QChar('\0'));
    terminal_->clearSession();
    auto state=std::make_shared<SessionState>();
    state->rows.store(terminal_->rows());
    state->cols.store(terminal_->cols());
    session_=state;
    connect_->setEnabled(false);
    disconnect_->setEnabled(true);
    status_->setText(QStringLiteral("SSH · iniciando conexión y verificación de identidad…"));
    const QPointer<SshPane> owner(this);
    const QByteArray address=host_.toUtf8();
    const QByteArray username=user_.toUtf8();
    try {
        // No thread can outlive its own SessionState or refer directly to the
        // pane. Detached workers may complete after the UI tab is destroyed.
        std::thread([state,owner,secret=std::move(secret),address,username,port=port_]() mutable {
            runSession(state,owner,std::move(secret),address,username,port);
        }).detach();
    } catch (const std::system_error&) {
        state->running.store(false);
        state->cancelled.store(true);
        session_.reset();
        connect_->setEnabled(true);
        disconnect_->setEnabled(false);
        status_->setText(QStringLiteral("SSH · no se pudo crear el hilo de conexión"));
    }
}

void SshPane::disconnectNow() {
    auto state=session_;
    if (state) {
        state->cancelled.store(true);
        state->authenticated.store(false);
        state->running.store(false);
        // shutdown() wakes poll/libssh2 on the worker without closing its fd
        // behind its back. Only the worker owns the actual close().
        {
            std::lock_guard<std::mutex> lock(state->socketLock);
            if (state->socketFd>=0) ::shutdown(state->socketFd,SHUT_RDWR);
        }
        {
            std::lock_guard<std::mutex> lock(state->outputLock);
            state->outbound.clear();
        }
    }
    if (status_) status_->setText(QStringLiteral("SSH · desconectado"));
    if (connect_) connect_->setEnabled(true);
    if (disconnect_) disconnect_->setEnabled(false);
    // Deliberately no join(): DNS and libssh2 teardown never block Qt.
}

void SshPane::runSession(SessionPtr state, QPointer<SshPane> owner,
                         QByteArray password, QByteArray address,
                         QByteArray user, int port) {
    auto clearSecret=[&password](){password.fill('\0');password.clear();};
    auto finish=[&](const QString& status) {
        clearSecret();
        state->authenticated.store(false);
        state->running.store(false);
        postStatus(state,owner,status);
    };
    static std::once_flag initialized;
    static int initResult=-1;
    std::call_once(initialized,[](){initResult=libssh2_init(0);});
    if (initResult!=0) {
        finish(QStringLiteral("SSH · no se pudo inicializar libssh2"));
        return;
    }
    const int fd=connectSocket(address,port,state->running);
    if (fd<0 || !state->running.load()) {
        if (fd>=0) ::close(fd);
        finish(QStringLiteral("SSH · conexión TCP no disponible"));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state->socketLock);
        state->socketFd=fd;
        if (!state->running.load()) ::shutdown(fd,SHUT_RDWR);
    }
    LIBSSH2_SESSION* session=libssh2_session_init();
    LIBSSH2_CHANNEL* channel=nullptr;
    QString finalStatus=QStringLiteral("SSH · desconectado");
    if (!session) finalStatus=QStringLiteral("SSH · no se pudo crear sesión");
    else {
        libssh2_session_set_blocking(session,0);
        do {
            if (!retryUntil([&](){return libssh2_session_handshake(session,fd);},state->running,fd)) {
                finalStatus=QStringLiteral("SSH · error de negociación/cifrado");break;
            }
            size_t keyLen=0;int keyType=0;
            const char* key=libssh2_session_hostkey(session,&keyLen,&keyType);
            if (!key || keyLen==0 || keyType==LIBSSH2_HOSTKEY_TYPE_UNKNOWN) {
                finalStatus=QStringLiteral("SSH · clave pública del servidor inválida");break;
            }
            const auto hash=QCryptographicHash::hash(QByteArray(key,static_cast<qsizetype>(keyLen)),QCryptographicHash::Sha256);
            const QString fp=QStringLiteral("SHA256:")+QString::fromLatin1(hash.toBase64(QByteArray::OmitTrailingEquals));
            if (!reviewHostKey(state,owner,QString::fromUtf8(address),port,fp)) {
                finalStatus=QStringLiteral("SSH · identidad del servidor no autorizada");break;
            }
            // No password is sent until the user confirms or an existing fingerprint matches.
            if (!retryUntil([&](){return libssh2_userauth_password_ex(session,user.constData(),
                    static_cast<unsigned int>(user.size()),password.constData(),
                    static_cast<unsigned int>(password.size()),nullptr);},state->running,fd)) {
                finalStatus=QStringLiteral("SSH · autenticación rechazada");break;
            }
            clearSecret();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
            while (state->running.load() && std::chrono::steady_clock::now()<deadline && !channel) {
                channel=libssh2_channel_open_session(session);
                if (!channel) {
                    if (libssh2_session_last_errno(session)!=LIBSSH2_ERROR_EAGAIN) break;
                    struct pollfd wait{fd,POLLIN|POLLOUT,0};::poll(&wait,1,kWaitMs);
                }
            }
            if (!channel) {finalStatus=QStringLiteral("SSH · canal no disponible");break;}
            if (!retryUntil([&](){return libssh2_channel_request_pty_ex(channel,"xterm-256color",14,
                      nullptr,0,state->cols.load(),state->rows.load(),0,0);},state->running,fd)) {
                finalStatus=QStringLiteral("SSH · servidor rechazó PTY");break;
            }
            if (!retryUntil([&](){return libssh2_channel_shell(channel);},state->running,fd)) {
                finalStatus=QStringLiteral("SSH · servidor rechazó shell interactiva");break;
            }
            state->authenticated.store(true);
            postStatus(state,owner,QStringLiteral("SSH · conectado a %1:%2").arg(QString::fromUtf8(address)).arg(port));
            QByteArray pending;
            int lastRows=state->rows.load(),lastCols=state->cols.load();
            while(state->running.load() && !libssh2_channel_eof(channel)) {
                if (lastRows!=state->rows.load() || lastCols!=state->cols.load()) {
                    const int r=state->rows.load(),c=state->cols.load();
                    const int rc=libssh2_channel_request_pty_size(channel,c,r);
                    if (rc==0) {lastRows=r;lastCols=c;}
                    else if (rc!=LIBSSH2_ERROR_EAGAIN) break;
                }
                if (pending.isEmpty()) {
                    std::lock_guard<std::mutex> lock(state->outputLock);
                    if (!state->outbound.empty()) {pending=std::move(state->outbound.front());state->outbound.pop_front();}
                }
                if (!pending.isEmpty()) {
                    const auto wr=libssh2_channel_write(channel,pending.constData(),static_cast<size_t>(pending.size()));
                    if (wr>0) pending.remove(0,static_cast<qsizetype>(wr));
                    else if (wr<0 && wr!=LIBSSH2_ERROR_EAGAIN) break;
                }
                char buffer[8192];
                bool received=false;
                bool fatalRead=false;
                for(int i=0;i<16;++i) {
                    const auto rd=libssh2_channel_read(channel,buffer,sizeof(buffer));
                    if (rd>0) {postBytes(state,owner,QByteArray(buffer,static_cast<qsizetype>(rd)));received=true;}
                    else {
                        if (rd<0 && rd!=LIBSSH2_ERROR_EAGAIN) {
                            finalStatus=QStringLiteral("SSH · conexión perdida / error de lectura");
                            fatalRead=true;
                        }
                        break;
                    }
                }
                if (fatalRead) break;
                if (!received) {
                    struct pollfd wait{fd,POLLIN|POLLOUT,0};
                    const int ready=::poll(&wait,1,30);
                    if (ready>0 && (wait.revents & (POLLERR|POLLHUP|POLLNVAL))) {
                        finalStatus=QStringLiteral("SSH · conexión remota cerrada");
                        break;
                    }
                    if (ready<0 && errno!=EINTR) {
                        finalStatus=QStringLiteral("SSH · error de transporte");
                        break;
                    }
                }
            }
        } while(false);
        clearSecret();
        // Stop the socket before libssh2 cleanup; the GUI never waits for it.
        ::shutdown(fd,SHUT_RDWR);
        if (channel) libssh2_channel_free(channel);
        libssh2_session_free(session);
    }
    {
        std::lock_guard<std::mutex> lock(state->socketLock);
        state->socketFd=-1;
        ::close(fd);
    }
    finish(finalStatus);
}
