#include "MainWindow.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QScreen>
#include <QSocketNotifier>
#include <QSplashScreen>
#include <QTimer>
#include <cstdio>
#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {
// SIGTERM comes from the Rust governor. Signal handlers may only write to
// a nonblocking pipe; QWidget shutdown runs later on the Qt event loop.
volatile sig_atomic_t g_shutdownWriteFd = -1;

extern "C" void requestShutdownSignal(int) {
    const int fd = static_cast<int>(g_shutdownWriteFd);
    if (fd < 0) return;
    const int previousErrno = errno;
    const char byte = 1;
    (void)::write(fd, &byte, 1); // async-signal-safe; pipe may already be full
    errno = previousErrno;
}

QString moduleRoot() {
    // El binario se instala en <modulo>/bin/. El modulo se identifica
    // solo por contenido instalado, nunca por paths del checkout/laboratorio.
    const QDir bin(QCoreApplication::applicationDirPath());
    const QDir root(bin.filePath(".."));
    QFile manifest(root.filePath("manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    if (!document.isObject() ||
        document.object().value("name").toString() != QStringLiteral("neebles-connect"))
        return {};
    if (!QFile::exists(root.filePath("assets/neebles-connect-logo.png")) ||
        !QFile::exists(root.filePath("assets/neebles-connect-icon.png")))
        return {};
    return root.absolutePath();
}

QString moduleVersion(const QString& root) {
    QFile file(QDir(root).filePath("manifest.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QString version = doc.object().value("version").toString();
        if (!version.isEmpty()) return version;
    }
    return QStringLiteral("development");
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Handle Boss/SIGTERM on Qt's event loop, not inside a POSIX handler.
    // A socket notifier wakes the application even during the splash screen.
    int shutdownPipe[2] = {-1,-1};
    if (::pipe2(shutdownPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        std::fputs("NEEBLES Connect: no se pudo crear canal de cierre.\n", stderr);
        return 3;
    }
    g_shutdownWriteFd = shutdownPipe[1];
    struct sigaction shutdownAction {};
    shutdownAction.sa_handler = requestShutdownSignal;
    sigemptyset(&shutdownAction.sa_mask);
    if (::sigaction(SIGTERM, &shutdownAction, nullptr) != 0 ||
        ::sigaction(SIGINT, &shutdownAction, nullptr) != 0) {
        std::fputs("NEEBLES Connect: no se pudo registrar cierre.\n", stderr);
        g_shutdownWriteFd = -1;
        ::close(shutdownPipe[0]);
        ::close(shutdownPipe[1]);
        return 3;
    }
    QSocketNotifier shutdownNotifier(shutdownPipe[0], QSocketNotifier::Read, &app);
    QObject::connect(&shutdownNotifier, &QSocketNotifier::activated, &app, [&app, &shutdownNotifier]() {
        shutdownNotifier.setEnabled(false);
        QApplication::closeAllWindows();
        app.quit();
    });
    QApplication::setApplicationName(QStringLiteral("NEEBLES Connect"));
    QApplication::setOrganizationName(QStringLiteral("NEEBLES"));
    QApplication::setDesktopFileName(QStringLiteral("neebles-connect"));

    const QString root = moduleRoot();
    if (root.isEmpty()) {
        std::fputs("NEEBLES Connect: falta el arbol instalado (manifest.json y assets junto a bin/).\n", stderr);
        return 2;
    }
    const QDir assets(root);
    const QString version = moduleVersion(root);
    const QIcon icon(assets.filePath("assets/neebles-connect-icon.png"));
    app.setWindowIcon(icon);

    QPixmap logo(assets.filePath("assets/neebles-connect-logo.png"));
    if (logo.isNull()) {
        // No inventar una imagen externa si falta el asset empaquetado.
        logo = QPixmap(640, 340);
        logo.fill(QColor("#09090B"));
    }
    QSize size(780, 440);
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QSize available = screen->availableGeometry().size();
        size = QSize(qMin(size.width(), available.width() - 80),
                     qMin(size.height(), available.height() - 80));
    }
    const QPixmap scaled = logo.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // Evitar que cerrar el splash termine QApplication antes de mostrar MainWindow.
    app.setQuitOnLastWindowClosed(false);
    QSplashScreen splash(scaled, Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    splash.setAttribute(Qt::WA_DeleteOnClose, false);
    splash.show();

    // La ventana principal NI EXISTE durante los 3000 ms del splash.
    QTimer::singleShot(3000, &app, [&app, &splash, root, version]() {
        auto* window = new MainWindow(root, version);
        window->setAttribute(Qt::WA_DeleteOnClose);
        splash.close();
        window->show();
        app.setQuitOnLastWindowClosed(true);
    });
    const int result = app.exec();
    // No other handler may write to the pipe after these descriptors close.
    ::signal(SIGTERM, SIG_DFL);
    ::signal(SIGINT, SIG_DFL);
    g_shutdownWriteFd = -1;
    ::close(shutdownPipe[1]);
    ::close(shutdownPipe[0]);
    return result;
}
