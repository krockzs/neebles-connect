#pragma once

#include <QByteArray>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <rfb/rfbclient.h>

class QLabel;
class QPushButton;
class QTimer;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QFocusEvent;

class VncScreen final : public QWidget {
public:
    explicit VncScreen(QWidget* parent = nullptr);
    void setClient(rfbClient* client);
    void resized();
    // Actualizaciones VNC se proyectan al rectángulo escalado de la ventana.
    void remoteUpdated(int x, int y, int w, int h);
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
    rfbClient* client_ = nullptr;
    std::vector<Held> held_;
    std::vector<std::uint64_t> pulsed_;
};

class VncPane final : public QWidget {
public:
    VncPane(QString host, int port, QWidget* parent = nullptr);
    ~VncPane() override;
    void connectNow(const QString& oneShotPassword = {});
    void setAccessGuard(std::function<bool()> guard) { accessGuard_ = std::move(guard); }
    void setCredentialProvider(std::function<bool(QString&)> provider) { credentialProvider_ = std::move(provider); }
    void disconnectNow();
    bool connected() const { return client_ != nullptr && authenticated_; }
private:
    static VncPane* owner(rfbClient*);
    static rfbBool allocate(rfbClient*);
    static void updateRect(rfbClient*, int, int, int, int);
    static char* getPassword(rfbClient*);
    void handleAllocate(rfbClient*);
    void handleUpdate(int, int, int, int);
    char* handlePassword();
    void poll();
    void updateUi(const QString& status);

    std::function<bool()> accessGuard_;
    std::function<bool(QString&)> credentialProvider_;
    QString host_;
    int port_;
    VncScreen* screen_ = nullptr;
    QPushButton* connect_ = nullptr;
    QPushButton* disconnect_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* timer_ = nullptr;
    rfbClient* client_ = nullptr;
    std::vector<std::uint8_t> frame_;
    QByteArray oneShotPassword_;
    bool authenticated_ = false;
};
