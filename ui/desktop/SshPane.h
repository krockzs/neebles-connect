#pragma once

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

class QLabel;
class QPushButton;
class SshTerminal;

// Network work owns an independent shared SessionState. QWidget never joins a
// network/DNS thread: cancellation and destruction must remain responsive.
class SshPane final : public QWidget {
public:
    explicit SshPane(QString host, int port, QString username, QWidget* parent = nullptr);
    ~SshPane() override;
    void connectNow(const QString& oneShotPassword = {});
    void setAccessGuard(std::function<bool()> guard) { accessGuard_ = std::move(guard); }
    void setCredentialProvider(std::function<bool(QString&)> provider) { credentialProvider_ = std::move(provider); }
    void setConnectRequestHandler(std::function<void()> handler) { connectRequestHandler_ = std::move(handler); }
    void disconnectNow();
    bool connected() const { return session_ && session_->authenticated.load(); }
    bool copySelectedText() const;
private:
    struct SessionState {
        std::atomic<bool> running{true};
        std::atomic<bool> cancelled{false};
        std::atomic<bool> authenticated{false};
        std::atomic<int> rows{24};
        std::atomic<int> cols{80};
        std::atomic<int> pendingUiBytes{0};
        std::mutex socketLock;
        int socketFd = -1;
        std::mutex outputLock;
        std::deque<QByteArray> outbound;
    };

    using SessionPtr = std::shared_ptr<SessionState>;
    static void runSession(SessionPtr state, QPointer<SshPane> owner,
                           QByteArray secret, QByteArray host,
                           QByteArray user, int port);
    static void postStatus(const SessionPtr& state, const QPointer<SshPane>& owner,
                           const QString& status);
    static void postBytes(const SessionPtr& state, const QPointer<SshPane>& owner,
                          const QByteArray& bytes);
    static bool reviewHostKey(const SessionPtr& state, const QPointer<SshPane>& owner,
                              const QString& host, int port, const QString& fingerprint);
    void sendBytes(QByteArray bytes);
    void requestSize(int rows, int cols);
    std::function<bool()> accessGuard_;
    std::function<bool(QString&)> credentialProvider_;
    std::function<void()> connectRequestHandler_;
    QString host_;
    int port_;
    QString user_;
    SshTerminal* terminal_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* connect_ = nullptr;
    QPushButton* disconnect_ = nullptr;
    SessionPtr session_;
};
