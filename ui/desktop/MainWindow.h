#pragma once

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QHash>
#include <QJsonArray>
#include <array>
#include <cstdint>
#include <functional>
#include <QPair>

class QTabWidget;
class QStackedWidget;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QComboBox;
class QVBoxLayout;
class QFormLayout;
class QWidget;
class QLabel;
class QToolButton;
class QScrollArea;
class VncPane;

struct ConnectProfile {
    QString id;
    QString name;
    QString host;
    QString generalUser;
    QString sshUser;
    bool hasSsh = false;
    bool hasVnc = false;
    bool hasSunshine = false;
    int defaultProtocol = 1; // 0 = SSH, 1 = VNC
    int sshPort = 22;
    int vncPort = 5900;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow(QString moduleRoot, QString version, QWidget* parent = nullptr);
private:
    void showAdmin(int view);
    void syncNavigation();
    void refreshVisualState();
    void syncSavedTabs();
    void buildSavedTab(const ConnectProfile& profile);
    QTabWidget* savedSubtab(const QString& profileId) const;
    bool activeConnection(QWidget** pane = nullptr, int* protocol = nullptr) const;
    void renderDashboard();
    bool loadDashboardLayout();
    bool saveDashboardLayout();
    bool runStructure(QString description, std::function<QJsonObject()> operation,
                      std::function<void(const QJsonObject&)> onFinish);
    QString groupFor(const QString& profileId) const;
    void moveDashboardRow(int row, int offset);
    void moveEquipmentToGroup(const QString& profileId, const QString& groupId);
    void addDashboardGroup();
    void renameDashboardGroup(int index);
    void setDashboardGroupColor(int index);
    void buildDashboard();
    void buildQuick();
    void buildEditor();
    void buildSaved();
    void refreshSaved();
    void refreshSecretsAccess();
    void applySecretsStatus(const QJsonObject& state);
    void requestVault();
    void refreshVaultDataAsync();
    bool applyVaultSnapshot(const QJsonObject& snapshot);
    void saveProfile();
    void editProfile(const ConnectProfile&);
    void deleteProfile(const ConnectProfile&);
    void openProfile(const ConnectProfile&, int protocol, const QString& password = {});
    void connectSaved(const QString& profileId, int protocol);
    void closeSavedSessions();
    bool ensureVaultReady();
    void configureFixedProtocol(const ConnectProfile&, int protocol);
    void manageVsCode(const ConnectProfile&);
    void clearSensitiveEditor();
    void openQuick();
    void saveQuickDraft();
    void selectQuickProtocol(int selection);
    void closeTab(int index);
    bool isOpen(const QString& id) const;
    bool loadProfiles();
    void resetEditor();

    QString moduleRoot_;
    QString version_;
    QVector<ConnectProfile> profiles_;
    QHash<QString,int> vsCodeCounts_;
    QHash<QString,QVector<QPair<QString,QString>>> vsCodeTabs_;
    QString editingId_;
    QWidget* sidebar_ = nullptr;
    QWidget* sidebarContent_ = nullptr;
    QWidget* brandLogo_ = nullptr;
    QWidget* sidebarEdition_ = nullptr;
    QVBoxLayout* sidebarLayout_ = nullptr;
    QToolButton* stopTool_ = nullptr;
    QToolButton* copyTool_ = nullptr;
    QWidget* monitorTools_ = nullptr;
    QVBoxLayout* monitorLayout_ = nullptr;
    QVector<QString> tabOrder_;
    QJsonArray dashboardRows_;
    QJsonArray dashboardCommittedRows_;
    QHash<QString,bool> dashboardStateSnapshot_;
    bool dashboardLayoutLoaded_ = false;
    bool syncingTabs_ = false;
    bool lastCompact_ = false;
    int visualProtocol_ = -1;
    VncPane* visualVncPane_ = nullptr; // Observation only; never owned.
    std::uint64_t visualVncRevision_ = 0;
    int visualMonitorSelection_ = -2;
    QVBoxLayout* dashboardList_ = nullptr;
    QComboBox* editGroup_ = nullptr;
    QVector<QPushButton*> adminButtons_;
    QTabWidget* tabs_ = nullptr;
    QStackedWidget* adminStack_ = nullptr;
    struct QuickDraft {
        QString user;
        QString password;
        QString instanceName;
        int port = 0;
    };
    std::array<QuickDraft, 4> quickDrafts_{};
    int quickSelectedProtocol_ = 0;
    QString quickSessionId_;
    QString quickSessionHost_;
    QFormLayout* quickForm_ = nullptr;
    QPushButton* quickAction_ = nullptr;
    QLineEdit* quickName_ = nullptr;
    QLineEdit* quickInstanceName_ = nullptr;
    QLineEdit* quickHost_ = nullptr;
    QLineEdit* quickUser_ = nullptr;
    QLineEdit* quickPassword_ = nullptr;
    QComboBox* quickProtocol_ = nullptr;
    QSpinBox* quickPort_ = nullptr;
    QLineEdit* editName_ = nullptr;
    QLineEdit* editHost_ = nullptr;
    QLineEdit* editSshUser_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QVBoxLayout* savedList_ = nullptr;
    QLabel* secretsState_ = nullptr;
    QPushButton* missingVault_ = nullptr;
    int vaultExists_ = -1; // -1 unknown, 0 missing, 1 exists
    bool vaultReady_ = false;
    bool vaultLoading_ = false;
    bool secretsPollInFlight_ = false;
    bool structureInFlight_ = false;
    quint64 vaultEpoch_ = 0;
};
