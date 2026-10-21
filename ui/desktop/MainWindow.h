#pragma once

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <QJsonObject>

class QTabWidget;
class QStackedWidget;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QComboBox;
class QVBoxLayout;
class QWidget;
class QLabel;
class VncPane;

struct ConnectProfile {
    QString id;
    QString name;
    QString host;
    QString sshUser;
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
    void toggleSidebar();
    void buildDashboard();
    void buildQuick();
    void buildEditor();
    void buildSaved();
    void refreshSaved();
    void refreshSecretsAccess();
    void requestVault();
    void saveProfile();
    void editProfile(const ConnectProfile&);
    void deleteProfile(const ConnectProfile&);
    void openProfile(const ConnectProfile&, int protocol, const QString& password = {});
    void closeSavedSessions();
    bool ensureVaultReady();
    bool readSecret(const QString& id, QJsonObject& value);
    void clearSensitiveEditor();
    void openQuick();
    void closeTab(int index);
    bool isOpen(const QString& id) const;
    bool loadProfiles();
    void resetEditor();

    QString moduleRoot_;
    QString version_;
    QVector<ConnectProfile> profiles_;
    QString editingId_;
    QWidget* sidebar_ = nullptr;
    QWidget* sidebarContent_ = nullptr;
    QPushButton* sidebarToggle_ = nullptr;
    QPushButton* sidebarReveal_ = nullptr;
    QVector<QPushButton*> adminButtons_;
    QTabWidget* tabs_ = nullptr;
    QStackedWidget* adminStack_ = nullptr;
    QLineEdit* quickHost_ = nullptr;
    QLineEdit* quickUser_ = nullptr;
    QLineEdit* quickPassword_ = nullptr;
    QComboBox* quickProtocol_ = nullptr;
    QSpinBox* quickPort_ = nullptr;
    QLineEdit* editName_ = nullptr;
    QLineEdit* editHost_ = nullptr;
    QLineEdit* editSshUser_ = nullptr;
    QLineEdit* editPassword_ = nullptr;
    QComboBox* editProtocol_ = nullptr;
    QSpinBox* editSshPort_ = nullptr;
    QSpinBox* editVncPort_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QVBoxLayout* savedList_ = nullptr;
    QLabel* secretsState_ = nullptr;
    QPushButton* missingVault_ = nullptr;
    int vaultExists_ = -1; // -1 unknown, 0 missing, 1 exists
    bool vaultReady_ = false;
    bool vaultLoading_ = false;
};
