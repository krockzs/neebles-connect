// NEEBLES Connect 1.0.1. Saved host credentials live solely in Secrets vault-connect.
#include "MainWindow.h"
#include "VncPane.h"
#include "SshPane.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLocalSocket>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QtEndian>
#include <QJsonArray>
#include <QJsonValue>
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
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
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


// Secrets IPC is always local to the user session. This is not the Boss module IPC.
QJsonObject secretsRpc(const QJsonObject& message) {
    const QString runtime=qEnvironmentVariable("XDG_RUNTIME_DIR");
    if(runtime.isEmpty())return {};
    QLocalSocket socket;
    socket.connectToServer(QDir(runtime).filePath(QStringLiteral("neebles-secrets/secrets.sock")));
    if(!socket.waitForConnected(650))return {};
    const QByteArray body=QJsonDocument(message).toJson(QJsonDocument::Compact);
    if(body.isEmpty()||body.size()>16384)return {};
    const quint32 n=quint32(body.size());
    char prefix[4]={char((n>>24)&255),char((n>>16)&255),char((n>>8)&255),char(n&255)};
    if(socket.write(prefix,4)!=4||socket.write(body)!=body.size())return {};
    if(!socket.waitForBytesWritten(1500))return {};
    auto readExact=[&socket](int len)->QByteArray {
        QByteArray out;
        while(out.size()<len){
            const QByteArray part=socket.read(len-out.size());
            if(part.isEmpty()){
                if(!socket.waitForReadyRead(35000))return {};
            }else out.append(part);
        }
        return out;
    };
    const QByteArray header=readExact(4);
    if(header.size()!=4)return {};
    const quint32 size=(quint8(header[0])<<24)|(quint8(header[1])<<16)|
                       (quint8(header[2])<<8)|quint8(header[3]);
    if(size==0||size>16384)return {};
    const QByteArray response=readExact(int(size));
    if(response.size()!=int(size))return {};
    const auto data=QJsonDocument::fromJson(response);
    return data.isObject()?data.object():QJsonObject{};
}

QString secretsLabel(const QString& root,const QString& key) {
    QString language=qEnvironmentVariable("NEEBLES_LANGUAGE");
    if(language.isEmpty()) {
        QFile settings(QStringLiteral("/opt/neebles/shared/settings/local_settings_neebles-connect.json"));
        if(settings.open(QIODevice::ReadOnly))
            language=QJsonDocument::fromJson(settings.readAll()).object()
                         .value(QStringLiteral("ui")).toObject().value(QStringLiteral("language")).toString();
    }
    if(language.isEmpty())language=QLocale::system().name();
    const QString code=language.startsWith(QStringLiteral("es"),Qt::CaseInsensitive)
                       ?QStringLiteral("es_CL"):QStringLiteral("en_US");
    QFile file(QDir(root).filePath(QStringLiteral("languages/%1.json").arg(code)));
    if(file.open(QIODevice::ReadOnly)){
        const QString result=QJsonDocument::fromJson(file.readAll()).object().value(key).toString();
        if(!result.isEmpty())return result;
    }
    return code==QStringLiteral("es_CL")?QStringLiteral("Requiere desbloqueo"):
                                               QStringLiteral("Requires unlock");
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
    statusBar()->showMessage(QStringLiteral("Connect 1.0.1 · Quick libre · My protegido por Secrets"));
    QObject::connect(sidebarToggle_, &QPushButton::clicked, this, [this]() { toggleSidebar(); });
    QObject::connect(sidebarReveal_, &QPushButton::clicked, this, [this]() { toggleSidebar(); });
    QObject::connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    QObject::connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { syncNavigation(); });

    refreshSaved();
    showAdmin(kDashboard);
    QTimer::singleShot(0,this,[this](){refreshSecretsAccess();});
    auto* monitor=new QTimer(this);
    monitor->setInterval(1000);
    connect(monitor,&QTimer::timeout,this,[this](){refreshSecretsAccess();});
    monitor->start();
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
    if ((view == kCreate || view == kSaved) && !ensureVaultReady()) {
        view = kDashboard;
        statusBar()->showMessage(QStringLiteral("My Connect requiere Secrets desbloqueado"), 4000);
    }
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
    quickPassword_->setPlaceholderText(QStringLiteral("Opcional; SSH o VNC solicitarán la clave si falta"));
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
            action->setText(selection == 0 ? QStringLiteral("Conectar VNC temporal")
                                           : QStringLiteral("Conectar SSH temporal"));
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
    editPassword_ = new QLineEdit(page);
    editPassword_->setEchoMode(QLineEdit::Password);
    editPassword_->setPlaceholderText(QStringLiteral("Al editar, vacío conserva la clave guardada"));
    editProtocol_ = new QComboBox(page);
    editProtocol_->addItems({QStringLiteral("SSH"), QStringLiteral("VNC")});
    editProtocol_->setCurrentIndex(1);
    editSshPort_ = portBox(page, 22);
    editVncPort_ = portBox(page, 5900);
    form->addRow(QStringLiteral("Nombre del equipo"), editName_);
    form->addRow(QStringLiteral("Dirección"), editHost_);
    form->addRow(QStringLiteral("Usuario"), editSshUser_);
    form->addRow(QStringLiteral("Contraseña (cifrada en Secrets)"), editPassword_);
    form->addRow(QStringLiteral("Protocolo preferido"), editProtocol_);
    form->addRow(QStringLiteral("SSH · puerto"), editSshPort_);
    form->addRow(QStringLiteral("VNC · puerto"), editVncPort_);
    outer->addLayout(form);
    outer->addWidget(new QLabel(QStringLiteral(
        "Credenciales y equipos se guardan exclusivamente en la bóveda cifrada vault-connect.\n"
        "No se generan perfiles locales ni se copia la Master Key."), page));
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
    secretsState_=new QLabel(QStringLiteral("Secrets: checking"),page);
    secretsState_->setWordWrap(true);
    outer->addWidget(secretsState_);
    missingVault_=new QPushButton(secretsLabel(moduleRoot_,QStringLiteral("vault.requires_unlock")),page);
    missingVault_->setVisible(false);
    outer->addWidget(missingVault_,0,Qt::AlignLeft);
    connect(missingVault_,&QPushButton::clicked,this,[this](){requestVault();});
    auto* scroller = new QScrollArea(page);
    scroller->setWidgetResizable(true);
    auto* contents = new QWidget(scroller);
    savedList_ = new QVBoxLayout(contents);
    scroller->setWidget(contents);
    outer->addWidget(scroller, 1);
    adminStack_->addWidget(page);
}


void MainWindow::clearSensitiveEditor() {
    if (editPassword_) editPassword_->clear();
    editingId_.clear();
}

void MainWindow::closeSavedSessions() {
    if (!tabs_) return;
    // Only My-owned tabs. Quick has a separate quick: namespace and survives lock.
    for (int i = tabs_->count()-1; i >= 1; --i) {
        if (!tabs_->tabBar()->tabData(i).toString().startsWith(QStringLiteral("quick:"))) {
            QWidget* page = tabs_->widget(i);
            tabs_->removeTab(i);
            delete page; // VNC / SSH destructors close their owned sessions.
        }
    }
}

bool MainWindow::ensureVaultReady() {
    if (!vaultReady_) return false;
    const auto response = secretsRpc(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("vault.exists")},
        {QStringLiteral("module"), QStringLiteral("neebles-connect")}
    });
    const QJsonObject state = response.value(QStringLiteral("data")).toObject();
    if (!response.value(QStringLiteral("ok")).toBool() ||
        !state.value(QStringLiteral("exists")).toBool() ||
        state.value(QStringLiteral("locked")).toBool(true)) {
        // Defer UI/session teardown: this guard may be called from a pane callback.
        QTimer::singleShot(0,this,[this](){ refreshSecretsAccess(); });
        return false;
    }
    return true;
}

void MainWindow::refreshSecretsAccess() {
    if (vaultLoading_) return;
    const auto state=secretsRpc(QJsonObject{
        {QStringLiteral("op"),QStringLiteral("vault.exists")},
        {QStringLiteral("module"),QStringLiteral("neebles-connect")}
    });
    const bool online=state.value(QStringLiteral("ok")).toBool();
    const QJsonObject payload=state.value(QStringLiteral("data")).toObject();
    const int nextExists=online?(payload.value(QStringLiteral("exists")).toBool()?1:0):-1;
    const bool nextReady=nextExists==1&&!payload.value(QStringLiteral("locked")).toBool(true);
    const bool wasReady=vaultReady_;
    const int wasExists=vaultExists_;
    vaultExists_=nextExists;
    vaultReady_=nextReady;
    if (missingVault_) {
        missingVault_->setVisible(vaultExists_==0);
        missingVault_->setText(secretsLabel(moduleRoot_,QStringLiteral("vault.requires_unlock")));
    }
    if (secretsState_) {
        secretsState_->setText(vaultExists_<0?QStringLiteral("Secrets service unavailable"):
            vaultExists_==0?QStringLiteral("Vault not installed. Unlock Secrets to create it."):
            vaultReady_?QStringLiteral("Secrets unlocked · My Connections available"):
                        QStringLiteral("Secrets locked · My Connections disabled"));
    }
    for (int index : {kCreate, kSaved}) {
        if (index < adminButtons_.size()) adminButtons_[index]->setEnabled(vaultReady_);
    }
    if (!vaultReady_) {
        closeSavedSessions();
        profiles_.clear();
        clearSensitiveEditor();
        if (editName_) editName_->clear();
        if (editHost_) editHost_->clear();
        if (editSshUser_) editSshUser_->clear();
        if (adminStack_ && (adminStack_->currentIndex()==kCreate || adminStack_->currentIndex()==kSaved))
            showAdmin(kDashboard);
        if (savedList_ && (wasReady || wasExists != vaultExists_)) refreshSaved();
    } else if (!wasReady) {
        vaultLoading_=true;
        const bool loaded=loadProfiles();
        vaultLoading_=false;
        if (!loaded) {
            vaultReady_=false; // Fail closed on IPC/auth/KDBX errors.
            for (int index : {kCreate,kSaved}) if (index < adminButtons_.size())
                adminButtons_[index]->setEnabled(false);
            if (secretsState_) secretsState_->setText(QStringLiteral("Secrets vault read error · My Connections disabled"));
            closeSavedSessions();
            profiles_.clear();
            if (savedList_) refreshSaved();
            statusBar()->showMessage(QStringLiteral("Error al cargar My Connect desde Secrets"), 7000);
        } else if (savedList_) refreshSaved();
    }
}

bool MainWindow::readSecret(const QString& id, QJsonObject& value) {
    const auto response=secretsRpc(QJsonObject{
        {QStringLiteral("op"),QStringLiteral("secret.get")},
        {QStringLiteral("module"),QStringLiteral("neebles-connect")},
        {QStringLiteral("id"),id}
    });
    if (!response.value(QStringLiteral("ok")).toBool()) return false;
    const auto result=response.value(QStringLiteral("data")).toObject();
    if (result.value(QStringLiteral("id")).toString()!=id || !result.value(QStringLiteral("value")).isObject())return false;
    value=result.value(QStringLiteral("value")).toObject();
    return true;
}

void MainWindow::requestVault() {
    // The consumer asks, Secrets creates after successful unlock. Never send a password.
    const auto requested=secretsRpc(QJsonObject{
        {QStringLiteral("op"),QStringLiteral("vault.request")},
        {QStringLiteral("module"),QStringLiteral("neebles-connect")}
    });
    if(!requested.value(QStringLiteral("ok")).toBool()){
        QMessageBox::warning(this,QStringLiteral("NEEBLES Secrets"),
            QStringLiteral("The Secrets service is unavailable. Please start it first."));
        refreshSecretsAccess();
        return;
    }
    refreshSecretsAccess();
    if(vaultExists_==1)return; // Already unlocked and provisioned.
    // Ask the installed Boss CLI to Open Secrets; no direct Qt or backend bypass.
    const QString boss=QStandardPaths::findExecutable(QStringLiteral("neebles"));
    if(boss.isEmpty()||!QProcess::startDetached(boss,
       {QStringLiteral("modules"),QStringLiteral("action"),
        QStringLiteral("neebles-secrets"),QStringLiteral("open")})){
        QMessageBox::information(this,QStringLiteral("NEEBLES Secrets"),
            QStringLiteral("Vault request pending. Open NEEBLES Secrets from Boss Launcher or Tray to unlock."));
    }
}

void MainWindow::refreshSaved() {
    if (!savedList_) return;
    while (QLayoutItem* item = savedList_->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    if (!vaultReady_) {
        savedList_->addWidget(new QLabel(QStringLiteral("Secrets locked / unavailable: My Connections disabled.")));
        savedList_->addStretch();
        return;
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
        auto* ssh = new QPushButton(QStringLiteral("SSH"), row);
        auto* vnc = new QPushButton(QStringLiteral("VNC"), row);
        auto* edit = new QPushButton(QStringLiteral("Editar"), row);
        auto* remove = new QPushButton(QStringLiteral("Eliminar"), row);
        layout->addWidget(ssh); layout->addWidget(vnc);
        layout->addWidget(edit); layout->addWidget(remove);
        QObject::connect(ssh, &QPushButton::clicked, this, [this, profile]() {
            if (!ensureVaultReady()) return;
            QJsonObject value;
            if (!readSecret(profile.id,value) || !ensureVaultReady()) { refreshSecretsAccess(); return; }
            QString password=value.value(QStringLiteral("password")).toString();
            value.remove(QStringLiteral("password"));
            openProfile(profile,kSsh,password);
            password.fill(QChar('\0')); password.clear();
        });
        QObject::connect(vnc, &QPushButton::clicked, this, [this, profile]() {
            if (!ensureVaultReady()) return;
            QJsonObject value;
            if (!readSecret(profile.id,value) || !ensureVaultReady()) { refreshSecretsAccess(); return; }
            QString password=value.value(QStringLiteral("password")).toString();
            value.remove(QStringLiteral("password"));
            openProfile(profile,kVnc,password);
            password.fill(QChar('\0')); password.clear();
        });
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
    editProtocol_->setCurrentIndex(1); editPassword_->clear();
    saveButton_->setText(QStringLiteral("Guardar conexión"));
}

void MainWindow::editProfile(const ConnectProfile& profile) {
    if (isOpen(profile.id)) {
        QMessageBox::information(this, QStringLiteral("Sesión abierta"),
                                 QStringLiteral("Cierra la pestaña del equipo antes de editar su conexión."));
        return;
    }
    if (!ensureVaultReady()) return;
    editingId_ = profile.id;
    editName_->setText(profile.name); editHost_->setText(profile.host);
    editSshUser_->setText(profile.sshUser);
    editSshPort_->setValue(profile.sshPort); editVncPort_->setValue(profile.vncPort);
    editPassword_->clear(); editProtocol_->setCurrentIndex(profile.defaultProtocol);
    saveButton_->setText(QStringLiteral("Actualizar conexión"));
    showAdmin(kCreate);
}

void MainWindow::saveProfile() {
    if (!ensureVaultReady()) return;
    const QString name=editName_->text().trimmed();
    const QString host=editHost_->text().trimmed();
    const QString user=editSshUser_->text().trimmed();
    if (name.isEmpty() || !validHost(host) || user.isEmpty()) {
        QMessageBox::warning(this,QStringLiteral("Datos inválidos"),
            QStringLiteral("Nombre, host y usuario son obligatorios."));
        return;
    }
    const QString id=editingId_.isEmpty()?
        QStringLiteral("host-")+QUuid::createUuid().toString(QUuid::WithoutBraces):editingId_;
    QString password=editPassword_->text();
    editPassword_->clear();
    if (!editingId_.isEmpty() && password.isEmpty()) {
        QJsonObject old;
        if (!readSecret(id,old)) {
            QMessageBox::warning(this,QStringLiteral("Secrets"),QStringLiteral("No se pudo recuperar el registro actual."));
            return;
        }
        password=old.value(QStringLiteral("password")).toString();
        old.remove(QStringLiteral("password"));
    }
    if (!ensureVaultReady()) {password.fill(QChar('\0'));return;}
    QJsonObject value{
        {QStringLiteral("name"),name},{QStringLiteral("ip"),host},
        {QStringLiteral("user"),user},{QStringLiteral("password"),password},
        {QStringLiteral("ssh_port"),editSshPort_->value()},
        {QStringLiteral("vnc_port"),editVncPort_->value()},
        {QStringLiteral("protocol"),editProtocol_->currentIndex()==kSsh?QStringLiteral("ssh"):QStringLiteral("vnc")}
    };
    const QJsonObject response=secretsRpc(QJsonObject{
        {QStringLiteral("op"),editingId_.isEmpty()?QStringLiteral("secret.put"):QStringLiteral("secret.update")},
        {QStringLiteral("module"),QStringLiteral("neebles-connect")},
        {QStringLiteral("id"),id},{QStringLiteral("value"),value}
    });
    password.fill(QChar('\0'));password.clear();
    value.remove(QStringLiteral("password"));
    if (!response.value(QStringLiteral("ok")).toBool()) {
        refreshSecretsAccess();
        QMessageBox::warning(this,QStringLiteral("Secrets"),
            QStringLiteral("No se pudo escribir el registro cifrado. No se creó ningún perfil local."));
        return;
    }
    resetEditor();
    if (!loadProfiles()) {refreshSecretsAccess();return;}
    showAdmin(kSaved);
}

void MainWindow::deleteProfile(const ConnectProfile& profile) {
    if (!ensureVaultReady()) return;
    if (isOpen(profile.id)) {
        QMessageBox::information(this,QStringLiteral("Pestaña abierta"),
            QStringLiteral("Cierra la pestaña antes de eliminar este equipo."));
        return;
    }
    if (QMessageBox::question(this,QStringLiteral("Eliminar conexión"),
        QStringLiteral("¿Eliminar el equipo guardado «%1»?").arg(profile.name))!=QMessageBox::Yes) return;
    if (!ensureVaultReady()) return;
    const QJsonObject response=secretsRpc(QJsonObject{
        {QStringLiteral("op"),QStringLiteral("secret.delete")},
        {QStringLiteral("module"),QStringLiteral("neebles-connect")},
        {QStringLiteral("id"),profile.id}
    });
    if (!response.value(QStringLiteral("ok")).toBool()) {
        refreshSecretsAccess();
        QMessageBox::warning(this,QStringLiteral("Secrets"),QStringLiteral("No se pudo eliminar la entrada cifrada."));
        return;
    }
    if (!loadProfiles()) {refreshSecretsAccess();return;}
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
    password.fill(QChar('\0')); password.clear();
}

void MainWindow::openProfile(const ConnectProfile& profile, int protocol, const QString& password) {
    if (!profile.id.startsWith(QStringLiteral("quick:")) && !ensureVaultReady()) return;
    for (int i = 1; i < tabs_->count(); ++i) if (tabs_->tabBar()->tabData(i).toString() == profile.id) {
        tabs_->setCurrentIndex(i);
        if (protocol >= 0) {
            auto* sub = qobject_cast<QTabWidget*>(tabs_->widget(i));
            if (sub) {
                sub->setCurrentIndex(protocol);
                if (protocol == kVnc) {
                    QPointer<VncPane> pane(dynamic_cast<VncPane*>(sub->widget(kVnc)));
                    QString secret=password;
                    QTimer::singleShot(0,sub,[pane,secret=std::move(secret)]() mutable {
                        if (pane) pane->connectNow(secret);
                        secret.fill(QChar('\0'));
                    });
                } else if (protocol == kSsh) {
                    QPointer<SshPane> pane(dynamic_cast<SshPane*>(sub->widget(kSsh)));
                    QString secret=password;
                    QTimer::singleShot(0,sub,[pane,secret=std::move(secret)]() mutable {
                        if (pane) pane->connectNow(secret);
                        secret.fill(QChar('\0'));
                    });
                }
            }
        }
        return;
    }
    auto* sub = new QTabWidget(tabs_);
    sub->setDocumentMode(true);
    auto* ssh = new SshPane(profile.host, profile.sshPort, profile.sshUser, sub);
    sub->addTab(ssh, QStringLiteral("SSH"));
    auto* vnc = new VncPane(profile.host, profile.vncPort, sub);
    sub->addTab(vnc, QStringLiteral("VNC"));
    if (!profile.id.startsWith(QStringLiteral("quick:"))) {
        ssh->setAccessGuard([this]() { return ensureVaultReady(); });
        vnc->setAccessGuard([this]() { return ensureVaultReady(); });
        const QString id=profile.id;
        auto fromVault=[this,id](QString& password)->bool {
            if (!ensureVaultReady()) return false;
            QJsonObject record;
            if (!readSecret(id,record)) return false;
            password=record.value(QStringLiteral("password")).toString();
            record.remove(QStringLiteral("password"));
            if (!ensureVaultReady()) {
                password.fill(QChar('\0')); password.clear();
                return false;
            }
            return true;
        };
        ssh->setCredentialProvider(fromVault);
        vnc->setCredentialProvider(fromVault);
    }
    sub->setCurrentIndex(protocol >= 0 ? protocol : profile.defaultProtocol);
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
    // Only the authorized installed Qt binary can read these entries from Secrets.
    if (!vaultReady_) return false;
    const QJsonObject list=secretsRpc(QJsonObject{
        {QStringLiteral("op"),QStringLiteral("vault.list")},
        {QStringLiteral("module"),QStringLiteral("neebles-connect")}
    });
    if (!list.value(QStringLiteral("ok")).toBool()) return false;
    const QJsonValue ids=list.value(QStringLiteral("data")).toObject().value(QStringLiteral("ids"));
    if (!ids.isArray() || ids.toArray().size()>256) return false;
    QVector<ConnectProfile> loaded;
    for (const QJsonValue& entry : ids.toArray()) {
        if (!entry.isString()) return false;
        const QString id=entry.toString();
        QJsonObject record;
        if (!readSecret(id,record)) return false;
        ConnectProfile profile;
        profile.id=id;
        profile.name=record.value(QStringLiteral("name")).toString();
        profile.host=record.value(QStringLiteral("ip")).toString();
        profile.sshUser=record.value(QStringLiteral("user")).toString();
        profile.sshPort=record.value(QStringLiteral("ssh_port")).toInt(22);
        profile.vncPort=record.value(QStringLiteral("vnc_port")).toInt(5900);
        profile.defaultProtocol=record.value(QStringLiteral("protocol")).toString()==QStringLiteral("ssh")?kSsh:kVnc;
        record.remove(QStringLiteral("password")); // Credentials never enter cached profiles.
        if (profile.id.isEmpty() || profile.name.isEmpty() || !validHost(profile.host) ||
            profile.sshUser.isEmpty() || profile.sshPort<1 || profile.sshPort>65535 ||
            profile.vncPort<1 || profile.vncPort>65535) return false;
        for (const auto& prior:loaded) if (prior.id==profile.id) return false;
        loaded.append(profile);
    }
    if (!ensureVaultReady()) return false;
    profiles_=std::move(loaded); // Replace entire in-memory metadata snapshot.
    return true;
}
