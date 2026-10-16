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

class QLabel;
class QPushButton;
class SshTerminal;

// M6 developer candidate: independent SSH worker, native libssh2 + libvterm.
// No module IPC, Governor, credential vault or runtime certification is claimed.
class SshPane final : public QWidget {
public:
    explicit SshPane(QString host, int port, QString username, QWidget* parent = nullptr);
    ~SshPane() override;
    void connectNow(const QString& oneShotPassword = {});
    void disconnectNow();
private:
    void runSession(QByteArray password);
    void postStatus(const QString& status);
    void postBytes(const QByteArray& bytes);
    bool reviewHostKey(const QString& fingerprint);
    void sendBytes(QByteArray bytes);
    void requestSize(int rows, int cols);
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
