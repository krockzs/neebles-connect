// NEEBLES Connect — My Connect uses exclusively Secrets structured KDBX records.
#include "MainWindow.h"
#include "ConnectStructures.h"
#include "VncPane.h"
#include "SshPane.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QThread>
#include <QThreadPool>
#include <QColor>
#include <QColorDialog>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QSet>
#include <QIcon>
#include <QJsonParseError>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
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
#include <QShortcut>
#include <QKeySequence>
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
#include <algorithm>

namespace {
constexpr int kDashboard = 0;
constexpr int kQuick = 1;
constexpr int kCreate = 2;
constexpr int kSaved = 3;
constexpr int kSsh = 0;
constexpr int kVnc = 1;
constexpr int kVsCode = 2;
constexpr int kSunshine = 3;

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
QJsonObject secretsRpc(const QJsonObject& message, int timeoutMs = 20000) {
    // A single monotonic deadline across the entire request prevents a peer
    // which drip-feeds bytes from keeping a Qt caller waiting indefinitely.
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) return {};
    QElapsedTimer elapsed;
    elapsed.start();
    const int deadlineMs = qBound(400, timeoutMs, 20000);
    auto remaining = [&]() -> int {
        return qMax(0, deadlineMs - int(elapsed.elapsed()));
    };
    QLocalSocket socket;
    socket.connectToServer(QDir(runtime).filePath(QStringLiteral("neebles-secrets/secrets.sock")));
    if (!socket.waitForConnected(qMin(650, remaining()))) return {};
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > 16384) return {};
    const quint32 n = quint32(body.size());
    const char prefix[4] = {char((n >> 24) & 255), char((n >> 16) & 255),
                            char((n >> 8) & 255), char(n & 255)};
    if (socket.write(prefix, 4) != 4 || socket.write(body) != body.size()) return {};
    if (remaining() == 0 || !socket.waitForBytesWritten(qMin(1500, remaining()))) return {};
    auto readExact = [&](int len) -> QByteArray {
        QByteArray out;
        while (out.size() < len && remaining() > 0) {
            const QByteArray part = socket.read(len - out.size());
            if (!part.isEmpty()) {
                out.append(part);
                continue;
            }
            if (socket.state() != QLocalSocket::ConnectedState ||
                !socket.waitForReadyRead(remaining())) return {};
        }
        return out.size() == len ? out : QByteArray{};
    };
    const QByteArray header = readExact(4);
    if (header.size() != 4) return {};
    const quint32 size = (quint8(header[0]) << 24) | (quint8(header[1]) << 16) |
                         (quint8(header[2]) << 8) | quint8(header[3]);
    if (size == 0 || size > 16384) return {};
    const QByteArray response = readExact(int(size));
    if (response.size() != int(size)) return {};
    const auto doc = QJsonDocument::fromJson(response);
    return doc.isObject() ? doc.object() : QJsonObject{};
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
        QPushButton#adminNavButton { padding: 0; border-radius: 5px; }
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

    auto* sideFrame = new QFrame(root);
    sideFrame->setObjectName(QStringLiteral("sidebar"));
    sidebar_ = sideFrame;
    sidebar_->setFixedWidth(235);
    auto* sideLayout = new QVBoxLayout(sidebar_);
    sidebarLayout_ = sideLayout;
    auto* topBar = new QHBoxLayout();
    // Misma fotografía oficial que usa el splash; miniatura en cabecera.
    auto* brandLogo = new QLabel(sidebar_);
    brandLogo->setObjectName(QStringLiteral("sidebarBrandLogo"));
    brandLogo_ = brandLogo;
    brandLogo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QPixmap brandPhoto(QDir(moduleRoot_).filePath("assets/neebles-connect-logo.png"));
    if (brandPhoto.isNull())
        brandPhoto.load(QDir(moduleRoot_).filePath("assets/neebles-connect-icon.png"));
    if (!brandPhoto.isNull())
        brandLogo->setPixmap(brandPhoto.scaled(QSize(125, 76), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    brandLogo->setMinimumHeight(76);
    topBar->addWidget(brandLogo, 1);
    sideLayout->addLayout(topBar);
    sideLayout->addSpacing(12);
    sidebarContent_ = new QWidget(sidebar_);
    auto* choices = new QVBoxLayout(sidebarContent_);
    choices->setContentsMargins(0, 0, 0, 0);
    for (const auto& action : QVector<QPair<QString,int>>{
             {QStringLiteral("Dashboard"), kDashboard},
             {QStringLiteral("Quick Connect"), kQuick},
             {QStringLiteral("My Connect"), kSaved}}) {
        auto* button = new QPushButton(action.first, sidebarContent_);
        button->setObjectName(QStringLiteral("adminNavButton"));
        button->setCheckable(true);
        button->setProperty("adminView", action.second);
        button->setProperty("navLabel", action.first);
        button->setIcon(style()->standardIcon(action.second == kDashboard ? QStyle::SP_DesktopIcon :
                                            action.second == kQuick ? QStyle::SP_ArrowForward :
                                                                      QStyle::SP_DirHomeIcon));
        adminButtons_.append(button);
        choices->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, this,
                         [this, view=action.second]() { if (view == kCreate) resetEditor(); showAdmin(view); });
    }
    sideLayout->addWidget(sidebarContent_);
    sideLayout->addStretch();
    auto* edition = new QLabel(QStringLiteral("Connect v%1").arg(version_), sidebar_);
    edition->setStyleSheet(QStringLiteral("color: #A78BFA;"));
    sideLayout->addWidget(edition);
    sidebarEdition_ = edition;
    auto* contextual = new QWidget(sidebar_);
    auto* tools = new QVBoxLayout(contextual);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(1);
    monitorTools_ = new QWidget(contextual);
    monitorLayout_ = new QVBoxLayout(monitorTools_);
    monitorLayout_->setContentsMargins(0,0,0,0);
    monitorLayout_->setSpacing(1);
    tools->addWidget(monitorTools_);
    copyTool_ = new QToolButton(contextual);
    copyTool_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    copyTool_->setToolTip(QStringLiteral("Copiar desde la conexión seleccionada"));
    copyTool_->setAutoRaise(true);
    copyTool_->setFixedSize(28,28);
    tools->addWidget(copyTool_);
    stopTool_ = new QToolButton(contextual);
    stopTool_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    stopTool_->setToolTip(QStringLiteral("Detener únicamente la conexión seleccionada"));
    stopTool_->setAutoRaise(true);
    stopTool_->setFixedSize(28,28);
    tools->addWidget(stopTool_);
    sideLayout->insertWidget(sideLayout->count()-2, contextual);
    contextual->setObjectName(QStringLiteral("connectionTools"));
    contextual->hide();
    QObject::connect(stopTool_, &QToolButton::clicked, this, [this]() {
        QWidget* pane = nullptr; int protocol = -1;
        if (!activeConnection(&pane, &protocol)) return;
        if (protocol == kSsh) static_cast<SshPane*>(pane)->disconnectNow();
        else if (protocol == kVnc) static_cast<VncPane*>(pane)->disconnectNow();
        refreshVisualState();
    });
    QObject::connect(copyTool_, &QToolButton::clicked, this, [this]() {
        QWidget* pane = nullptr;
        int protocol = -1;
        if (!activeConnection(&pane, &protocol)) return;
        const bool copied = protocol == kSsh ? static_cast<SshPane*>(pane)->copySelectedText() :
                            protocol == kVnc ? static_cast<VncPane*>(pane)->copySelectedMonitor() : false;
        statusBar()->showMessage(copied ? QStringLiteral("Copiado al portapapeles") :
            QStringLiteral("No hay imagen/texto válido para copiar"), 3500);
    });
    horizontal->addWidget(sidebar_);

    tabs_ = new QTabWidget(root);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
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
    QObject::connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });
    QObject::connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        syncNavigation(); refreshVisualState();
    });
    QObject::connect(tabs_->tabBar(), &QTabBar::tabMoved, this, [this](int, int) {
        if (syncingTabs_) return;
        QSignalBlocker guard(tabs_->tabBar());
        const int fixed = [&]() {
            for (int i = 0; i < tabs_->count(); ++i)
                if (tabs_->tabBar()->tabData(i).toString() == QStringLiteral("fixed:admin")) return i;
            return -1;
        }();
        if (fixed > 0) tabs_->tabBar()->moveTab(fixed, 0);
        tabOrder_.clear();
        for (int i = 1; i < tabs_->count(); ++i) {
            const QString id = tabs_->tabBar()->tabData(i).toString();
            if (!id.startsWith(QStringLiteral("quick:"))) tabOrder_.append(id);
        }
    });
    auto* visualTimer = new QTimer(this);
    visualTimer->setInterval(400);
    QObject::connect(visualTimer, &QTimer::timeout, this, [this]() { refreshVisualState(); });
    visualTimer->start();

    refreshSaved();
    showAdmin(kDashboard);
    QTimer::singleShot(0,this,[this](){refreshSecretsAccess();});
    auto* monitor=new QTimer(this);
    monitor->setInterval(1000);
    connect(monitor,&QTimer::timeout,this,[this](){refreshSecretsAccess();});
    monitor->start();
}

void MainWindow::syncNavigation() {
    const int active = tabs_->currentIndex() == 0 ? adminStack_->currentIndex() : -1;
    for (QPushButton* button : adminButtons_)
        button->setChecked(button->property("adminView").toInt() == active);
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
        QStringLiteral("Crear equipo"), QStringLiteral("My Connect")
    };
    tabs_->setTabText(0, titles.at(view));
    tabs_->setCurrentIndex(0);
    syncNavigation();
    refreshVisualState();
    if (view == kSaved) refreshSaved();
}

void MainWindow::buildDashboard() {
    auto* page = new QWidget(adminStack_);
    auto* outer = new QVBoxLayout(page);
    outer->addWidget(header(QStringLiteral("Dashboard · Conexiones"), page));
    outer->addWidget(new QLabel(QStringLiteral(
        "Equipos de My Connect, grupos personalizados y estado real de SSH/VNC."), page));
    auto* addGroup = new QPushButton(QStringLiteral("+ Separador personalizado"), page);
    outer->addWidget(addGroup, 0, Qt::AlignLeft);
    QObject::connect(addGroup, &QPushButton::clicked, this, [this]() { addDashboardGroup(); });
    auto* scroller = new QScrollArea(page);
    scroller->setWidgetResizable(true);
    auto* content = new QWidget(scroller);
    dashboardList_ = new QVBoxLayout(content);
    scroller->setWidget(content);
    outer->addWidget(scroller, 1);
    adminStack_->addWidget(page);
    renderDashboard();
}

void MainWindow::saveQuickDraft() {
    // Per-protocol transient fields only; never persisted in Settings or Secrets.
    if (quickSelectedProtocol_ < 0 || quickSelectedProtocol_ >= 4) return;
    auto& draft = quickDrafts_[quickSelectedProtocol_];
    draft.user = quickUser_->text();
    draft.password = quickPassword_->text();
    draft.instanceName = quickInstanceName_->text();
    draft.port = quickPort_->value();
}

void MainWindow::selectQuickProtocol(int selection) {
    if (selection < 0 || selection >= 4) return;
    saveQuickDraft();
    quickSelectedProtocol_ = selection;
    const auto& draft = quickDrafts_[selection];
    quickUser_->setText(draft.user);
    quickPassword_->setText(draft.password);
    quickInstanceName_->setText(draft.instanceName);
    quickPort_->setValue(draft.port);
    const bool future = selection >= 2;
    const bool vsCode = selection == 2;
    quickInstanceName_->setVisible(vsCode);
    if (auto* fieldLabel = quickForm_->labelForField(quickInstanceName_))
        fieldLabel->setVisible(vsCode);
    quickUser_->setPlaceholderText(selection == 1
        ? QStringLiteral("Usuario SSH obligatorio")
        : QStringLiteral("Usuario propio de este protocolo (opcional)"));
    quickPort_->setSpecialValueText(future ? QStringLiteral("Sin definir") : QString());
    static const QString actions[] = {
        QStringLiteral("Conectar VNC temporal"),
        QStringLiteral("Conectar SSH temporal"),
        QStringLiteral("Abrir pestaña VS Code"),
        QStringLiteral("Abrir pestaña Sunshine / Moonlight")
    };
    quickAction_->setText(actions[selection]);
}

void MainWindow::buildQuick() {
    auto* page = new QWidget(adminStack_);
    auto* outer = new QVBoxLayout(page);
    outer->addWidget(header(QStringLiteral("Quick Connect"), page));
    outer->addWidget(new QLabel(QStringLiteral("Conexión temporal, sin guardar credenciales ni equipos."), page));
    quickForm_ = new QFormLayout();
    quickName_ = new QLineEdit(page);
    quickName_->setPlaceholderText(QStringLiteral("Nombre del equipo (obligatorio)"));
    quickHost_ = new QLineEdit(page);
    quickHost_->setPlaceholderText(QStringLiteral("IP LAN / Tailscale / hostname"));
    quickProtocol_ = new QComboBox(page);
    // Preserve the current VNC/SSH selector and add the future protocols.
    quickProtocol_->addItems({QStringLiteral("VNC"), QStringLiteral("SSH"),
                              QStringLiteral("VS Code"), QStringLiteral("Sunshine / Moonlight")});
    quickUser_ = new QLineEdit(page);
    quickPassword_ = new QLineEdit(page);
    quickPassword_->setEchoMode(QLineEdit::Password);
    quickPassword_->setPlaceholderText(QStringLiteral("Clave independiente, solo en memoria"));
    quickInstanceName_ = new QLineEdit(page);
    quickInstanceName_->setPlaceholderText(QStringLiteral("Nombre de esta instancia VS Code"));
    quickPort_ = portBox(page, 5900);
    quickPort_->setRange(0, 65535);
    quickDrafts_[0].port = 5900;
    quickDrafts_[1].port = 22;
    quickDrafts_[2].port = 0;
    quickDrafts_[3].port = 0; // No inventar un puerto Sunshine antes de implementar el motor.
    quickForm_->addRow(QStringLiteral("Nombre del equipo"), quickName_);
    quickForm_->addRow(QStringLiteral("IP / Hostname"), quickHost_);
    quickForm_->addRow(QStringLiteral("Protocolo"), quickProtocol_);
    quickForm_->addRow(QStringLiteral("Usuario"), quickUser_);
    quickForm_->addRow(QStringLiteral("Contraseña / credencial"), quickPassword_);
    quickForm_->addRow(QStringLiteral("Nombre instancia"), quickInstanceName_);
    quickForm_->addRow(QStringLiteral("Puerto"), quickPort_);
    outer->addLayout(quickForm_);
    quickAction_ = new QPushButton(page);
    quickAction_->setFixedWidth(315);
    outer->addWidget(quickAction_, 0, Qt::AlignHCenter);
    outer->addStretch();
    QObject::connect(quickProtocol_, &QComboBox::currentIndexChanged, this,
                     [this](int selection) { selectQuickProtocol(selection); });
    QObject::connect(quickAction_, &QPushButton::clicked, this, [this]() { openQuick(); });
    // Enter in fields runs precisely the same validation/action as clicking the button.
    for (auto* field : {quickName_, quickHost_, quickUser_, quickPassword_, quickInstanceName_}) {
        QObject::connect(field, &QLineEdit::returnPressed, quickAction_, &QPushButton::click);
    }
    for (QWidget* widget : {static_cast<QWidget*>(quickPort_), static_cast<QWidget*>(quickProtocol_)}) {
        for (int key : {int(Qt::Key_Return), int(Qt::Key_Enter)}) {
            auto* shortcut = new QShortcut(QKeySequence(key), widget);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            QObject::connect(shortcut, &QShortcut::activated, quickAction_, &QPushButton::click);
        }
    }
    selectQuickProtocol(0);
    adminStack_->addWidget(page);
}

void MainWindow::buildEditor() {
    auto* page = new QWidget(adminStack_);
    auto* outer = new QVBoxLayout(page);
    outer->addWidget(header(QStringLiteral("Crear equipo"), page));
    outer->addWidget(new QLabel(QStringLiteral(
        "Primero registra nombre e IP/hostname. SSH, VNC, VS Code y Sunshine "
        "empiezan vacíos; después podrás añadirlos individualmente."), page));
    auto* form = new QFormLayout();
    editName_ = new QLineEdit(page);
    editHost_ = new QLineEdit(page);
    editHost_->setPlaceholderText(QStringLiteral("IP LAN / Tailscale / hostname"));
    editSshUser_ = new QLineEdit(page); // User is optional general equipment metadata.
    form->addRow(QStringLiteral("Nombre del equipo"), editName_);
    form->addRow(QStringLiteral("IP / Hostname"), editHost_);
    form->addRow(QStringLiteral("Usuario general (opcional)"), editSshUser_);
    editGroup_ = new QComboBox(page);
    form->addRow(QStringLiteral("Grupo (opcional)"), editGroup_);
    outer->addLayout(form);
    saveButton_ = new QPushButton(QStringLiteral("Guardar equipo"), page);
    outer->addWidget(saveButton_);
    auto* cancel = new QPushButton(QStringLiteral("Cancelar"), page);
    outer->addWidget(cancel);
    outer->addStretch();
    QObject::connect(saveButton_, &QPushButton::clicked, this, [this]() { saveProfile(); });
    for (auto* field : {editName_, editHost_, editSshUser_})
        QObject::connect(field, &QLineEdit::returnPressed, saveButton_, &QPushButton::click);
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
    auto* create = new QPushButton(QStringLiteral("+ Crear equipo"), page);
    outer->addWidget(create, 0, Qt::AlignLeft);
    QObject::connect(create, &QPushButton::clicked, this, [this]() {
        resetEditor(); showAdmin(kCreate);
    });
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
    editingId_.clear();
    if (editName_) editName_->clear();
    if (editHost_) editHost_->clear();
    if (editSshUser_) editSshUser_->clear();
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
    // Q07D1: avoid any synchronous IPC on Qt's GUI thread. The Secrets
    // structure.* broker still authoritatively verifies lock/authorization.
    // A stale unlocked cache can never bypass the broker's authorization.
    return vaultReady_ && !vaultLoading_;
}

void MainWindow::refreshSecretsAccess() {
    // Only the status probe runs in a thread-pool worker. It never touches
    // QWidget, KDBX records or Qt UI state. Coalescing avoids RPC pile-ups.
    if (vaultLoading_ || secretsPollInFlight_) return;
    secretsPollInFlight_ = true;
    const QPointer<MainWindow> receiver(this);
    QThreadPool::globalInstance()->start([receiver]() {
        const auto state = secretsRpc(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("vault.exists")},
            {QStringLiteral("module"), QStringLiteral("neebles-connect")}
        }, 2500);
        // Schedule on QApplication rather than the mutable MainWindow receiver;
        // inspect QPointer only on the main thread after dispatch.
        if (auto* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, [receiver,state]() {
                if (!receiver) return;
                receiver->secretsPollInFlight_ = false;
                receiver->applySecretsStatus(state);
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::applySecretsStatus(const QJsonObject& state) {
    if (vaultLoading_) return;
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
    for (QPushButton* button : adminButtons_)
        if (button->property("adminView").toInt() == kSaved)
            button->setEnabled(vaultReady_);
    if (!vaultReady_ && (wasReady || wasExists != vaultExists_)) {
        closeSavedSessions();
        profiles_.clear();
        vsCodeCounts_.clear();
        vsCodeTabs_.clear();
        ++vaultEpoch_;
        clearSensitiveEditor();
        if (adminStack_ && (adminStack_->currentIndex()==kCreate || adminStack_->currentIndex()==kSaved))
            showAdmin(kDashboard);
        dashboardRows_ = QJsonArray{};
        dashboardLayoutLoaded_ = false;
        dashboardStateSnapshot_.clear();
        renderDashboard();
        if (savedList_ && (wasReady || wasExists != vaultExists_)) refreshSaved();
    } else if (!wasReady) {
        refreshVaultDataAsync();
    }
}

// Hydrate the protected My Connect list in a worker; never call Store from
// QWidget code while applying an unlock status. The worker sends only the
// whitelisted, non-secret fields to the GUI (no *-pass fields).
void MainWindow::refreshVaultDataAsync() {
    if (!vaultReady_ || vaultLoading_) return;
    vaultLoading_ = true;
    const QPointer<MainWindow> receiver(this);
    QThreadPool::globalInstance()->start([receiver]() {
        QJsonObject snapshot;
        QString error;
        const ConnectStructures::Store store([](const QJsonObject& request) {
            return secretsRpc(request, 8000);
        });
        QVector<ConnectStructures::Record> equipment;
        QVector<ConnectStructures::Record> vscode;
        ConnectStructures::Record layout;
        bool layoutMissing = false;
        bool ok = store.ensureSchemas(&error) &&
                  store.list(QStringLiteral("myconnect"), equipment, &error) &&
                  store.list(QStringLiteral("vscode"), vscode, &error);
        if (ok) {
            ok = store.get(QStringLiteral("dashboard"), QStringLiteral("primary"), layout, &error);
            if (!ok && error == QStringLiteral("entity_not_found")) {
                layoutMissing = true;
                error.clear();
                ok = true;
            }
        }
        if (ok) {
            QJsonArray hosts;
            for (const auto& entry : equipment) {
                const auto& v = entry.value;
                const QString host = v.value(QStringLiteral("ip")).toString();
                const QString name = v.value(QStringLiteral("name")).toString();
                if (entry.id.isEmpty() || name.isEmpty() || !validHost(host)) {
                    ok = false;
                    error = QStringLiteral("invalid_equipment");
                    break;
                }
                hosts.append(QJsonObject{
                    {QStringLiteral("id"), entry.id},
                    {QStringLiteral("name"), name},
                    {QStringLiteral("host"), host},
                    {QStringLiteral("generalUser"), v.value(QStringLiteral("user")).toString()},
                    {QStringLiteral("sshUser"), v.value(QStringLiteral("ssh-user")).toString()},
                    {QStringLiteral("hasSsh"), v.contains(QStringLiteral("ssh-name"))},
                    {QStringLiteral("hasVnc"), v.contains(QStringLiteral("vnc-name"))},
                    {QStringLiteral("hasSunshine"), v.contains(QStringLiteral("sunshine-name"))},
                    {QStringLiteral("sshPort"), v.value(QStringLiteral("ssh-port")).toInt(22)},
                    {QStringLiteral("vncPort"), v.value(QStringLiteral("vnc-port")).toInt(5900)}
                });
            }
            QJsonObject counts;
            QJsonArray instances;
            for (const auto& entry : vscode) {
                const QString owner = entry.value.value(QStringLiteral("equipment")).toString();
                const QString name = entry.value.value(QStringLiteral("name")).toString();
                if (owner.isEmpty() || entry.id.isEmpty() || name.isEmpty()) {
                    ok = false; error = QStringLiteral("invalid_vscode"); break;
                }
                counts.insert(owner, counts.value(owner).toInt() + 1);
                // Never include the pass field in a GUI-visible snapshot.
                instances.append(QJsonObject{{QStringLiteral("equipment"), owner},
                    {QStringLiteral("id"), entry.id}, {QStringLiteral("name"), name},
                    {QStringLiteral("port"), entry.value.value(QStringLiteral("port"))}});
            }
            if (ok) {
                snapshot.insert(QStringLiteral("hosts"), hosts);
                snapshot.insert(QStringLiteral("counts"), counts);
                snapshot.insert(QStringLiteral("instances"), instances);
                snapshot.insert(QStringLiteral("layoutMissing"), layoutMissing);
                if (!layoutMissing) snapshot.insert(QStringLiteral("layout"),
                       layout.value.value(QStringLiteral("layout")).toString());
            }
        }
        // Secrets may have locked while records were being read. Read its
        // current state before allowing anything to become visible.
        if (ok) {
            const QJsonObject state = secretsRpc(QJsonObject{
                {QStringLiteral("op"), QStringLiteral("vault.exists")},
                {QStringLiteral("module"), QStringLiteral("neebles-connect")}
            }, 2500);
            const QJsonObject data = state.value(QStringLiteral("data")).toObject();
            if (!state.value(QStringLiteral("ok")).toBool() ||
                !data.value(QStringLiteral("exists")).toBool() ||
                data.value(QStringLiteral("locked")).toBool(true)) {
                ok = false;
                error = QStringLiteral("vault_locked_during_load");
            }
        }
        if (auto* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, [receiver,ok,snapshot,error]() {
                if (!receiver) return;
                receiver->vaultLoading_ = false;
                if (!receiver->vaultReady_) return;
                if (!ok || !receiver->applyVaultSnapshot(snapshot)) {
                    receiver->vaultReady_ = false;
                    receiver->closeSavedSessions();
                    receiver->profiles_.clear();
                    receiver->vsCodeCounts_.clear();
                    receiver->vsCodeTabs_.clear();
                    ++receiver->vaultEpoch_;
                    receiver->dashboardRows_ = {};
                    receiver->dashboardLayoutLoaded_ = false;
                    for (auto* button : receiver->adminButtons_)
                        if (button->property("adminView").toInt() == kSaved)
                            button->setEnabled(false);
                    if (receiver->secretsState_)
                        receiver->secretsState_->setText(
                            QStringLiteral("Secrets vault read error · My Connections disabled"));
                    receiver->refreshSaved();
                    receiver->renderDashboard();
                    receiver->statusBar()->showMessage(
                        QStringLiteral("My Connect: error al cargar Secrets (%1)").arg(error), 7000);
                    return;
                }
                receiver->syncSavedTabs();
                receiver->renderDashboard();
                receiver->refreshSaved();
            }, Qt::QueuedConnection);
        }
    });
}

bool MainWindow::applyVaultSnapshot(const QJsonObject& snapshot) {
    const QJsonArray hosts = snapshot.value(QStringLiteral("hosts")).toArray();
    const QJsonObject counts = snapshot.value(QStringLiteral("counts")).toObject();
    if (hosts.size() > 2048) return false;
    QVector<ConnectProfile> loaded;
    for (const auto& item : hosts) {
        if (!item.isObject()) return false;
        const auto v = item.toObject();
        ConnectProfile profile;
        profile.id = v.value(QStringLiteral("id")).toString();
        profile.name = v.value(QStringLiteral("name")).toString();
        profile.host = v.value(QStringLiteral("host")).toString();
        profile.generalUser = v.value(QStringLiteral("generalUser")).toString();
        profile.sshUser = v.value(QStringLiteral("sshUser")).toString();
        profile.hasSsh = v.value(QStringLiteral("hasSsh")).toBool();
        profile.hasVnc = v.value(QStringLiteral("hasVnc")).toBool();
        profile.hasSunshine = v.value(QStringLiteral("hasSunshine")).toBool();
        profile.sshPort = v.value(QStringLiteral("sshPort")).toInt(22);
        profile.vncPort = v.value(QStringLiteral("vncPort")).toInt(5900);
        if (profile.id.isEmpty() || profile.name.isEmpty() || !validHost(profile.host)) return false;
        loaded.push_back(std::move(profile));
    }
    QHash<QString,int> newCounts;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        if (it.key().isEmpty() || !it.value().isDouble() || it.value().toInt() < 0) return false;
        newCounts.insert(it.key(), it.value().toInt());
    }
    QHash<QString,QVector<QPair<QString,QString>>> tabs;
    const QJsonArray instances = snapshot.value(QStringLiteral("instances")).toArray();
    if (instances.size() > 2048) return false;
    for (const QJsonValue& element : instances) {
        if (!element.isObject()) return false;
        const auto v = element.toObject();
        const QString owner = v.value(QStringLiteral("equipment")).toString();
        const QString id = v.value(QStringLiteral("id")).toString();
        const QString name = v.value(QStringLiteral("name")).toString();
        if (owner.isEmpty() || id.isEmpty() || name.isEmpty()) return false;
        tabs[owner].append(qMakePair(id, name));
    }
    QJsonArray layoutRows;
    if (snapshot.value(QStringLiteral("layoutMissing")).toBool()) {
        for (const auto& p : loaded) layoutRows.append(QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("host")},
            {QStringLiteral("id"), p.id},
            {QStringLiteral("group"), QString()}
        });
    } else {
        QJsonParseError parseError;
        const QByteArray raw = snapshot.value(QStringLiteral("layout")).toString().toUtf8();
        const auto doc = QJsonDocument::fromJson(raw, &parseError);
        if (raw.size() > 7900 || parseError.error != QJsonParseError::NoError || !doc.isArray())
            return false;
        layoutRows = doc.array();
        if (layoutRows.size() > 2048) return false;
        QSet<QString> groups, equipments;
        for (const QJsonValue& item : layoutRows) {
            if (!item.isObject()) return false;
            const QJsonObject row = item.toObject();
            const QString id = row.value(QStringLiteral("id")).toString();
            const QString kind = row.value(QStringLiteral("kind")).toString();
            if (id.isEmpty()) return false;
            if (kind == QStringLiteral("group")) {
                const QString name = row.value(QStringLiteral("name")).toString();
                if (groups.contains(id) || name.isEmpty() || name.size() > 100 ||
                    !QColor(row.value(QStringLiteral("color")).toString()).isValid()) return false;
                groups.insert(id);
            } else if (kind == QStringLiteral("host")) {
                if (equipments.contains(id)) return false;
                equipments.insert(id);
            } else return false;
        }
        for (const auto& p : loaded) if (!equipments.contains(p.id)) {
            layoutRows.append(QJsonObject{
                {QStringLiteral("kind"), QStringLiteral("host")},
                {QStringLiteral("id"), p.id},
                {QStringLiteral("group"), QString()}
            });
        }
    }
    profiles_ = std::move(loaded);
    vsCodeCounts_ = std::move(newCounts);
    vsCodeTabs_ = std::move(tabs);
    dashboardRows_ = std::move(layoutRows);
    dashboardCommittedRows_ = dashboardRows_;
    dashboardLayoutLoaded_ = true;
    return true;
}

// Direct access to old secret.get/put is deliberately removed from My Connect.
// All persisted records now flow through the authenticated structure.* broker.

// Q07D2A: at most one interactive KDBX transaction at a time. Never repeat
// a mutating RPC after timeout or ambiguous completion; CAS revision is owned
// by Secrets. All Qt widgets and status messages remain on the GUI thread.
bool MainWindow::runStructure(QString description, std::function<QJsonObject()> operation,
                              std::function<void(const QJsonObject&)> onFinish) {
    if (!ensureVaultReady()) return false;
    if (structureInFlight_ || vaultLoading_) {
        statusBar()->showMessage(QStringLiteral("Secrets ocupado: termina la operación anterior"), 5000);
        return false;
    }
    structureInFlight_ = true;
    const quint64 epoch = vaultEpoch_;
    const QPointer<MainWindow> receiver(this);
    statusBar()->showMessage(description);
    QThreadPool::globalInstance()->start([receiver, epoch, operation=std::move(operation),
                                          onFinish=std::move(onFinish)]() mutable {
        const QJsonObject answer = operation();
        if (auto* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, [receiver, epoch, answer,
                                            onFinish=std::move(onFinish)]() mutable {
                if (!receiver) return;
                receiver->structureInFlight_ = false;
                if (!receiver->vaultReady_ || receiver->vaultEpoch_ != epoch) {
                    receiver->statusBar()->showMessage(
                        QStringLiteral("Operación Secrets finalizada con estado de bóveda cambiado: revisar"),7000);
                    return;
                }
                onFinish(answer);
            }, Qt::QueuedConnection);
        }
    });
    return true;
}

// Strictly bounded dashboard-layout writes. Called ONLY from the worker.
static QJsonObject writeDashboardInWorker(const QJsonArray& rows) {
    const QByteArray packed = QJsonDocument(rows).toJson(QJsonDocument::Compact);
    if (packed.size() > 7900) return QJsonObject{{QStringLiteral("error"),QStringLiteral("dashboard_too_large")}};
    const ConnectStructures::Store store([](const QJsonObject& request) {
        return secretsRpc(request, 10000);
    });
    ConnectStructures::Record original, result;
    QString error;
    const QJsonObject value{{QStringLiteral("layout"), QString::fromUtf8(packed)}};
    bool ok = false;
    if (store.get(QStringLiteral("dashboard"),QStringLiteral("primary"),original,&error))
        ok = store.replace(QStringLiteral("dashboard"),original,value,result,&error);
    else if (error == QStringLiteral("entity_not_found"))
        ok = store.create(QStringLiteral("dashboard"),QStringLiteral("primary"),value,result,&error);
    return QJsonObject{{QStringLiteral("ok"),ok},{QStringLiteral("error"),error}};
}

static QJsonArray assignHostToGroup(QJsonArray rows, const QString& id, const QString& group) {
    for (int i = rows.size() - 1; i >= 0; --i) {
        const auto row = rows.at(i).toObject();
        if (row.value(QStringLiteral("kind")) == QStringLiteral("host") &&
            row.value(QStringLiteral("id")).toString() == id) rows.removeAt(i);
    }
    const QJsonObject entry{{QStringLiteral("kind"),QStringLiteral("host")},
        {QStringLiteral("id"),id},{QStringLiteral("group"),group}};
    int pos = rows.size();
    if (!group.isEmpty()) {
        for (int i = 0; i < rows.size(); ++i) {
            const auto row = rows.at(i).toObject();
            if (row.value(QStringLiteral("kind")) == QStringLiteral("group") &&
                row.value(QStringLiteral("id")).toString() == group) {
                pos = i + 1;
                while (pos < rows.size() &&
                    rows.at(pos).toObject().value(QStringLiteral("kind")) != QStringLiteral("group"))
                    ++pos;
                break;
            }
        }
    }
    rows.insert(pos,entry);
    return rows;
}

void MainWindow::requestVault() {
    // This action can create/provision a vault: dispatch exactly once.
    // Neither a timeout nor an ambiguous response triggers an automatic retry.
    if (vaultLoading_) return;
    vaultLoading_ = true;
    if (missingVault_) missingVault_->setEnabled(false);
    const QPointer<MainWindow> receiver(this);
    QThreadPool::globalInstance()->start([receiver]() {
        const auto reply = secretsRpc(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("vault.request")},
            {QStringLiteral("module"), QStringLiteral("neebles-connect")}
        }, 20000);
        if (auto* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, [receiver,reply]() {
                if (!receiver) return;
                receiver->vaultLoading_ = false;
                if (receiver->missingVault_) receiver->missingVault_->setEnabled(true);
                if (!reply.value(QStringLiteral("ok")).toBool()) {
                    QMessageBox::warning(receiver, QStringLiteral("NEEBLES Secrets"),
                        QStringLiteral("No se pudo solicitar la bóveda a Secrets. Revisa su estado antes de repetir."));
                    receiver->refreshSecretsAccess();
                    return;
                }
                // The installed Boss CLI remains the only authority for Open.
                const QString boss = QStandardPaths::findExecutable(QStringLiteral("neebles"));
                if (boss.isEmpty() || !QProcess::startDetached(boss,
                    {QStringLiteral("modules"),QStringLiteral("action"),
                     QStringLiteral("neebles-secrets"),QStringLiteral("open")})) {
                    QMessageBox::information(receiver, QStringLiteral("NEEBLES Secrets"),
                        QStringLiteral("Solicitud recibida. Abre Secrets desde Boss Launcher o Tray."));
                }
                receiver->refreshSecretsAccess();
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::refreshSaved() {
    if (!savedList_) return;
    while (QLayoutItem* item = savedList_->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    if (!vaultReady_) {
        savedList_->addWidget(new QLabel(QStringLiteral("Secrets bloqueado: My Connect no está disponible.")));
        savedList_->addStretch();
        return;
    }
    if (profiles_.isEmpty())
        savedList_->addWidget(new QLabel(QStringLiteral("No hay equipos. Pulsa Crear equipo para comenzar.")));
    for (const ConnectProfile& profile : profiles_) {
        auto* row = new QFrame();
        row->setStyleSheet(QStringLiteral("QFrame { border: 1px solid #5B21B6; border-radius: 8px; }"));
        auto* layout = new QVBoxLayout(row);
        auto* heading = new QHBoxLayout();
        heading->addWidget(new QLabel(profile.name + QStringLiteral(" · ") + profile.host, row), 1);
        auto* edit = new QPushButton(QStringLiteral("Editar equipo"), row);
        auto* remove = new QPushButton(QStringLiteral("Eliminar equipo"), row);
        heading->addWidget(edit); heading->addWidget(remove);
        layout->addLayout(heading);
        QObject::connect(edit, &QPushButton::clicked, this, [this, profile]() { editProfile(profile); });
        QObject::connect(remove, &QPushButton::clicked, this, [this, profile]() { deleteProfile(profile); });
        auto* connections = new QHBoxLayout();
        for (int protocol : {kSsh, kVnc, kVsCode, kSunshine}) {
            const QString title = protocol == kSsh ? QStringLiteral("SSH") :
                                  protocol == kVnc ? QStringLiteral("VNC") :
                                  protocol == kVsCode ? QStringLiteral("VS Code") :
                                                        QStringLiteral("Sunshine / Moonlight");
            const bool present = protocol == kSsh ? profile.hasSsh :
                                 protocol == kVnc ? profile.hasVnc :
                                 protocol == kVsCode ? vsCodeCounts_.value(profile.id) > 0 :
                                                       profile.hasSunshine;
            auto* group = new QVBoxLayout();
            group->addWidget(new QLabel(title + (present ? QStringLiteral(" · configurado") :
                                         QStringLiteral(" · vacío")), row));
            auto* manage = new QPushButton(protocol == kVsCode ? QStringLiteral("＋ / Administrar") :
                                           present ? QStringLiteral("Editar / Eliminar") :
                                                     QStringLiteral("＋ Agregar"), row);
            group->addWidget(manage);
            QObject::connect(manage, &QPushButton::clicked, this, [this, profile, protocol]() {
                if (protocol == kVsCode) manageVsCode(profile);
                else configureFixedProtocol(profile, protocol);
            });
            if ((protocol == kSsh || protocol == kVnc) && present) {
                auto* connectButton = new QPushButton(QStringLiteral("Conectar"), row);
                group->addWidget(connectButton);
                QObject::connect(connectButton, &QPushButton::clicked, this, [this, profile, protocol]() {
                    if (!ensureVaultReady()) return;
                    connectSaved(profile.id, protocol);
                });
            }
            connections->addLayout(group);
        }
        layout->addLayout(connections);
        savedList_->addWidget(row);
    }
    savedList_->addStretch();
}

void MainWindow::resetEditor() {
    editingId_.clear();
    editName_->clear();
    editHost_->clear();
    editSshUser_->clear();
    saveButton_->setText(QStringLiteral("Guardar equipo"));
    if (editGroup_) {
        editGroup_->clear();
        editGroup_->addItem(QStringLiteral("Sin grupo"), QString());
        for (const QJsonValue& item : dashboardRows_) {
            const auto row = item.toObject();
            if (row.value(QStringLiteral("kind")).toString() == QStringLiteral("group"))
                editGroup_->addItem(row.value(QStringLiteral("name")).toString(),
                                    row.value(QStringLiteral("id")).toString());
        }
    }
}

void MainWindow::editProfile(const ConnectProfile& profile) {
    if (!ensureVaultReady()) return;
    if (isOpen(profile.id)) {
        QMessageBox::information(this, QStringLiteral("Sesión abierta"),
                                 QStringLiteral("Cierra la pestaña del equipo antes de editarlo."));
        return;
    }
    editingId_ = profile.id;
    editName_->setText(profile.name);
    editHost_->setText(profile.host);
    editSshUser_->setText(profile.generalUser);
    saveButton_->setText(QStringLiteral("Actualizar equipo"));
    if (editGroup_) {
        editGroup_->clear();
        editGroup_->addItem(QStringLiteral("Sin grupo"), QString());
        for (const QJsonValue& row : dashboardRows_)
            if (row.toObject().value(QStringLiteral("kind")).toString() == QStringLiteral("group"))
                editGroup_->addItem(row.toObject().value(QStringLiteral("name")).toString(),
                                    row.toObject().value(QStringLiteral("id")).toString());
        editGroup_->setCurrentIndex(qMax(0, editGroup_->findData(groupFor(profile.id))));
    }
    showAdmin(kCreate);
}

void MainWindow::saveProfile() {
    if (!ensureVaultReady()) return;
    const QString name = editName_->text().trimmed();
    const QString host = editHost_->text().trimmed();
    const QString user = editSshUser_->text().trimmed();
    const QString id = editingId_;
    const QString group = editGroup_ ? editGroup_->currentData().toString() : QString();
    const QJsonArray rows = dashboardRows_;
    if (name.isEmpty() || !validHost(host)) {
        QMessageBox::warning(this,QStringLiteral("Datos obligatorios"),
            QStringLiteral("Indica un nombre y una IP/hostname válidos."));
        return;
    }
    if (isOpen(id) && !id.isEmpty()) {
        QMessageBox::information(this,QStringLiteral("Sesión activa"),
            QStringLiteral("Desconecta primero las sesiones activas de este equipo."));
        return;
    }
    runStructure(QStringLiteral("Guardando equipo en Secrets…"),
        [id,name,host,user,group,rows]() {
            const ConnectStructures::Store store([](const QJsonObject& request) {
                return secretsRpc(request,10000);
            });
            ConnectStructures::Record before, result;
            QString error;
            QJsonObject value;
            bool ok = false;
            if (!id.isEmpty()) {
                if (!store.get(QStringLiteral("myconnect"),id,before,&error))
                    return QJsonObject{{QStringLiteral("error"),error}};
                value = before.value;
                value.insert(QStringLiteral("name"),name);
                value.insert(QStringLiteral("ip"),host);
                if (user.isEmpty()) value.remove(QStringLiteral("user"));
                else value.insert(QStringLiteral("user"),user);
                ok = store.replace(QStringLiteral("myconnect"),before,value,result,&error);
            } else {
                value.insert(QStringLiteral("name"),name);
                value.insert(QStringLiteral("ip"),host);
                if (!user.isEmpty()) value.insert(QStringLiteral("user"),user);
                ok = store.create(QStringLiteral("myconnect"),
                    ConnectStructures::Store::newId(QStringLiteral("myconnect")), value,result,&error);
            }
            if (!ok) return QJsonObject{{QStringLiteral("error"),error}};
            const QJsonObject layoutResult=writeDashboardInWorker(assignHostToGroup(rows,result.id,group));
            return QJsonObject{{QStringLiteral("ok"),true},
                {QStringLiteral("layoutOk"),layoutResult.value(QStringLiteral("ok"))},
                {QStringLiteral("error"),layoutResult.value(QStringLiteral("error"))}};
        }, [this](const QJsonObject& result) {
            if (!result.value(QStringLiteral("ok")).toBool()) {
                QMessageBox::warning(this,QStringLiteral("Secrets"),
                    QStringLiteral("No se pudo guardar equipo (%1). Revisa Secrets antes de repetir.")
                        .arg(result.value(QStringLiteral("error")).toString()));
                return;
            }
            resetEditor();
            showAdmin(kSaved);
            if (!result.value(QStringLiteral("layoutOk")).toBool())
                QMessageBox::warning(this,QStringLiteral("Secrets"),
                    QStringLiteral("El equipo se guardó, pero su grupo no quedó confirmado (%1). "
                                   "No repitas la creación; revisa el equipo guardado.")
                        .arg(result.value(QStringLiteral("error")).toString()));
            refreshVaultDataAsync();
        });
}

void MainWindow::deleteProfile(const ConnectProfile& profile) {
    if (!ensureVaultReady()) return;
    if (isOpen(profile.id)) {
        QMessageBox::information(this,QStringLiteral("Sesión activa"),
            QStringLiteral("Desconecta primero las sesiones activas del equipo."));
        return;
    }
    if (vsCodeCounts_.value(profile.id) > 0) {
        QMessageBox::information(this,QStringLiteral("VS Code"),
            QStringLiteral("Elimina primero las instancias VS Code del equipo."));
        return;
    }
    if (QMessageBox::question(this,QStringLiteral("Eliminar equipo"),
        QStringLiteral("¿Eliminar «%1» y sus configuraciones? Esta acción no se puede deshacer.")
            .arg(profile.name)) != QMessageBox::Yes) return;
    QJsonArray rows = dashboardRows_;
    for (int i=rows.size()-1; i>=0; --i)
        if (rows.at(i).toObject().value(QStringLiteral("kind"))==QStringLiteral("host") &&
            rows.at(i).toObject().value(QStringLiteral("id")).toString()==profile.id)
            rows.removeAt(i);
    const QString id=profile.id;
    runStructure(QStringLiteral("Eliminando equipo en Secrets…"), [id,rows]() {
        const ConnectStructures::Store store([](const QJsonObject& request){
            return secretsRpc(request,10000);
        });
        ConnectStructures::Record before;
        QString error;
        if (!store.get(QStringLiteral("myconnect"),id,before,&error) ||
            !store.erase(QStringLiteral("myconnect"),before,&error))
            return QJsonObject{{QStringLiteral("error"),error}};
        const QJsonObject layoutResult=writeDashboardInWorker(rows);
        return QJsonObject{{QStringLiteral("ok"),true},
            {QStringLiteral("layoutOk"),layoutResult.value(QStringLiteral("ok"))},
            {QStringLiteral("error"),layoutResult.value(QStringLiteral("error"))}};
    }, [this](const QJsonObject& result) {
        if (!result.value(QStringLiteral("ok")).toBool()) {
            QMessageBox::warning(this,QStringLiteral("Secrets"),
                QStringLiteral("No se pudo eliminar (%1). Revisa Secrets antes de repetir.")
                    .arg(result.value(QStringLiteral("error")).toString()));
            return;
        }
        if (!result.value(QStringLiteral("layoutOk")).toBool())
            QMessageBox::warning(this,QStringLiteral("Secrets"),
                QStringLiteral("Equipo eliminado; no se pudo actualizar su orden. "
                               "Revisa la organización en Dashboard."));
        refreshVaultDataAsync();
    });
}

void MainWindow::configureFixedProtocol(const ConnectProfile& profile, int protocol) {
    if (!ensureVaultReady() || isOpen(profile.id)) {
        if (isOpen(profile.id)) QMessageBox::information(this,QStringLiteral("Sesión activa"),
            QStringLiteral("Desconecta primero las sesiones activas del equipo."));
        return;
    }
    const QString prefix = protocol == kSsh ? QStringLiteral("ssh") :
                           protocol == kVnc ? QStringLiteral("vnc") :
                           protocol == kSunshine ? QStringLiteral("sunshine") : QString();
    if (prefix.isEmpty()) return;
    const QString id=profile.id;
    runStructure(QStringLiteral("Cargando configuración en Secrets…"),[id,prefix]() {
        const ConnectStructures::Store store([](const QJsonObject& req){return secretsRpc(req,10000);});
        ConnectStructures::Record record;
        QString error;
        if (!store.get(QStringLiteral("myconnect"),id,record,&error))
            return QJsonObject{{QStringLiteral("error"),error}};
        QJsonObject publicValues;
        for (auto field : {"name","port","user","monitor"}) {
            const QString key=prefix+QLatin1Char('-')+QString::fromLatin1(field);
            if (record.value.contains(key)) publicValues.insert(key,record.value.value(key));
        }
        return QJsonObject{{QStringLiteral("ok"),true},
            {QStringLiteral("revision"),double(record.revision)},
            {QStringLiteral("fields"),publicValues}};
    },[this,profile,id,protocol,prefix](const QJsonObject& answer) {
        if (!answer.value(QStringLiteral("ok")).toBool()) {
            QMessageBox::warning(this,QStringLiteral("Secrets"),
                QStringLiteral("No se pudo leer la configuración: %1")
                    .arg(answer.value(QStringLiteral("error")).toString()));
            return;
        }
        const QJsonObject fields=answer.value(QStringLiteral("fields")).toObject();
        const quint64 revision=quint64(answer.value(QStringLiteral("revision")).toDouble());
        const auto key=[&prefix](const QString& field){return prefix+QLatin1Char('-')+field;};
        const bool configured=fields.contains(key(QStringLiteral("name")));
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("%1 · %2").arg(profile.name,prefix.toUpper()));
        auto* layout=new QVBoxLayout(&dialog);
        auto* form=new QFormLayout();
        auto* name=new QLineEdit(fields.value(key(QStringLiteral("name"))).toString(),&dialog);
        auto* port=portBox(&dialog,protocol==kSsh?22:5900);
        port->setValue(fields.value(key(QStringLiteral("port"))).toInt(protocol==kSsh?22:5900));
        auto* user=new QLineEdit(fields.value(key(QStringLiteral("user"))).toString(),&dialog);
        auto* password=new QLineEdit(&dialog);
        password->setEchoMode(QLineEdit::Password);
        password->setPlaceholderText(configured?QStringLiteral("Vacío = conservar clave"):
            QStringLiteral("Credencial opcional"));
        auto* monitor=new QLineEdit(fields.value(key(QStringLiteral("monitor"))).toString(),&dialog);
        form->addRow(QStringLiteral("Nombre"),name);
        if (protocol==kSsh) form->addRow(QStringLiteral("Usuario SSH"),user);
        if (protocol==kSsh||protocol==kVnc) form->addRow(QStringLiteral("Puerto"),port);
        form->addRow(QStringLiteral("Contraseña / credencial"),password);
        if (protocol==kVnc) form->addRow(QStringLiteral("Monitor (opcional)"),monitor);
        layout->addLayout(form);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if (configured) {
            auto* remove=new QPushButton(QStringLiteral("Eliminar configuración"),&dialog);
            layout->addWidget(remove);
            QObject::connect(remove,&QPushButton::clicked,&dialog,[&dialog](){dialog.done(2);});
        }
        const int choice=dialog.exec();
        if (choice!=QDialog::Accepted && choice!=2) return;
        if (choice==2 && QMessageBox::question(this,QStringLiteral("Eliminar configuración"),
            QStringLiteral("¿Eliminar la configuración %1?").arg(prefix))!=QMessageBox::Yes) return;
        if (choice==QDialog::Accepted && (name->text().trimmed().isEmpty() ||
             (protocol==kSsh && user->text().trimmed().isEmpty()))) {
            QMessageBox::warning(this,QStringLiteral("Datos incompletos"),
                QStringLiteral("Completa el nombre y el usuario SSH cuando corresponda."));
            return;
        }
        const QString newName=name->text().trimmed();
        const QString newUser=user->text().trimmed();
        const int newPort=port->value();
        QString newPassword=password->text();
        password->clear();
        const QString newMonitor=monitor->text().trimmed();
        const bool remove=choice==2;
        if (!ensureVaultReady() || isOpen(id)) {
            newPassword.fill(QChar('\0'));
            return;
        }
        const bool dispatched=runStructure(QStringLiteral("Guardando protocolo en Secrets…"),
            [id,revision,prefix,protocol,remove,newName,newUser,newPort,
             newPassword=std::move(newPassword),newMonitor]() mutable {
                const ConnectStructures::Store store([](const QJsonObject& req){return secretsRpc(req,10000);});
                ConnectStructures::Record original,updated;
                QString error;
                if (!store.get(QStringLiteral("myconnect"),id,original,&error))
                    return QJsonObject{{QStringLiteral("error"),error}};
                if (original.revision!=revision)
                    return QJsonObject{{QStringLiteral("error"),QStringLiteral("revision_conflict")}};
                QJsonObject value=original.value;
                const auto key=[&prefix](const QString& field){return prefix+QLatin1Char('-')+field;};
                if (remove) {
                    for(auto it=value.begin();it!=value.end();) {
                        if(it.key().startsWith(prefix+QLatin1Char('-')))it=value.erase(it);
                        else ++it;
                    }
                } else {
                    value.insert(key(QStringLiteral("name")),newName);
                    if (protocol==kSsh) value.insert(key(QStringLiteral("user")),newUser);
                    if (protocol==kSsh || protocol==kVnc) value.insert(key(QStringLiteral("port")),newPort);
                    if (!newPassword.isEmpty())value.insert(key(QStringLiteral("pass")),newPassword);
                    if (protocol==kVnc) {
                        if(newMonitor.isEmpty())value.remove(key(QStringLiteral("monitor")));
                        else value.insert(key(QStringLiteral("monitor")),newMonitor);
                    }
                }
                newPassword.fill(QChar('\0'));newPassword.clear();
                const bool ok=store.replace(QStringLiteral("myconnect"),original,value,updated,&error);
                return QJsonObject{{QStringLiteral("ok"),ok},{QStringLiteral("error"),error}};
            },[this](const QJsonObject& result){
                if (!result.value(QStringLiteral("ok")).toBool())
                    QMessageBox::warning(this,QStringLiteral("Secrets"),
                        QStringLiteral("No se pudo guardar protocolo (%1). Verifica antes de repetir.")
                            .arg(result.value(QStringLiteral("error")).toString()));
                else refreshVaultDataAsync();
            });
        (void)dispatched;
    });
}

void MainWindow::manageVsCode(const ConnectProfile& profile) {
    if (!ensureVaultReady() || isOpen(profile.id)) {
        if (isOpen(profile.id)) QMessageBox::information(this,QStringLiteral("Sesión activa"),
            QStringLiteral("Desconecta primero las sesiones activas del equipo."));
        return;
    }
    const QString equipment=profile.id;
    runStructure(QStringLiteral("Cargando instancias VS Code…"),[equipment]() {
        const ConnectStructures::Store store([](const QJsonObject& req){return secretsRpc(req,10000);});
        QVector<ConnectStructures::Record> records;
        QString error;
        if (!store.list(QStringLiteral("vscode"),records,&error))
            return QJsonObject{{QStringLiteral("error"),error}};
        QJsonArray entries;
        for (const auto& record:records) {
            if (record.value.value(QStringLiteral("equipment")).toString()!=equipment)continue;
            entries.append(QJsonObject{{QStringLiteral("id"),record.id},
                {QStringLiteral("revision"),double(record.revision)},
                {QStringLiteral("name"),record.value.value(QStringLiteral("name"))},
                {QStringLiteral("port"),record.value.value(QStringLiteral("port"))}});
        }
        return QJsonObject{{QStringLiteral("ok"),true},{QStringLiteral("entries"),entries}};
    },[this,profile,equipment](const QJsonObject& answer) {
        if (!answer.value(QStringLiteral("ok")).toBool()) {
            QMessageBox::warning(this,QStringLiteral("Secrets"),
                QStringLiteral("No se pudieron recuperar las instancias: %1")
                    .arg(answer.value(QStringLiteral("error")).toString()));
            return;
        }
        const QJsonArray entries=answer.value(QStringLiteral("entries")).toArray();
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("VS Code · %1").arg(profile.name));
        auto* outer=new QVBoxLayout(&dialog);
        outer->addWidget(new QLabel(QStringLiteral("Instancias VS Code (0..N)"),&dialog));
        for (const auto& value:entries) {
            const auto entry=value.toObject();
            auto* row=new QHBoxLayout();
            row->addWidget(new QLabel(QStringLiteral("%1 · puerto %2").arg(
                entry.value(QStringLiteral("name")).toString()).arg(
                entry.value(QStringLiteral("port")).toInt()),&dialog),1);
            auto* edit=new QPushButton(QStringLiteral("Editar"),&dialog);
            auto* erase=new QPushButton(QStringLiteral("Eliminar"),&dialog);
            row->addWidget(edit);row->addWidget(erase);outer->addLayout(row);
            QObject::connect(edit,&QPushButton::clicked,&dialog,[&dialog,entry]() {
                dialog.setProperty("target",entry.value(QStringLiteral("id")));
                dialog.done(3);
            });
            QObject::connect(erase,&QPushButton::clicked,&dialog,[&dialog,entry]() {
                dialog.setProperty("target",entry.value(QStringLiteral("id")));
                dialog.done(4);
            });
        }
        auto* add=new QPushButton(QStringLiteral("＋ Nueva instancia VS Code"),&dialog);
        outer->addWidget(add);
        QObject::connect(add,&QPushButton::clicked,&dialog,[&dialog](){dialog.done(2);});
        auto* close=new QPushButton(QStringLiteral("Cerrar"),&dialog);
        outer->addWidget(close);
        QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
        const int action=dialog.exec();
        if (action!=2&&action!=3&&action!=4)return;
        const QString target=dialog.property("target").toString();
        QJsonObject chosen;
        for (const auto& value:entries) if(value.toObject().value(QStringLiteral("id")).toString()==target)
            chosen=value.toObject();
        if (action!=2 && chosen.isEmpty())return;
        if (action==4 && QMessageBox::question(this,QStringLiteral("Eliminar VS Code"),
                QStringLiteral("¿Eliminar esta instancia VS Code?"))!=QMessageBox::Yes)return;
        QString newName;
        QString newPassword;
        int newPort=0;
        if (action==2||action==3) {
            QDialog formDialog(this);
            formDialog.setWindowTitle(action==2?QStringLiteral("Agregar VS Code"):
                QStringLiteral("Editar VS Code"));
            auto* layout=new QVBoxLayout(&formDialog);
            auto* form=new QFormLayout();
            auto* name=new QLineEdit(action==2?QString():chosen.value(QStringLiteral("name")).toString(),&formDialog);
            auto* port=portBox(&formDialog,action==2?1:chosen.value(QStringLiteral("port")).toInt(1));
            auto* password=new QLineEdit(&formDialog);
            password->setEchoMode(QLineEdit::Password);
            password->setPlaceholderText(action==2?QStringLiteral("Credencial opcional"):
                QStringLiteral("Vacío = conservar clave anterior"));
            form->addRow(QStringLiteral("Nombre"),name);
            form->addRow(QStringLiteral("Puerto"),port);
            form->addRow(QStringLiteral("Contraseña"),password);
            layout->addLayout(form);
            auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&formDialog);
            layout->addWidget(buttons);
            QObject::connect(buttons,&QDialogButtonBox::accepted,&formDialog,&QDialog::accept);
            QObject::connect(buttons,&QDialogButtonBox::rejected,&formDialog,&QDialog::reject);
            if(formDialog.exec()!=QDialog::Accepted)return;
            newName=name->text().trimmed();newPort=port->value();
            newPassword=password->text();password->clear();
            if(newName.isEmpty()) {
                newPassword.fill(QChar('\0'));
                QMessageBox::warning(this,QStringLiteral("Datos incompletos"),
                    QStringLiteral("Indica un nombre VS Code."));
                return;
            }
        }
        if (!ensureVaultReady() || isOpen(equipment)) {
            newPassword.fill(QChar('\0'));
            return;
        }
        runStructure(QStringLiteral("Actualizando VS Code en Secrets…"),
            [equipment,target,revision=quint64(chosen.value(QStringLiteral("revision")).toDouble()),
             action,newName,newPort,newPassword=std::move(newPassword)]() mutable {
                const ConnectStructures::Store store([](const QJsonObject& req){return secretsRpc(req,10000);});
                ConnectStructures::Record previous,updated;
                QString error;
                if(action!=2) {
                    if(!store.get(QStringLiteral("vscode"),target,previous,&error))
                        return QJsonObject{{QStringLiteral("error"),error}};
                    if(previous.revision!=revision ||
                       previous.value.value(QStringLiteral("equipment")).toString()!=equipment)
                        return QJsonObject{{QStringLiteral("error"),QStringLiteral("revision_conflict")}};
                }
                bool ok=false;
                if(action==4)ok=store.erase(QStringLiteral("vscode"),previous,&error);
                else {
                    QJsonObject fields=action==2?QJsonObject{}:previous.value;
                    fields.insert(QStringLiteral("equipment"),equipment);
                    fields.insert(QStringLiteral("name"),newName);
                    fields.insert(QStringLiteral("port"),newPort);
                    if(!newPassword.isEmpty())fields.insert(QStringLiteral("pass"),newPassword);
                    newPassword.fill(QChar('\0'));newPassword.clear();
                    if(action==2)ok=store.create(QStringLiteral("vscode"),
                        ConnectStructures::Store::newId(QStringLiteral("vscode")),fields,updated,&error);
                    else ok=store.replace(QStringLiteral("vscode"),previous,fields,updated,&error);
                }
                return QJsonObject{{QStringLiteral("ok"),ok},{QStringLiteral("error"),error}};
            },[this](const QJsonObject& result) {
                if (!result.value(QStringLiteral("ok")).toBool())
                    QMessageBox::warning(this,QStringLiteral("Secrets"),
                        QStringLiteral("Operación VS Code no confirmada (%1). Revisa antes de repetir.")
                            .arg(result.value(QStringLiteral("error")).toString()));
                else refreshVaultDataAsync();
            });
    });
}

void MainWindow::openQuick() {
    const int selection = quickProtocol_->currentIndex();
    if (selection < 0 || selection >= 4) return;
    const QString name = quickName_->text().trimmed();
    const QString host = quickHost_->text().trimmed();
    if (name.isEmpty() || !validHost(host)) {
        QMessageBox::warning(this, QStringLiteral("Datos obligatorios"),
                             QStringLiteral("Escribe un nombre y una IP/hostname válidos."));
        return;
    }
    if (selection == 1 && quickUser_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Usuario SSH obligatorio"),
                             QStringLiteral("Completa el usuario SSH."));
        return;
    }
    if (selection != 3 && quickPort_->value() == 0) {
        QMessageBox::warning(this, QStringLiteral("Puerto obligatorio"),
                             QStringLiteral("Selecciona un puerto entre 1 y 65535."));
        return;
    }
    if (selection == 2 && quickInstanceName_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Nombre VS Code obligatorio"),
                             QStringLiteral("Asigna un nombre a la instancia temporal."));
        return;
    }

    // One logical Quick host tab, with a single SSH/VNC/VS Code/Sunshine pane.
    // A changed hostname starts a different ephemeral node without reusing credentials.
    if (quickSessionId_.isEmpty() || quickSessionHost_ != host) {
        quickSessionId_ = QStringLiteral("quick:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        quickSessionHost_ = host;
    }
    ConnectProfile profile;
    profile.id = quickSessionId_;
    profile.host = host;
    profile.name = name;
    profile.sshUser = selection == 1 ? quickUser_->text().trimmed() : QString();
    profile.sshPort = selection == 1 ? quickPort_->value() : 22;
    profile.vncPort = selection == 0 ? quickPort_->value() : 5900;
    const int protocol = selection == 0 ? kVnc : selection == 1 ? kSsh
                       : selection == 2 ? kVsCode : kSunshine;
    QString password = quickPassword_->text();
    // Purge this protocol's draft after dispatch; credentials are never persisted.
    quickPassword_->clear();
    quickDrafts_[selection].password.fill(QChar('\0'));
    quickDrafts_[selection].password.clear();
    openProfile(profile, protocol, password);
    password.fill(QChar('\0'));
    password.clear();
}

bool MainWindow::loadDashboardLayout() {
    if (!vaultReady_) return false;
    const ConnectStructures::Store store([](const QJsonObject& request) {
        return secretsRpc(request, 10000);
    });
    ConnectStructures::Record record;
    QString error;
    if (!store.get(QStringLiteral("dashboard"),QStringLiteral("primary"),record,&error)) {
        if (error != QStringLiteral("entity_not_found")) return false;
        dashboardRows_ = QJsonArray{};
        for (const ConnectProfile& p:profiles_)
            dashboardRows_.append(QJsonObject{
                {QStringLiteral("kind"),QStringLiteral("host")},
                {QStringLiteral("id"),p.id},{QStringLiteral("group"),QString()}});
        dashboardLayoutLoaded_ = true;
        return true;
    }
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(
        record.value.value(QStringLiteral("layout")).toString().toUtf8(),&parseError);
    if (parseError.error!=QJsonParseError::NoError || !document.isArray()) return false;
    const QJsonArray loaded=document.array();
    if (loaded.size()>2048) return false;
    QSet<QString> groupIds, equipmentIds;
    for (const QJsonValue& v : loaded) {
        if (!v.isObject()) return false;
        const QJsonObject row=v.toObject();
        const QString kind=row.value(QStringLiteral("kind")).toString();
        const QString id=row.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) return false;
        if (kind==QStringLiteral("group")) {
            const QString name=row.value(QStringLiteral("name")).toString();
            if (groupIds.contains(id)||name.isEmpty()||name.size()>100) return false;
            const QString color=row.value(QStringLiteral("color")).toString();
            if (!QColor(color).isValid()) return false;
            groupIds.insert(id);
        } else if (kind==QStringLiteral("host")) {
            if (equipmentIds.contains(id)) return false;
            equipmentIds.insert(id);
        } else return false;
    }
    dashboardRows_=loaded;
    for (const ConnectProfile& p : profiles_) {
        if(!equipmentIds.contains(p.id))
            dashboardRows_.append(QJsonObject{
                {QStringLiteral("kind"),QStringLiteral("host")},
                {QStringLiteral("id"),p.id},{QStringLiteral("group"),QString()}});
    }
    dashboardLayoutLoaded_=true;
    return true;
}

bool MainWindow::saveDashboardLayout() {
    if (!ensureVaultReady() || !dashboardLayoutLoaded_) return false;
    const QJsonArray rows = dashboardRows_;
    if (QJsonDocument(rows).toJson(QJsonDocument::Compact).size() > 7900) {
        QMessageBox::warning(this,QStringLiteral("Dashboard"),
            QStringLiteral("La organización supera el límite de almacenamiento actual."));
        return false;
    }
    return runStructure(QStringLiteral("Guardando organización en Secrets…"),
        [rows]() {return writeDashboardInWorker(rows);},
        [this,rows](const QJsonObject& result) {
            if (!result.value(QStringLiteral("ok")).toBool()) {
                dashboardRows_ = dashboardCommittedRows_;
                renderDashboard();
                QMessageBox::warning(this, QStringLiteral("Secrets"),
                    QStringLiteral("No se guardó el Dashboard (%1). Revisa el estado antes de repetir.")
                        .arg(result.value(QStringLiteral("error")).toString()));
                return;
            }
            dashboardCommittedRows_ = rows;
            statusBar()->showMessage(QStringLiteral("Organización guardada en Secrets"),3500);
        });
}

QString MainWindow::groupFor(const QString& profileId) const {
    for (const QJsonValue& item : dashboardRows_) {
        const auto row=item.toObject();
        if (row.value(QStringLiteral("kind")).toString()==QStringLiteral("host") &&
            row.value(QStringLiteral("id")).toString()==profileId)
            return row.value(QStringLiteral("group")).toString();
    }
    return {};
}

void MainWindow::moveDashboardRow(int index,int delta) {
    if (index<0||index>=dashboardRows_.size()||index+delta<0||
        index+delta>=dashboardRows_.size()||!ensureVaultReady()) return;
    const QJsonArray before=dashboardRows_;
    const QJsonObject target=dashboardRows_.at(index).toObject();
    int destination=index+delta;
    if (target.value(QStringLiteral("kind"))==QStringLiteral("group")) {
        while(destination>=0 && destination<dashboardRows_.size() &&
              dashboardRows_.at(destination).toObject().value(QStringLiteral("kind"))!=QStringLiteral("group"))
              destination+=delta;
        if(destination<0||destination>=dashboardRows_.size())return;
    } else {
        while(destination>=0 && destination<dashboardRows_.size() &&
              dashboardRows_.at(destination).toObject().value(QStringLiteral("kind"))==QStringLiteral("group"))
              destination+=delta;
        if(destination<0||destination>=dashboardRows_.size())return;
        const QJsonObject neighbor=dashboardRows_.at(destination).toObject();
        if(neighbor.value(QStringLiteral("kind"))!=QStringLiteral("host") ||
           neighbor.value(QStringLiteral("group"))!=target.value(QStringLiteral("group")))return;
    }
    const QJsonValue previous=dashboardRows_.at(destination);
    dashboardRows_.replace(destination,dashboardRows_.at(index));
    dashboardRows_.replace(index,previous);
    if (!saveDashboardLayout()) dashboardRows_=before;
    renderDashboard();
}

void MainWindow::moveEquipmentToGroup(const QString& id,const QString& group) {
    if (!dashboardLayoutLoaded_||!ensureVaultReady()) return;
    const QJsonArray before=dashboardRows_;
    for(int i=dashboardRows_.size()-1;i>=0;--i) {
        const QJsonObject row=dashboardRows_.at(i).toObject();
        if (row.value(QStringLiteral("kind"))==QStringLiteral("host")&&
            row.value(QStringLiteral("id")).toString()==id) dashboardRows_.removeAt(i);
    }
    const QJsonObject host{{QStringLiteral("kind"),QStringLiteral("host")},
                           {QStringLiteral("id"),id},
                           {QStringLiteral("group"),group}};
    int target=dashboardRows_.size();
    if (!group.isEmpty()) {
        int start=-1;
        for(int i=0;i<dashboardRows_.size();++i) {
            const auto row=dashboardRows_.at(i).toObject();
            if (row.value(QStringLiteral("kind"))==QStringLiteral("group") &&
                row.value(QStringLiteral("id")).toString()==group) {start=i;break;}
        }
        if (start>=0) {
            target=start+1;
            while(target<dashboardRows_.size() &&
                dashboardRows_.at(target).toObject().value(QStringLiteral("kind"))!=QStringLiteral("group"))
                ++target;
        }
    }
    dashboardRows_.insert(target,host);
    if (!saveDashboardLayout()) dashboardRows_=before;
    renderDashboard();
}

void MainWindow::addDashboardGroup() {
    if (!ensureVaultReady()) return;
    bool ok=false;
    const QString name=QInputDialog::getText(this,QStringLiteral("Grupo de conexiones"),
        QStringLiteral("Nombre del separador:"),QLineEdit::Normal,QString(),&ok).trimmed();
    if (!ok||name.isEmpty()||name.size()>100) return;
    const QColor color=QColorDialog::getColor(QColor(QStringLiteral("#22D3EE")),this,
                                              QStringLiteral("Color del separador"));
    if(!color.isValid()) return;
    const QJsonArray before=dashboardRows_;
    dashboardRows_.append(QJsonObject{{QStringLiteral("kind"),QStringLiteral("group")},
            {QStringLiteral("id"),QStringLiteral("g-")+QUuid::createUuid().toString(QUuid::WithoutBraces).left(20)},
            {QStringLiteral("name"),name},{QStringLiteral("color"),color.name()}});
    if(!saveDashboardLayout()) dashboardRows_=before;
    renderDashboard();
}

void MainWindow::renameDashboardGroup(int index) {
    if (!ensureVaultReady()||index<0||index>=dashboardRows_.size()) return;
    QJsonObject group=dashboardRows_.at(index).toObject();
    if(group.value(QStringLiteral("kind"))!=QStringLiteral("group")) return;
    bool ok=false;
    const QString name=QInputDialog::getText(this,QStringLiteral("Renombrar separador"),
        QStringLiteral("Nombre:"),QLineEdit::Normal,group.value(QStringLiteral("name")).toString(),&ok).trimmed();
    if(!ok||name.isEmpty()||name.size()>100) return;
    const QJsonArray before=dashboardRows_;
    group.insert(QStringLiteral("name"),name);
    dashboardRows_.replace(index,group);
    if(!saveDashboardLayout()) dashboardRows_=before;
    renderDashboard();
}

void MainWindow::setDashboardGroupColor(int index) {
    if (!ensureVaultReady()||index<0||index>=dashboardRows_.size()) return;
    QJsonObject group=dashboardRows_.at(index).toObject();
    if(group.value(QStringLiteral("kind"))!=QStringLiteral("group")) return;
    const QColor color=QColorDialog::getColor(QColor(group.value(QStringLiteral("color")).toString()),this,
                                             QStringLiteral("Color del separador"));
    if (!color.isValid()) return;
    const QJsonArray before=dashboardRows_;
    group.insert(QStringLiteral("color"),color.name());
    dashboardRows_.replace(index,group);
    if(!saveDashboardLayout()) dashboardRows_=before;
    renderDashboard();
}

void MainWindow::renderDashboard() {
    if (!dashboardList_) return;
    while(QLayoutItem* item=dashboardList_->takeAt(0)) {
        if (QWidget* widget=item->widget()) widget->deleteLater();
        delete item;
    }
    if (!vaultReady_ || !dashboardLayoutLoaded_) {
        dashboardList_->addWidget(new QLabel(QStringLiteral(
            "Desbloquea Secrets para mostrar y organizar las conexiones de My Connect.")));
        dashboardList_->addStretch();
        return;
    }
    QSet<QString> present;
    auto drawHost=[this,&present](const ConnectProfile& profile,int orderIndex) {
        if(present.contains(profile.id))return;
        present.insert(profile.id);
        auto* row=new QWidget();
        auto* h=new QHBoxLayout(row);
        h->setContentsMargins(6,3,6,3);h->setSpacing(5);
        auto* label=new QLabel(profile.name,row);
        label->setToolTip(profile.host);
        h->addWidget(label,1);
        const auto addProtocol=[this,row,h,&profile](int kind,bool configured,
                                                     const QString& iconFile,const QString& labelText) {
            if(!configured)return;
            auto* button=new QToolButton(row);
            button->setAutoRaise(true);
            button->setIcon(QIcon(QDir(moduleRoot_).filePath(QStringLiteral("assets/")+iconFile)));
            button->setIconSize(QSize(21,21));
            button->setFixedSize(29,29);
            bool live=false;
            if(auto* sub=savedSubtab(profile.id)) {
                for(int n=0;n<sub->count();++n) {
                    if(kind==kSsh) {
                        auto* ssh=dynamic_cast<SshPane*>(sub->widget(n));
                        if(ssh) live=ssh->connected();
                    } else if(kind==kVnc) {
                        auto* vnc=dynamic_cast<VncPane*>(sub->widget(n));
                        if(vnc) live=vnc->connected();
                    }
                }
            }
            button->setToolTip(profile.name+QStringLiteral(" · ")+labelText+
                    (live?QStringLiteral(" · Conectado"):QStringLiteral(" · Desconectado")));
            auto* state=new QLabel(live?QStringLiteral("●"):QStringLiteral("●"),row);
            state->setStyleSheet(live?QStringLiteral("color: #22C55E;"):
                                      QStringLiteral("color: #EF4444;"));
            h->addWidget(button);h->addWidget(state);
            QObject::connect(button,&QToolButton::clicked,this,[this,id=profile.id,kind]() {
                if (!ensureVaultReady())return;
                auto* sub=savedSubtab(id);
                if(!sub)return;
                for(int i=1;i<tabs_->count();++i)
                    if(tabs_->tabBar()->tabData(i).toString()==id) {tabs_->setCurrentIndex(i);break;}
                for(int i=0;i<sub->count();++i) {
                    if ((kind==kSsh && dynamic_cast<SshPane*>(sub->widget(i))) ||
                        (kind==kVnc && dynamic_cast<VncPane*>(sub->widget(i)))) {
                        sub->setCurrentIndex(i);break;
                    }
                }
                refreshVisualState();
            });
        };
        addProtocol(kSsh,profile.hasSsh,QStringLiteral("neebles-connect-ssh-256.png"),QStringLiteral("SSH"));
        addProtocol(kVnc,profile.hasVnc,QStringLiteral("neebles-connect-vnc-256.png"),QStringLiteral("VNC"));
        if(orderIndex>=0) {
            auto* up=new QToolButton(row);up->setText(QStringLiteral("↑"));up->setToolTip(QStringLiteral("Subir"));
            auto* down=new QToolButton(row);down->setText(QStringLiteral("↓"));down->setToolTip(QStringLiteral("Bajar"));
            h->addWidget(up);h->addWidget(down);
            QObject::connect(up,&QToolButton::clicked,this,[this,orderIndex](){moveDashboardRow(orderIndex,-1);});
            QObject::connect(down,&QToolButton::clicked,this,[this,orderIndex](){moveDashboardRow(orderIndex,1);});
        }
        dashboardList_->addWidget(row);
    };
    auto addGroupLine=[this](const QString& title,const QString& color,int rowIndex){
        auto* widget=new QWidget();
        auto* h=new QHBoxLayout(widget);h->setContentsMargins(5,6,5,4);
        auto* stripe=new QFrame(widget);
        stripe->setFixedSize(5,22);
        stripe->setStyleSheet(QStringLiteral("background-color: %1;").arg(color));
        h->addWidget(stripe);
        h->addWidget(new QLabel(title,widget),1);
        if(rowIndex>=0) {
            auto* rename=new QToolButton(widget);rename->setText(QStringLiteral("✎"));rename->setToolTip(QStringLiteral("Renombrar"));
            auto* palette=new QToolButton(widget);palette->setText(QStringLiteral("◉"));palette->setToolTip(QStringLiteral("Cambiar color"));
            auto* up=new QToolButton(widget);up->setText(QStringLiteral("↑"));up->setToolTip(QStringLiteral("Subir separador"));
            auto* down=new QToolButton(widget);down->setText(QStringLiteral("↓"));down->setToolTip(QStringLiteral("Bajar separador"));
            for(QToolButton* button:{rename,palette,up,down})h->addWidget(button);
            QObject::connect(rename,&QToolButton::clicked,this,[this,rowIndex](){renameDashboardGroup(rowIndex);});
            QObject::connect(palette,&QToolButton::clicked,this,[this,rowIndex](){setDashboardGroupColor(rowIndex);});
            QObject::connect(up,&QToolButton::clicked,this,[this,rowIndex](){moveDashboardRow(rowIndex,-1);});
            QObject::connect(down,&QToolButton::clicked,this,[this,rowIndex](){moveDashboardRow(rowIndex,1);});
        }
        dashboardList_->addWidget(widget);
    };
    QSet<QString> validGroups;
    for(int rowIndex=0;rowIndex<dashboardRows_.size();++rowIndex) {
        const QJsonObject row=dashboardRows_.at(rowIndex).toObject();
        if(row.value(QStringLiteral("kind")).toString()!=QStringLiteral("group"))continue;
        const QString group=row.value(QStringLiteral("id")).toString();
        validGroups.insert(group);
        addGroupLine(row.value(QStringLiteral("name")).toString(),
                     row.value(QStringLiteral("color")).toString(),rowIndex);
        for(int i=0;i<dashboardRows_.size();++i){
            const auto hostRow=dashboardRows_.at(i).toObject();
            if(hostRow.value(QStringLiteral("kind"))!=QStringLiteral("host") ||
               hostRow.value(QStringLiteral("group")).toString()!=group) continue;
            for(const auto& profile:profiles_)
                if(profile.id==hostRow.value(QStringLiteral("id")).toString()){
                    drawHost(profile,i);break;
                }
        }
    }
    bool needUngrouped=false;
    for(const auto& profile:profiles_)
        if(!present.contains(profile.id)) {needUngrouped=true;break;}
    if(needUngrouped) {
        addGroupLine(QStringLiteral("Sin grupo"),QStringLiteral("#64748B"),-1);
        for(int i=0;i<dashboardRows_.size();++i){
            const auto hostRow=dashboardRows_.at(i).toObject();
            if(hostRow.value(QStringLiteral("kind"))!=QStringLiteral("host") ||
               validGroups.contains(hostRow.value(QStringLiteral("group")).toString())) continue;
            for(const auto& profile:profiles_)
                if(profile.id==hostRow.value(QStringLiteral("id")).toString()) {
                    drawHost(profile,i);break;
                }
        }
        for(const auto& profile:profiles_)if(!present.contains(profile.id))drawHost(profile,-1);
    }
    dashboardList_->addStretch();
}

QTabWidget* MainWindow::savedSubtab(const QString& profileId) const {
    if (!tabs_) return nullptr;
    for (int i = 1; i < tabs_->count(); ++i) {
        if (tabs_->tabBar()->tabData(i).toString() == profileId)
            return qobject_cast<QTabWidget*>(tabs_->widget(i));
    }
    return nullptr;
}

bool MainWindow::activeConnection(QWidget** pane, int* protocol) const {
    if (!tabs_ || tabs_->currentIndex() <= 0) return false;
    auto* sub = qobject_cast<QTabWidget*>(tabs_->currentWidget());
    if (!sub || sub->currentIndex() < 0) return false;
    QWidget* current = sub->currentWidget();
    if (auto* ssh = dynamic_cast<SshPane*>(current)) {
        if (!ssh->connected()) return false;
        if (pane) *pane = ssh;
        if (protocol) *protocol = kSsh;
        return true;
    }
    if (auto* vnc = dynamic_cast<VncPane*>(current)) {
        if (!vnc->connected()) return false;
        if (pane) *pane = vnc;
        if (protocol) *protocol = kVnc;
        return true;
    }
    return false;
}

bool MainWindow::isOpen(const QString& id) const {
    auto* sub = savedSubtab(id);
    if (!sub) return false;
    for (int i=0; i<sub->count(); ++i) {
        if (auto* ssh=dynamic_cast<SshPane*>(sub->widget(i)); ssh && ssh->connected()) return true;
        if (auto* vnc=dynamic_cast<VncPane*>(sub->widget(i)); vnc && vnc->connected()) return true;
    }
    return false;
}

void MainWindow::buildSavedTab(const ConnectProfile& profile) {
    if (savedSubtab(profile.id) || !vaultReady_) return;
    auto* sub = new QTabWidget(tabs_);
    sub->setDocumentMode(true);
    sub->setTabsClosable(false);
    auto image = [this](const QString& file)->QIcon {
        return QIcon(QDir(moduleRoot_).filePath(QStringLiteral("assets/") + file));
    };
    if (profile.hasSsh) {
        auto* ssh = new SshPane(profile.host, profile.sshPort, profile.sshUser, sub);
        ssh->setAccessGuard([this]() {return ensureVaultReady();});
        ssh->setConnectRequestHandler([this,id=profile.id]() { connectSaved(id,kSsh); });
        sub->addTab(ssh,image(QStringLiteral("neebles-connect-ssh-256.png")),QStringLiteral("SSH"));
    }
    if (profile.hasVnc) {
        auto* vnc = new VncPane(profile.host, profile.vncPort, sub);
        vnc->setAccessGuard([this]() {return ensureVaultReady();});
        vnc->setConnectRequestHandler([this,id=profile.id]() { connectSaved(id,kVnc); });
        sub->addTab(vnc,image(QStringLiteral("neebles-connect-vnc-256.png")),QStringLiteral("VNC"));
    }
    // Q07D2A: tabs use only metadata hydrated by the Secrets worker.
    // No broker call can block Qt while constructing permanent host tabs.
    for (const auto& entry : vsCodeTabs_.value(profile.id)) {
        auto* placeholder = new QLabel(QStringLiteral("VS Code: motor aún no implementado"),sub);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setProperty("connectRecordId",entry.first);
        sub->addTab(placeholder,image(QStringLiteral("neebles-connect-vscode-256.png")),
                    entry.second);
    }
    if (profile.hasSunshine) {
        auto* placeholder = new QLabel(QStringLiteral("Sunshine/Moonlight: motor aún no implementado"),sub);
        placeholder->setAlignment(Qt::AlignCenter);
        sub->addTab(placeholder,image(QStringLiteral("neebles-connect-sunshine-moonlight-256.png")),
                    QStringLiteral("Sunshine/Moonlight"));
    }
    if (!sub->count()) {
        auto* empty = new QLabel(QStringLiteral("Equipo registrado sin protocolos. Configúralos desde My Connect."),sub);
        empty->setAlignment(Qt::AlignCenter);
        sub->addTab(empty,QStringLiteral("Sin conexiones"));
    }
    QObject::connect(sub,&QTabWidget::currentChanged,this,[this](int){refreshVisualState();});
    const int index=tabs_->addTab(sub,profile.name);
    tabs_->tabBar()->setTabData(index,profile.id);
    tabs_->tabBar()->setTabButton(index,QTabBar::RightSide,nullptr);
    tabs_->tabBar()->setTabButton(index,QTabBar::LeftSide,nullptr);
}

void MainWindow::syncSavedTabs() {
    if (!tabs_ || !vaultReady_) return;
    syncingTabs_=true;
    const QString selectedId=tabs_->currentIndex()>0 ?
        tabs_->tabBar()->tabData(tabs_->currentIndex()).toString() : QString();
    QSet<QString> current;
    for (const ConnectProfile& p : profiles_) current.insert(p.id);
    for (int i=tabs_->count()-1;i>=1;--i) {
        const QString id=tabs_->tabBar()->tabData(i).toString();
        if (id.startsWith(QStringLiteral("quick:")) || current.contains(id)) continue;
        auto* page=tabs_->widget(i);
        tabs_->removeTab(i);
        delete page;
    }
    for (const ConnectProfile& p : profiles_) {
        if (auto* sub=savedSubtab(p.id); sub && !isOpen(p.id)) {
            for(int i=1;i<tabs_->count();++i)
                if(tabs_->widget(i)==sub) {tabs_->removeTab(i);delete sub;break;}
        }
        if (!savedSubtab(p.id)) buildSavedTab(p);
        for(int i=1;i<tabs_->count();++i)
            if(tabs_->tabBar()->tabData(i).toString()==p.id) tabs_->setTabText(i,p.name);
    }
    // Saved profiles are never closable, regardless of QTabWidget's global policy.
    for(int i=1;i<tabs_->count();++i)
        if(!tabs_->tabBar()->tabData(i).toString().startsWith(QStringLiteral("quick:"))) {
            tabs_->tabBar()->setTabButton(i,QTabBar::RightSide,nullptr);
            tabs_->tabBar()->setTabButton(i,QTabBar::LeftSide,nullptr);
        }
    if(!selectedId.isEmpty())
        for(int i=1;i<tabs_->count();++i)
            if(tabs_->tabBar()->tabData(i).toString()==selectedId) {tabs_->setCurrentIndex(i);break;}
    syncingTabs_=false;
}

void MainWindow::refreshVisualState() {
    if (!sidebar_ || !tabs_) return;
    QWidget* pane=nullptr; int protocol=-1;
    const bool compact=activeConnection(&pane,&protocol);
    if(lastCompact_!=compact) {
        lastCompact_=compact;
        sidebar_->setFixedWidth(compact?38:235);
        if (brandLogo_) brandLogo_->setVisible(!compact);
        if (sidebarEdition_) sidebarEdition_->setVisible(!compact);
        if (sidebarLayout_) sidebarLayout_->setContentsMargins(compact?4:9,compact?5:9,compact?4:9,6);
        for (QPushButton* button : adminButtons_) {
            button->setText(compact?QString():button->property("navLabel").toString());
            button->setFixedHeight(compact?29:39);
            button->setIconSize(QSize(compact?16:19,compact?16:19));
            button->setToolTip(button->property("navLabel").toString());
        }
    }
    QWidget* tools=stopTool_?stopTool_->parentWidget():nullptr;
    if (tools) tools->setVisible(compact);
    if (copyTool_) {
        copyTool_->setVisible(compact && (protocol == kSsh || protocol == kVnc));
        copyTool_->setEnabled(compact && (protocol == kSsh || protocol == kVnc));
        if (compact) copyTool_->setToolTip(protocol == kSsh ?
            QStringLiteral("Copiar selección SSH (incluido scrollback)") :
            QStringLiteral("Copiar imagen del monitor VNC seleccionado"));
    }
    if (monitorTools_) {
        monitorTools_->setVisible(compact && protocol == kVnc);
        auto* currentVnc = compact && protocol == kVnc ? dynamic_cast<VncPane*>(pane) : nullptr;
        const auto revision = currentVnc ? currentVnc->topologyRevision() : 0;
        const int selected = currentVnc ? currentVnc->selectedMonitor() : -2;
        if (currentVnc != visualVncPane_ || revision != visualVncRevision_ ||
            selected != visualMonitorSelection_ || visualProtocol_ != (compact?protocol:-1)) {
            visualProtocol_ = compact ? protocol : -1;
            visualVncPane_ = currentVnc;
            visualVncRevision_ = revision;
            visualMonitorSelection_ = selected;
            while (QLayoutItem* child = monitorLayout_->takeAt(0)) {
                if (auto* widget = child->widget()) delete widget;
                delete child;
            }
            if (currentVnc) {
                auto createButton = [this,currentVnc](int index, const QString& tooltip, bool chosen) {
                    auto* button = new QToolButton(monitorTools_);
                    button->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon));
                    button->setIconSize(QSize(15,15));
                    button->setFixedSize(28,28);
                    button->setCheckable(true);
                    button->setChecked(chosen);
                    button->setToolTip(tooltip);
                    button->setStyleSheet(QStringLiteral("QToolButton:checked {border: 1px solid #22D3EE; background: #18181B;}"));
                    QPointer<VncPane> guarded(currentVnc);
                    QObject::connect(button, &QToolButton::clicked, this, [this,guarded,index]() {
                        if (guarded && guarded->selectMonitor(index)) refreshVisualState();
                    });
                    monitorLayout_->addWidget(button);
                };
                // The entire desktop remains available even with individual monitors.
                createButton(-1, QStringLiteral("Escritorio VNC completo"), selected == -1);
                const auto& screens = currentVnc->monitors();
                for (int i = 0; i < screens.size(); ++i) {
                    const auto& m = screens.at(i);
                    createButton(i, QStringLiteral("Monitor %1 · %2x%3 · RFB ID %4")
                        .arg(i+1).arg(m.geometry.width()).arg(m.geometry.height()).arg(m.id), selected == i);
                }
            }
        }
    }
    if (stopTool_) stopTool_->setEnabled(compact);
    if(vaultReady_ && adminStack_ && tabs_->currentIndex()==0 &&
       adminStack_->currentIndex()==kDashboard) {
        QHash<QString,bool> snapshot;
        for(const auto& profile:profiles_){
            auto* sub=savedSubtab(profile.id);
            if(!sub)continue;
            for(int i=0;i<sub->count();++i){
                auto* pane=sub->widget(i);
                if(auto* ssh=dynamic_cast<SshPane*>(pane))snapshot[profile.id+QStringLiteral(":ssh")]=ssh->connected();
                else if(auto* vnc=dynamic_cast<VncPane*>(pane))snapshot[profile.id+QStringLiteral(":vnc")]=vnc->connected();
            }
        }
        if(snapshot!=dashboardStateSnapshot_){dashboardStateSnapshot_=snapshot;renderDashboard();}
    }
}

void MainWindow::connectSaved(const QString& id, int protocol) {
    if (!ensureVaultReady() || (protocol != kSsh && protocol != kVnc)) return;
    const auto profileIt = std::find_if(profiles_.cbegin(), profiles_.cend(),
        [&id](const ConnectProfile& p) { return p.id == id; });
    if (profileIt == profiles_.cend()) return;
    if ((protocol == kSsh && !profileIt->hasSsh) ||
        (protocol == kVnc && !profileIt->hasVnc)) return;
    if (auto* sub = savedSubtab(id)) {
        for (int i = 0; i < sub->count(); ++i) {
            if (protocol == kSsh) {
                auto* ssh = dynamic_cast<SshPane*>(sub->widget(i));
                if (ssh && ssh->connected()) {
                    openProfile(*profileIt, protocol);
                    return;
                }
            } else {
                auto* vnc = dynamic_cast<VncPane*>(sub->widget(i));
                if (vnc && vnc->connected()) {
                    openProfile(*profileIt, protocol);
                    return;
                }
            }
        }
    }
    // Only this worker receives the KDBX value. Never cache passwords inside
    // ConnectProfile, Dashboard or persistent pane callbacks.
    runStructure(QStringLiteral("Leyendo credencial de Secrets…"), [id,protocol]() {
        const ConnectStructures::Store store([](const QJsonObject& req) {
            return secretsRpc(req,10000);
        });
        ConnectStructures::Record record;
        QString error;
        if (!store.get(QStringLiteral("myconnect"),id,record,&error))
            return QJsonObject{{QStringLiteral("error"),error}};
        const QString prefix = protocol == kSsh ? QStringLiteral("ssh") : QStringLiteral("vnc");
        if (!record.value.contains(prefix + QStringLiteral("-name")))
            return QJsonObject{{QStringLiteral("error"),QStringLiteral("protocol_not_configured")}};
        return QJsonObject{{QStringLiteral("ok"),true},
            {QStringLiteral("password"),record.value.value(prefix + QStringLiteral("-pass"))},
            {QStringLiteral("host"),record.value.value(QStringLiteral("ip"))}};
    }, [this,id,protocol](const QJsonObject& answer) {
        if (!answer.value(QStringLiteral("ok")).toBool()) {
            QMessageBox::warning(this,QStringLiteral("Secrets"),
                QStringLiteral("No se pudo recuperar la credencial: %1")
                    .arg(answer.value(QStringLiteral("error")).toString()));
            return;
        }
        const auto it = std::find_if(profiles_.cbegin(),profiles_.cend(),
            [&id](const ConnectProfile& p) { return p.id == id; });
        if (it == profiles_.cend() || it->host != answer.value(QStringLiteral("host")).toString()) {
            statusBar()->showMessage(QStringLiteral("Configuración cambió; vuelve a abrir My Connect"),7000);
            return;
        }
        QString password=answer.value(QStringLiteral("password")).toString();
        openProfile(*it,protocol,password);
        password.fill(QChar('\0'));
        password.clear();
    });
}

void MainWindow::openProfile(const ConnectProfile& profile, int protocol, const QString& password) {
    if (!profile.id.startsWith(QStringLiteral("quick:"))) {
        if (!ensureVaultReady()) return;
        QTabWidget* sub=savedSubtab(profile.id);
        if (!sub) {buildSavedTab(profile);sub=savedSubtab(profile.id);}
        if (!sub) return;
        for(int i=1;i<tabs_->count();++i)
            if(tabs_->tabBar()->tabData(i).toString()==profile.id){tabs_->setCurrentIndex(i);break;}
        for(int i=0;i<sub->count();++i){
            auto* pane=sub->widget(i);
            if (protocol==kSsh && dynamic_cast<SshPane*>(pane)) {
                sub->setCurrentIndex(i);
                QPointer<SshPane> guarded(dynamic_cast<SshPane*>(pane));
                QString secret=password;
                QTimer::singleShot(0,sub,[guarded,secret=std::move(secret)]() mutable {
                    if (guarded) guarded->connectNow(secret);
                    secret.fill(QChar('\0'));
                });
                return;
            }
            if (protocol==kVnc && dynamic_cast<VncPane*>(pane)) {
                sub->setCurrentIndex(i);
                QPointer<VncPane> guarded(dynamic_cast<VncPane*>(pane));
                QString secret=password;
                QTimer::singleShot(0,sub,[guarded,secret=std::move(secret)]() mutable {
                    if (guarded) guarded->connectNow(secret);
                    secret.fill(QChar('\0'));
                });
                return;
            }
        }
        return;
    }
    for (int i = 1; i < tabs_->count(); ++i) if (tabs_->tabBar()->tabData(i).toString() == profile.id) {
        tabs_->setTabText(i, profile.name);
        tabs_->setCurrentIndex(i);
        if (protocol >= 0) {
            auto* sub = qobject_cast<QTabWidget*>(tabs_->widget(i));
            if (sub && protocol < sub->count()) {
                if (profile.id.startsWith(QStringLiteral("quick:")) && protocol == kSsh) {
                    // The previous pane may contain another protocol selection's port/user.
                    QWidget* previous = sub->widget(kSsh);
                    auto* fresh = new SshPane(profile.host, profile.sshPort, profile.sshUser, sub);
                    sub->removeTab(kSsh);
                    delete previous;
                    sub->insertTab(kSsh, fresh, QStringLiteral("SSH"));
                    QPointer<SshPane> pane(fresh);
                    QString secret = password;
                    QTimer::singleShot(0, fresh, [pane, secret=std::move(secret)]() mutable {
                        if (pane) pane->connectNow(secret);
                        secret.fill(QChar('\0'));
                    });
                } else if (profile.id.startsWith(QStringLiteral("quick:")) && protocol == kVnc) {
                    QWidget* previous = sub->widget(kVnc);
                    auto* fresh = new VncPane(profile.host, profile.vncPort, sub);
                    sub->removeTab(kVnc);
                    delete previous;
                    sub->insertTab(kVnc, fresh, QStringLiteral("VNC"));
                    QPointer<VncPane> pane(fresh);
                    QString secret = password;
                    QTimer::singleShot(0, fresh, [pane, secret=std::move(secret)]() mutable {
                        if (pane) pane->connectNow(secret);
                        secret.fill(QChar('\0'));
                    });
                } else if (protocol == kVnc) {
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
                sub->setCurrentIndex(protocol);
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
    {
        // UI-only placeholders for both Quick and My; no backend transport is started.
        for (const auto& protocolName : {QStringLiteral("VS Code"),
                                         QStringLiteral("Sunshine / Moonlight")}) {
            auto* placeholder = new QWidget(sub);
            auto* layout = new QVBoxLayout(placeholder);
            auto* label = new QLabel(
                QStringLiteral("%1: interfaz preparada; motor de conexión pendiente.").arg(protocolName),
                placeholder);
            label->setAlignment(Qt::AlignCenter);
            layout->addWidget(label);
            sub->addTab(placeholder, protocolName);
        }
    }
    if (!profile.id.startsWith(QStringLiteral("quick:"))) {
        ssh->setAccessGuard([this]() { return ensureVaultReady(); });
        vnc->setAccessGuard([this]() { return ensureVaultReady(); });
        ssh->setConnectRequestHandler([this,id=profile.id]() { connectSaved(id,kSsh); });
        vnc->setConnectRequestHandler([this,id=profile.id]() { connectSaved(id,kVnc); });
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
    if (!tabs_->tabBar()->tabData(index).toString().startsWith(QStringLiteral("quick:"))) return;
    if (tabs_->tabBar()->tabData(index).toString() == quickSessionId_) {
        quickSessionId_.clear();
        quickSessionHost_.clear();
    }
    QWidget* content = tabs_->widget(index);
    tabs_->removeTab(index);
    delete content; // Desconecta SSH y VNC del nodo, sin afectar otros nodos.
    refreshSaved();
}


bool MainWindow::loadProfiles() {
    if (!vaultReady_) return false;
    const ConnectStructures::Store store([](const QJsonObject& request) {
        return secretsRpc(request, 10000);
    });
    QVector<ConnectStructures::Record> equipment;
    QVector<ConnectStructures::Record> vsCode;
    QString error;
    if (!store.list(QStringLiteral("myconnect"),equipment,&error) ||
        !store.list(QStringLiteral("vscode"),vsCode,&error)) return false;
    QVector<ConnectProfile> loaded;
    QHash<QString,int> counts;
    for (const auto& record : equipment) {
        ConnectProfile profile;
        profile.id=record.id;
        profile.name=record.value.value(QStringLiteral("name")).toString();
        profile.host=record.value.value(QStringLiteral("ip")).toString();
        profile.generalUser=record.value.value(QStringLiteral("user")).toString();
        profile.hasSsh=record.value.contains(QStringLiteral("ssh-name"));
        profile.hasVnc=record.value.contains(QStringLiteral("vnc-name"));
        profile.hasSunshine=record.value.contains(QStringLiteral("sunshine-name"));
        profile.sshUser=record.value.value(QStringLiteral("ssh-user")).toString();
        profile.sshPort=record.value.value(QStringLiteral("ssh-port")).toInt(22);
        profile.vncPort=record.value.value(QStringLiteral("vnc-port")).toInt(5900);
        if (profile.id.isEmpty() || profile.name.isEmpty() || !validHost(profile.host)) return false;
        loaded.append(profile); // Never cache any -pass field in profiles_.
    }
    for (const auto& record : vsCode) {
        const QString owner=record.value.value(QStringLiteral("equipment")).toString();
        if (owner.isEmpty()) return false;
        counts[owner]++;
    }
    if (!ensureVaultReady()) return false;
    profiles_=std::move(loaded);
    vsCodeCounts_=std::move(counts);
    syncSavedTabs();
    renderDashboard();
    return true;
}
