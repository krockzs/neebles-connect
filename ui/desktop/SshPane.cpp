#include "SshPane.h"
#include "SshTerminal.h"

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
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <memory>
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
        if (connected>=0) break;
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
    QObject::connect(connect_,&QPushButton::clicked,this,[this](){connectNow();});
    QObject::connect(disconnect_,&QPushButton::clicked,this,[this](){disconnectNow();});
}

SshPane::~SshPane() { disconnectNow(); }

void SshPane::postStatus(const QString& status) {
    QMetaObject::invokeMethod(this,[this,status]() {
        status_->setText(status);
        if (!running_.load()) {connect_->setEnabled(true);disconnect_->setEnabled(false);}
    },Qt::QueuedConnection);
}

void SshPane::postBytes(const QByteArray& bytes) {
    QMetaObject::invokeMethod(this,[this,bytes]() {terminal_->receive(bytes);},Qt::QueuedConnection);
}

void SshPane::requestSize(int rows,int cols) {rows_.store(rows);cols_.store(cols);}

void SshPane::sendBytes(QByteArray bytes) {
    if (!running_.load() || bytes.isEmpty()) return;
    std::lock_guard<std::mutex> lock(outputLock_);
    outbound_.push_back(std::move(bytes));
}

bool SshPane::reviewHostKey(const QString& fp) {
    auto verdict=std::make_shared<std::atomic<int>>(0);
    QMetaObject::invokeMethod(this,[this,fp,verdict]() {
        const bool accepted=reviewPin(host_,port_,fp,this);
        verdict->store(accepted?1:-1);
    },Qt::QueuedConnection);
    while(running_.load() && verdict->load()==0)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    return running_.load() && verdict->load()==1;
}

void SshPane::connectNow(const QString& oneShotPassword) {
    if (running_.load()) return;
    if (worker_.joinable()) worker_.join();
    if (user_.trimmed().isEmpty()) {
        QMessageBox::warning(this,QStringLiteral("SSH"),QStringLiteral("SSH necesita un nombre de usuario."));
        return;
    }
    QString password=oneShotPassword;
    if (password.isEmpty()) {
        bool ok=false;
        password=QInputDialog::getText(this,QStringLiteral("Autenticación SSH"),
            QStringLiteral("Clave SSH de %1@%2 (no se guardará):").arg(user_,host_),
            QLineEdit::Password,QString(),&ok);
        if (!ok) {password.fill(QChar('\0'));return;}
    }
    QByteArray secret=password.toUtf8();
    password.fill(QChar('\0'));
    running_.store(true);
    connect_->setEnabled(false);
    disconnect_->setEnabled(true);
    status_->setText(QStringLiteral("SSH · iniciando conexión y verificación de identidad…"));
    rows_.store(terminal_->rows());cols_.store(terminal_->cols());
    worker_=std::thread([this,secret=std::move(secret)]() mutable {
        runSession(std::move(secret));
        running_.store(false);
    });
}

void SshPane::disconnectNow() {
    running_.store(false);
    const int fd=sock_.load();
    if (fd>=0) ::shutdown(fd,SHUT_RDWR);
    if (worker_.joinable()) worker_.join();
    {std::lock_guard<std::mutex> lock(outputLock_);outbound_.clear();}
    sock_.store(-1);
    if (status_) status_->setText(QStringLiteral("SSH · desconectado"));
    if (connect_) connect_->setEnabled(true);
    if (disconnect_) disconnect_->setEnabled(false);
}

void SshPane::runSession(QByteArray password) {
    const QByteArray address=host_.toUtf8();
    const QByteArray user=user_.toUtf8();
    auto clearSecret=[&password](){password.fill('\0');password.clear();};
    static std::once_flag initialized;
    static int initResult=-1;
    std::call_once(initialized,[](){initResult=libssh2_init(0);});
    if (initResult!=0) {clearSecret();postStatus(QStringLiteral("SSH · no se pudo inicializar libssh2"));return;}
    const int fd=connectSocket(address,port_,running_);
    if (fd<0 || !running_.load()) {
        if (fd>=0) ::close(fd);
        clearSecret();postStatus(QStringLiteral("SSH · conexión TCP no disponible"));return;
    }
    sock_.store(fd);
    LIBSSH2_SESSION* session=libssh2_session_init();
    LIBSSH2_CHANNEL* channel=nullptr;
    QString finalStatus=QStringLiteral("SSH · desconectado");
    if (!session) finalStatus=QStringLiteral("SSH · no se pudo crear sesión");
    else {
        libssh2_session_set_blocking(session,0);
        do {
            if (!retryUntil([&](){return libssh2_session_handshake(session,fd);},running_,fd)) {
                finalStatus=QStringLiteral("SSH · error de negociación/cifrado");break;
            }
            size_t keyLen=0;int keyType=0;
            const char* key=libssh2_session_hostkey(session,&keyLen,&keyType);
            if (!key || keyLen==0 || keyType==LIBSSH2_HOSTKEY_TYPE_UNKNOWN) {
                finalStatus=QStringLiteral("SSH · clave pública del servidor inválida");break;
            }
            const auto hash=QCryptographicHash::hash(QByteArray(key,static_cast<qsizetype>(keyLen)),QCryptographicHash::Sha256);
            const QString fp=QStringLiteral("SHA256:")+QString::fromLatin1(hash.toBase64(QByteArray::OmitTrailingEquals));
            if (!reviewHostKey(fp)) {
                finalStatus=QStringLiteral("SSH · identidad del servidor no autorizada");break;
            }
            // No password is sent until the user confirms or an existing fingerprint matches.
            if (!retryUntil([&](){return libssh2_userauth_password_ex(session,user.constData(),
                    static_cast<unsigned int>(user.size()),password.constData(),
                    static_cast<unsigned int>(password.size()),nullptr);},running_,fd)) {
                finalStatus=QStringLiteral("SSH · autenticación rechazada");break;
            }
            clearSecret();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
            while (running_.load() && std::chrono::steady_clock::now()<deadline && !channel) {
                channel=libssh2_channel_open_session(session);
                if (!channel) {
                    if (libssh2_session_last_errno(session)!=LIBSSH2_ERROR_EAGAIN) break;
                    struct pollfd wait{fd,POLLIN|POLLOUT,0};::poll(&wait,1,kWaitMs);
                }
            }
            if (!channel) {finalStatus=QStringLiteral("SSH · canal no disponible");break;}
            if (!retryUntil([&](){return libssh2_channel_request_pty_ex(channel,"xterm-256color",14,
                      nullptr,0,cols_.load(),rows_.load(),0,0);},running_,fd)) {
                finalStatus=QStringLiteral("SSH · servidor rechazó PTY");break;
            }
            if (!retryUntil([&](){return libssh2_channel_shell(channel);},running_,fd)) {
                finalStatus=QStringLiteral("SSH · servidor rechazó shell interactiva");break;
            }
            postStatus(QStringLiteral("SSH · conectado a %1:%2").arg(host_).arg(port_));
            QByteArray pending;
            int lastRows=rows_.load(),lastCols=cols_.load();
            while(running_.load() && !libssh2_channel_eof(channel)) {
                if (lastRows!=rows_.load() || lastCols!=cols_.load()) {
                    const int r=rows_.load(),c=cols_.load();
                    const int rc=libssh2_channel_request_pty_size(channel,c,r);
                    if (rc==0) {lastRows=r;lastCols=c;}
                    else if (rc!=LIBSSH2_ERROR_EAGAIN) break;
                }
                if (pending.isEmpty()) {
                    std::lock_guard<std::mutex> lock(outputLock_);
                    if (!outbound_.empty()) {pending=std::move(outbound_.front());outbound_.pop_front();}
                }
                if (!pending.isEmpty()) {
                    const auto wr=libssh2_channel_write(channel,pending.constData(),static_cast<size_t>(pending.size()));
                    if (wr>0) pending.remove(0,static_cast<qsizetype>(wr));
                    else if (wr<0 && wr!=LIBSSH2_ERROR_EAGAIN) break;
                }
                char buffer[8192];
                bool received=false;
                for(int i=0;i<16;++i) {
                    const auto rd=libssh2_channel_read(channel,buffer,sizeof(buffer));
                    if (rd>0) {postBytes(QByteArray(buffer,static_cast<qsizetype>(rd)));received=true;}
                    else {if (rd<0 && rd!=LIBSSH2_ERROR_EAGAIN) finalStatus=QStringLiteral("SSH · error de lectura");break;}
                }
                if (!received) {
                    struct pollfd wait{fd,POLLIN|POLLOUT,0};::poll(&wait,1,30);
                }
            }
        } while(false);
        clearSecret();
        // Shutdown the socket first: no unbounded blocking on a remote host.
        ::shutdown(fd,SHUT_RDWR);
        if (channel) libssh2_channel_free(channel);
        libssh2_session_free(session);
    }
    clearSecret();
    sock_.store(-1);
    ::close(fd);
    postStatus(finalStatus);
}
