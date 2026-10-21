#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

class QLabel;
class QPushButton;
class SshTerminal;

// Native SSH worker (libssh2 + libvterm); saved-session authorization comes from Secrets.
class SshPane final : public QWidget {
public:
    explicit SshPane(QString host, int port, QString username, QWidget* parent = nullptr);
    ~SshPane() override;
    void connectNow(const QString& oneShotPassword = {});
    void setAccessGuard(std::function<bool()> guard) { accessGuard_ = std::move(guard); }
    void setCredentialProvider(std::function<bool(QString&)> provider) { credentialProvider_ = std::move(provider); }
    void disconnectNow();
private:
    void runSession(QByteArray password);
    void postStatus(const QString& status);
    void postBytes(const QByteArray& bytes);
    bool reviewHostKey(const QString& fingerprint);
    void sendBytes(QByteArray bytes);
    void requestSize(int rows, int cols);
    std::function<bool()> accessGuard_;
    std::function<bool(QString&)> credentialProvider_;
    QString host_;
    int port_;
    QString user_;
    SshTerminal* terminal_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* connect_ = nullptr;
    QPushButton* disconnect_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<int> sock_{-1};
    std::atomic<int> rows_{24};
    std::atomic<int> cols_{80};
    std::mutex outputLock_;
    std::deque<QByteArray> outbound_;
    std::thread worker_;
};
