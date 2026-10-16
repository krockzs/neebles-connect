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
#include <QSplashScreen>
#include <QTimer>
#include <cstdio>

namespace {
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
    return app.exec();
}
