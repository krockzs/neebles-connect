// NEEBLES Connect desktop shell, milestone 1.
// Only VNC transport is wired. SSH is explicitly unimplemented, not simulated.
// Profiles are DEVELOPMENT-ONLY local metadata; Boss-owned persistence pending.
#include "MainWindow.h"
#include "VncPane.h"
#include "SshPane.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPair>
#include <QPixmap>
#include <QSize>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <utility>

namespace {
constexpr int kDashboard = 0;
constexpr int kQuick = 1;
constexpr int kCreate = 2;
constexpr int kSaved = 3;
constexpr int kSsh = 0;
constexpr int kVnc = 1;

QLabel* header(const QString& title, QWidget* parent) {
    auto* label = new QLabel(title, parent);
    label->setObjectName(QStringLiteral("sectionHeading"));
    return label;
}

QSpinBox* portBox(QWidget* parent, int port) {
    auto* value = new QSpinBox(parent);
    value->setRange(1, 65535);
    value->setValue(port);
    return value;
}

bool validHost(const QString& host) {
    return !host.trimmed().isEmpty() && !host.contains(' ') &&
           !host.contains('/') && !host.contains(':'); // IPv4 / hostname en este hito.
}

QString profilesFile() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (root.isEmpty()) return {};
    return QDir(root).filePath(QStringLiteral("profiles.dev.json"));
}

} // namespace

MainWindow::MainWindow(QString moduleRoot, QString version, QWidget* parent)
    : QMainWindow(parent), moduleRoot_(std::move(moduleRoot)), version_(std::move(version)) {
    setWindowTitle(QStringLiteral("N.E.E.B.L.E.S. Connect — v%1").arg(version_));
    setWindowIcon(QIcon(QDir(moduleRoot_).filePath("assets/neebles-connect-icon.png")));
    setMinimumSize(960, 600);
    resize(1320, 830);

    setStyleSheet(QStringLiteral(R"QSS(
        /* Negro oficial uniforme; violeta/cian solo para énfasis. */
        QWidget { background: #09090B; color: #F5F5F5; font-size: 13px; }
        QFrame#sidebar { background: #09090B; border-right: 1px solid #5B21B6; }
        QLabel#sectionHeading { font-size: 21px; font-weight: 700; color: #E9D5FF; margin: 8px 0; }
        QPushButton { background: #09090B; border: 1px solid #7C3AED; border-radius: 6px; padding: 8px 14px; }
        QPushButton:hover { background: #18181B; border-color: #A855F7; }
        QPushButton:disabled { color: #71717A; border-color: #52525B; }
        /* La vista administrativa activa permanece marcada hasta cambiar de vista. */
        QPushButton#adminNavButton:checked {
            background: #09090B; border: 1px solid #22D3EE;
            border-left: 3px solid #22D3EE; color: #67E8F9; font-weight: 700;
        }
        QPushButton#adminNavButton:checked:hover { background: #18181B; }
        QLineEdit, QSpinBox, QComboBox, QPlainTextEdit {
            background: #09090B; border: 1px solid #52525B; border-radius: 5px;
            selection-background-color: #7C3AED; padding: 7px; color: #F5F3FF;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #22D3EE; }
        QTabWidget::pane { border: 1px solid #52525B; background: #09090B; }
        QTabBar::tab { background: #09090B; color: #A1A1AA; padding: 9px 18px;
                       border: 1px solid #52525B; border-bottom: none; }
        QTabBar::tab:hover { background: #18181B; border-color: #A855F7; }
        QTabBar::tab:selected { background: #09090B; color: #67E8F9; border-color: #7C3AED; }
        QScrollArea, QScrollArea::viewport, QStatusBar { background: #09090B; }
        QStatusBar { color: #A78BFA; }
    )QSS"));

    auto* root = new QWidget(this);
    auto* horizontal = new QHBoxLayout(root);
    horizontal->setContentsMargins(0, 0, 0, 0);
    horizontal->setSpacing(0);
    sidebarReveal_ = new QPushButton(QStringLiteral("▶"), root);
    sidebarReveal_->setToolTip(QStringLiteral("Mostrar barra lateral"));
    sidebarReveal_->setFixedWidth(42);
    sidebarReveal_->hide();
    horizontal->addWidget(sidebarReveal_, 0, Qt::AlignTop);

    auto* sideFrame = new QFrame(root);
    sideFrame->setObjectName(QStringLiteral("sidebar"));
    sidebar_ = sideFrame;
    sidebar_->setFixedWidth(235);
    auto* sideLayout = new QVBoxLayout(sidebar_);
    auto* topBar = new QHBoxLayout();
    // Misma fotografía oficial que usa el splash; miniatura en cabecera.
    auto* brandLogo = new QLabel(sidebar_);
    brandLogo->setObjectName(QStringLiteral("sidebarBrandLogo"));
    brandLogo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QPixmap brandPhoto(QDir(moduleRoot_).filePath("assets/neebles-connect-logo.png"));
    if (brandPhoto.isNull())
        brandPhoto.load(QDir(moduleRoot_).filePath("assets/neebles-connect-icon.png"));
    if (!brandPhoto.isNull())
        brandLogo->setPixmap(brandPhoto.scaled(QSize(125, 76), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    brandLogo->setMinimumHeight(76);
    topBar->addWidget(brandLogo, 1);
    sidebarToggle_ = new QPushButton(QStringLiteral("◀"), sidebar_);
    sidebarToggle_->setToolTip(QStringLiteral("Ocultar barra lateral"));
    sidebarToggle_->setFixedWidth(40);
    topBar->addWidget(sidebarToggle_);
    sideLayout->addLayout(topBar);
    sideLayout->addSpacing(24);
    sidebarContent_ = new QWidget(sidebar_);
    auto* choices = new QVBoxLayout(sidebarContent_);
    choices->setContentsMargins(0, 0, 0, 0);
    for (const auto& action : QVector<QPair<QString,int>>{
             {QStringLiteral("◈  Dashboard"), kDashboard},
             {QStringLiteral("⚡  Quick Connect"), kQuick},
             {QStringLiteral("＋  Crear conexión"), kCreate},
             {QStringLiteral("▤  Mis conexiones"), kSaved}}) {
        auto* button = new QPushButton(action.first, sidebarContent_);
        button->setObjectName(QStringLiteral("adminNavButton"));
        button->setCheckable(true);
        adminButtons_.append(button);
        choices->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, this,
                         [this, view=action.second]() { showAdmin(view); });
    }
    sideLayout->addWidget(sidebarContent_);
    sideLayout->addStretch();
    auto* edition = new QLabel(QStringLiteral("Connect v%1").arg(version_), sidebar_);
    edition->setStyleSheet(QStringLiteral("color: #A78BFA;"));
    sideLayout->addWidget(edition);
    horizontal->addWidget(sidebar_);

    tabs_ = new QTabWidget(root);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(false);
    tabs_->setDocumentMode(true);
    horizontal->addWidget(tabs_, 1);
    adminStack_ = new QStackedWidget(tabs_);
    buildDashboard();
    buildQuick();
    buildEditor();
    buildSaved();
    tabs_->addTab(adminStack_, QStringLiteral("Dashboard"));
    // Pestaña administrativa no cerrable: quitar X, no sólo ignorar el clic.
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    tabs_->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    tabs_->tabBar()->setTabData(0, QStringLiteral("fixed:admin"));
    setCentralWidget(root);
    statusBar()->showMessage(QStringLiteral("Desarrollo · SSH/VNC de laboratorio · sin Governor"));
    QObject::connect(sidebarToggle_, &QPushButton::clicked, this, [this]() { toggleSidebar(); });
    QObject::connect(sidebarReveal_, &QPushButton::clicked, this, [this]() { toggleSidebar(); });
    QObject::connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    QObject::connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { syncNavigation(); });

    if (!loadProfiles())
        statusBar()->showMessage(QStringLiteral("No se pudieron leer los perfiles locales de desarrollo"));
    refreshSaved();
    showAdmin(kDashboard);
}

void MainWindow::toggleSidebar() {
    const bool expanded = sidebar_->isVisible();
    sidebar_->setVisible(!expanded);
    sidebarReveal_->setVisible(expanded);
}

void MainWindow::syncNavigation() {
    const int active = tabs_->currentIndex() == 0 ? adminStack_->currentIndex() : -1;
    for (int i = 0; i < adminButtons_.size(); ++i)
        adminButtons_[i]->setChecked(i == active);
}

void MainWindow::showAdmin(int view) {
    if (view < kDashboard || view > kSaved) return;
    adminStack_->setCurrentIndex(view);
    static const QVector<QString> titles = {
        QStringLiteral("Dashboard"), QStringLiteral("Quick Connect"),
        QStringLiteral("Crear conexión"), QStringLiteral("Mis conexiones")
    };
    tabs_->setTabText(0, titles.at(view));
    tabs_->setCurrentIndex(0);
    syncNavigation();
    if (view == kSaved) refreshSaved();
}

void MainWindow::buildDashboard() {
    auto* page = new QWidget(adminStack_);
    auto* outer = new QVBoxLayout(page);
    outer->addWidget(header(QStringLiteral("Dashboard"), page));
    auto* note = new QLabel(
        QStringLiteral("El contenido del Dashboard está pendiente de definir."), page);
    note->setStyleSheet(QStringLiteral("color: #A1A1AA;"));
    outer->addWidget(note);
    outer->addStretch();
    adminStack_->addWidget(page);
}

void MainWindow::buildQuick() {
    auto* page = new QWidget(adminStack_);
    auto* outer = new QVBoxLayout(page);
    outer->addWidget(header(QStringLiteral("Quick Connect"), page));
    outer->addWidget(new QLabel(QStringLiteral("Conexión temporal; no se guarda el equipo."), page));
    auto* form = new QFormLayout();
    quickHost_ = new QLineEdit(page);
    quickHost_->setPlaceholderText(QStringLiteral("IP LAN / Tailscale / hostname"));
    quickUser_ = new QLineEdit(page);
    quickUser_->setPlaceholderText(QStringLiteral("Solo SSH; opcional para VNC"));
    quickPassword_ = new QLineEdit(page);
    quickPassword_->setEchoMode(QLineEdit::Password);
    quickPassword_->setPlaceholderText(QStringLiteral("Opcional; si queda vacío, VNC solicitará la clave"));
    quickProtocol_ = new QComboBox(page);
    quickProtocol_->addItems({QStringLiteral("VNC"), QStringLiteral("SSH")});
    quickPort_ = portBox(page, 5900);
    form->addRow(QStringLiteral("IP / Hostname"), quickHost_);
    form->addRow(QStringLiteral("Usuario"), quickUser_);
    form->addRow(QStringLiteral("Contraseña"), quickPassword_);
    form->addRow(QStringLiteral("Protocolo"), quickProtocol_);
    form->addRow(QStringLiteral("Puerto"), quickPort_);
    outer->addLayout(form);
    auto* action = new QPushButton(QStringLiteral("Abrir conexión temporal"), page);
    action->setFixedWidth(260);
    outer->addWidget(action, 0, Qt::AlignHCenter);
    outer->addStretch();
    QObject::connect(quickProtocol_, &QComboBox::currentIndexChanged, this,
        [this, action](int selection) {
            quickPort_->setValue(selection == 0 ? 5900 : 22);
            quickPassword_->clear();
            quickPassword_->setEnabled(selection == 0);
            action->setText(selection == 0 ? QStringLiteral("Conectar VNC temporal")
                                           : QStringLiteral("Abrir vista SSH (pendiente)"));
        });
    action->setText(QStringLiteral("Conectar VNC temporal"));
    QObject::connect(action, &QPushButton::clicked, this, [this]() { openQuick(); });
    adminStack_->addWidget(page);
}

void MainWindow::buildEditor() {
    auto* page = new QWidget(adminStack_);
    auto* outer = new QVBoxLayout(page);
    outer->addWidget(header(QStringLiteral("Crear conexión"), page));
    auto* form = new QFormLayout();
    editName_ = new QLineEdit(page);
    editHost_ = new QLineEdit(page);
    editHost_->setPlaceholderText(QStringLiteral("IP LAN / Tailscale / hostname"));
    editSshUser_ = new QLineEdit(page);
    editSshPort_ = portBox(page, 22);
    editVncPort_ = portBox(page, 5900);
    form->addRow(QStringLiteral("Nombre del equipo"), editName_);
    form->addRow(QStringLiteral("Dirección"), editHost_);
    form->addRow(QStringLiteral("SSH · usuario"), editSshUser_);
    form->addRow(QStringLiteral("SSH · puerto"), editSshPort_);
    form->addRow(QStringLiteral("VNC · puerto"), editVncPort_);
    outer->addLayout(form);
    outer->addWidget(new QLabel(QStringLiteral(
        "Solo se guardan datos del equipo. Las contraseñas NO se almacenan.\n"
        "Perfiles de desarrollo: persistencia gobernada por Boss pendiente."), page));
    saveButton_ = new QPushButton(QStringLiteral("Guardar conexión"), page);
    outer->addWidget(saveButton_);
    auto* cancel = new QPushButton(QStringLiteral("Cancelar edición"), page);
    outer->addWidget(cancel);
    outer->addStretch();
    QObject::connect(saveButton_, &QPushButton::clicked, this, [this]() { saveProfile(); });
    QObject::connect(cancel, &QPushButton::clicked, this, [this]() { resetEditor(); showAdmin(kSaved); });
    adminStack_->addWidget(page);
}

void MainWindow::buildSaved() {
    auto* page = new QWidget(adminStack_);
    auto* outer = new QVBoxLayout(page);
    outer->addWidget(header(QStringLiteral("Mis conexiones"), page));
    auto* scroller = new QScrollArea(page);
    scroller->setWidgetResizable(true);
    auto* contents = new QWidget(scroller);
    savedList_ = new QVBoxLayout(contents);
    scroller->setWidget(contents);
    outer->addWidget(scroller, 1);
    adminStack_->addWidget(page);
}

void MainWindow::refreshSaved() {
    if (!savedList_) return;
    while (QLayoutItem* item = savedList_->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    if (profiles_.isEmpty()) {
        savedList_->addWidget(new QLabel(QStringLiteral("Todavía no hay equipos guardados.")));
    }
    for (const ConnectProfile& profile : profiles_) {
        auto* row = new QFrame();
        row->setStyleSheet(QStringLiteral("QFrame { border: 1px solid #5B21B6; border-radius: 8px; }"));
        auto* layout = new QHBoxLayout(row);
        const QString state = isOpen(profile.id) ? QStringLiteral(" · pestaña abierta") : QString();
        layout->addWidget(new QLabel(profile.name + QStringLiteral("\n") + profile.host + state, row), 1);
        auto* open = new QPushButton(QStringLiteral("Abrir / Enfocar"), row);
        auto* edit = new QPushButton(QStringLiteral("Editar"), row);
        auto* remove = new QPushButton(QStringLiteral("Eliminar"), row);
        layout->addWidget(open); layout->addWidget(edit); layout->addWidget(remove);
        QObject::connect(open, &QPushButton::clicked, this, [this, profile]() { openProfile(profile, -1); });
        QObject::connect(edit, &QPushButton::clicked, this, [this, profile]() { editProfile(profile); });
        QObject::connect(remove, &QPushButton::clicked, this, [this, profile]() { deleteProfile(profile); });
        savedList_->addWidget(row);
    }
    savedList_->addStretch();
}

void MainWindow::resetEditor() {
    editingId_.clear();
    editName_->clear(); editHost_->clear(); editSshUser_->clear();
    editSshPort_->setValue(22); editVncPort_->setValue(5900);
    saveButton_->setText(QStringLiteral("Guardar conexión"));
}

void MainWindow::editProfile(const ConnectProfile& profile) {
    if (isOpen(profile.id)) {
        QMessageBox::information(this, QStringLiteral("Sesión abierta"),
                                 QStringLiteral("Cierra la pestaña del equipo antes de editar su conexión."));
        return;
    }
    editingId_ = profile.id;
    editName_->setText(profile.name); editHost_->setText(profile.host);
    editSshUser_->setText(profile.sshUser);
    editSshPort_->setValue(profile.sshPort); editVncPort_->setValue(profile.vncPort);
    saveButton_->setText(QStringLiteral("Actualizar conexión"));
    showAdmin(kCreate);
}

void MainWindow::saveProfile() {
    const QString name = editName_->text().trimmed();
    const QString host = editHost_->text().trimmed();
    if (name.isEmpty() || !validHost(host)) {
        QMessageBox::warning(this, QStringLiteral("Datos inválidos"),
                             QStringLiteral("Escribe un nombre y una IP IPv4 o hostname válido."));
        return;
    }
    ConnectProfile profile;
    profile.id = editingId_.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : editingId_;
    profile.name = name; profile.host = host;
    profile.sshUser = editSshUser_->text().trimmed();
    profile.sshPort = editSshPort_->value(); profile.vncPort = editVncPort_->value();
    const QVector<ConnectProfile> before = profiles_;
    bool found = false;
    for (ConnectProfile& existing : profiles_) if (existing.id == profile.id) { existing = profile; found = true; break; }
    if (!found) profiles_.push_back(profile);
    if (!writeProfiles()) {
        profiles_ = before;
        QMessageBox::critical(this, QStringLiteral("No se pudo guardar"),
            QStringLiteral("Falló la escritura de los perfiles locales. No se aplicaron cambios."));
        return;
    }
    resetEditor();
    showAdmin(kSaved);
}

void MainWindow::deleteProfile(const ConnectProfile& profile) {
    if (isOpen(profile.id)) {
        QMessageBox::information(this, QStringLiteral("Pestaña abierta"),
            QStringLiteral("Primero cierra la pestaña de este equipo para terminar sus sesiones."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Eliminar conexión"),
            QStringLiteral("¿Eliminar el equipo guardado «%1»?").arg(profile.name)) != QMessageBox::Yes) return;
    const QVector<ConnectProfile> before = profiles_;
    for (int i = 0; i < profiles_.size(); ++i) if (profiles_[i].id == profile.id) {
        profiles_.removeAt(i); break;
    }
    if (!writeProfiles()) {
        profiles_ = before;
        QMessageBox::critical(this, QStringLiteral("Error"), QStringLiteral("No se pudieron guardar los cambios."));
    }
    refreshSaved();
}

bool MainWindow::isOpen(const QString& id) const {
    if (!tabs_) return false;
    for (int i = 1; i < tabs_->count(); ++i)
        if (tabs_->tabBar()->tabData(i).toString() == id) return true;
    return false;
}

void MainWindow::openQuick() {
    ConnectProfile profile;
    profile.id = QStringLiteral("quick:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile.host = quickHost_->text().trimmed();
    profile.name = QStringLiteral("Quick %1").arg(profile.host);
    profile.sshUser = quickUser_->text().trimmed();
    profile.sshPort = quickProtocol_->currentIndex() == 1 ? quickPort_->value() : 22;
    profile.vncPort = quickProtocol_->currentIndex() == 0 ? quickPort_->value() : 5900;
    if (!validHost(profile.host)) {
        QMessageBox::warning(this, QStringLiteral("Host inválido"), QStringLiteral("Escribe una IP IPv4 o hostname."));
        return;
    }
    QString password = quickPassword_->text();
    quickPassword_->clear();
    openProfile(profile, quickProtocol_->currentIndex() == 0 ? kVnc : kSsh, password);
    password.fill(QChar('\0'));
}

void MainWindow::openProfile(const ConnectProfile& profile, int protocol, const QString& password) {
    for (int i = 1; i < tabs_->count(); ++i) if (tabs_->tabBar()->tabData(i).toString() == profile.id) {
        tabs_->setCurrentIndex(i);
        if (protocol >= 0) {
            auto* sub = qobject_cast<QTabWidget*>(tabs_->widget(i));
            if (sub) sub->setCurrentIndex(protocol);
        }
        return;
    }
    auto* sub = new QTabWidget(tabs_);
    sub->setDocumentMode(true);
    auto* ssh = new SshPane(profile.host, profile.sshPort, profile.sshUser, sub);
    sub->addTab(ssh, QStringLiteral("SSH"));
    auto* vnc = new VncPane(profile.host, profile.vncPort, sub);
    sub->addTab(vnc, QStringLiteral("VNC"));
    sub->setCurrentIndex(protocol >= 0 ? protocol : kVnc);
    const int index = tabs_->addTab(sub, profile.name);
    tabs_->tabBar()->setTabData(index, profile.id);
    tabs_->setCurrentIndex(index);
    if (protocol == kVnc) {
        // Quick Connect es una acción única: preparar la pestaña y conectar
        // en el siguiente ciclo Qt, para que el diálogo RFB quede visible.
        // El objeto context evita usar el pane si se cerró la pestaña.
        QPointer<VncPane> guardedVnc(vnc);
        QString transientPassword = password;
        QTimer::singleShot(0, vnc, [guardedVnc, secret = std::move(transientPassword)]() mutable {
            if (guardedVnc) guardedVnc->connectNow(secret);
            secret.fill(QChar('\0'));
        });
    }
    if (protocol == kSsh) {
        // La conexión SSH se inicia al pulsar Quick Connect, sin segundo clic.
        QPointer<SshPane> guardedSsh(ssh);
        QString transientPassword = password;
        QTimer::singleShot(0, ssh, [guardedSsh, secret = std::move(transientPassword)]() mutable {
            if (guardedSsh) guardedSsh->connectNow(secret);
            secret.fill(QChar('\0'));
        });
    }
    refreshSaved();
}

void MainWindow::closeTab(int index) {
    if (index <= 0 || index >= tabs_->count()) return;
    QWidget* content = tabs_->widget(index);
    tabs_->removeTab(index);
    delete content; // Desconecta SSH y VNC del nodo, sin afectar otros nodos.
    refreshSaved();
}

bool MainWindow::loadProfiles() {
    const QString path = profilesFile();
    if (path.isEmpty()) return false;
    QFile source(path);
    if (!source.exists()) return true;
    if (!source.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(source.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) return false;
    QVector<ConnectProfile> loaded;
    for (const QJsonValue& item : doc.array()) {
        if (!item.isObject()) return false;
        const QJsonObject json = item.toObject();
        ConnectProfile profile;
        profile.id = json.value("id").toString();
        profile.name = json.value("name").toString();
        profile.host = json.value("host").toString();
        profile.sshUser = json.value("ssh_user").toString();
        profile.sshPort = json.value("ssh_port").toInt(22);
        profile.vncPort = json.value("vnc_port").toInt(5900);
        if (profile.id.isEmpty() || profile.name.isEmpty() || !validHost(profile.host) ||
            profile.sshPort < 1 || profile.sshPort > 65535 ||
            profile.vncPort < 1 || profile.vncPort > 65535) return false;
        for (const ConnectProfile& prior : loaded) if (prior.id == profile.id) return false;
        loaded.push_back(profile);
    }
    profiles_ = loaded;
    return true;
}

bool MainWindow::writeProfiles() {
    const QString path = profilesFile();
    if (path.isEmpty()) return false;
    const QString folder = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(folder)) return false;
    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QJsonArray items;
    for (const ConnectProfile& profile : profiles_) {
        QJsonObject object;
        object.insert("id", profile.id);
        object.insert("name", profile.name);
        object.insert("host", profile.host);
        object.insert("ssh_user", profile.sshUser);
        object.insert("ssh_port", profile.sshPort);
        object.insert("vnc_port", profile.vncPort);
        items.append(object);
    }
    QSaveFile writer(path);
    if (!writer.open(QIODevice::WriteOnly)) return false;
    if (!writer.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) return false;
    const QByteArray data = QJsonDocument(items).toJson(QJsonDocument::Indented);
    if (writer.write(data) != data.size()) return false;
    return writer.commit();
}
