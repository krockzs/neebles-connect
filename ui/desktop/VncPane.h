#pragma once

#include <QByteArray>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

class QLabel;
class QPushButton;
class QTimer;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QFocusEvent;
struct VncSessionState;

// UI-only framebuffer presentation. No LibVNCClient calls on Qt thread.
class VncScreen final : public QWidget {
public:
    struct Input {
        enum class Kind { Key, Pointer } kind = Kind::Key;
        std::uint32_t key = 0;
        bool down = false;
        int x = 0, y = 0, mask = 0;
        bool motion = false;
    };
    explicit VncScreen(QWidget* parent = nullptr);
    void setFrame(QImage frame);
    void setViewport(QRect viewport);
    void setInputHandler(std::function<void(Input)> handler);
    void resized();
protected:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    bool focusNextPrevChild(bool) override;
private:
    struct Held { std::uint64_t id; std::uint32_t keysym; };
    static std::uint64_t keyId(const QKeyEvent*);
    static std::uint32_t keysym(const QKeyEvent*);
    void down(QKeyEvent*);
    void up(QKeyEvent*);
    void releaseKeys();
    void sendMouse(QMouseEvent*);
    QRect fittedFrame() const;
    QPoint remotePoint(const QPointF& local) const;
    QRect remoteViewport() const;
    QImage frame_;
    QRect viewport_; // Empty = complete framebuffer.
    std::function<void(Input)> send_;
    std::vector<Held> held_;
    std::vector<std::uint64_t> pulsed_;
};

class VncPane final : public QWidget {
public:
    struct Monitor {
        std::uint32_t id = 0;
        QRect geometry;
        std::uint32_t flags = 0;
    };
    VncPane(QString host, int port, QWidget* parent = nullptr);
    ~VncPane() override;
    void connectNow(const QString& oneShotPassword = {});
    void setAccessGuard(std::function<bool()> guard) { accessGuard_ = std::move(guard); }
    void setCredentialProvider(std::function<bool(QString&)> provider) { credentialProvider_ = std::move(provider); }
    void setConnectRequestHandler(std::function<void()> handler) { connectRequestHandler_ = std::move(handler); }
    void disconnectNow();
    bool connected() const;
    const QVector<Monitor>& monitors() const { return monitors_; }
    std::uint64_t topologyRevision() const { return topologyRevision_; }
    int selectedMonitor() const { return selectedMonitor_; }
    bool selectMonitor(int index);
    bool copySelectedMonitor() const;
private:
    void poll();
    void updateUi(const QString& status);
    QRect selectedViewport() const;
    std::function<bool()> accessGuard_;
    std::function<bool(QString&)> credentialProvider_;
    std::function<void()> connectRequestHandler_;
    QString host_;
    int port_;
    VncScreen* screen_ = nullptr;
    QPushButton* connect_ = nullptr;
    QPushButton* disconnect_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* timer_ = nullptr;
    std::shared_ptr<VncSessionState> session_;
    std::uint64_t observedFrame_ = 0;
    std::uint64_t observedTopology_ = 0;
    QVector<Monitor> monitors_;
    int selectedMonitor_ = -1;
    std::uint32_t selectedMonitorId_ = 0;
    std::uint64_t topologyRevision_ = 0;
};
