from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


class QuickFourProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = read("ui/desktop/MainWindow.cpp")
        cls.header = read("ui/desktop/MainWindow.h")

    def test_selector_retains_existing_order_and_adds_future_protocols(self):
        self.assertRegex(self.source, r'quickProtocol_->addItems\(\{QStringLiteral\("VNC"\), QStringLiteral\("SSH"\),\s*QStringLiteral\("VS Code"\), QStringLiteral\("Sunshine / Moonlight"\)\}\)')
        self.assertIn('constexpr int kVsCode = 2;', self.source)
        self.assertIn('constexpr int kSunshine = 3;', self.source)

    def test_enter_and_click_use_the_same_validated_action(self):
        self.assertIn('&QLineEdit::returnPressed, quickAction_, &QPushButton::click', self.source)
        self.assertIn('&QShortcut::activated, quickAction_, &QPushButton::click', self.source)
        self.assertIn('&QPushButton::clicked, this, [this]() { openQuick(); }', self.source)
        self.assertEqual(self.source.count('void MainWindow::openQuick()'), 1)

    def test_required_fields_and_per_protocol_ports(self):
        self.assertIn('name.isEmpty() || !validHost(host)', self.source)
        self.assertIn('selection == 1 && quickUser_->text().trimmed().isEmpty()', self.source)
        self.assertIn('selection != 3 && quickPort_->value() == 0', self.source)
        self.assertIn('selection == 2 && quickInstanceName_->text().trimmed().isEmpty()', self.source)
        self.assertIn('quickDrafts_[0].port = 5900;', self.source)
        self.assertIn('quickDrafts_[1].port = 22;', self.source)
        self.assertIn('quickDrafts_[2].port = 0;', self.source)
        self.assertIn('quickDrafts_[3].port = 0;', self.source)

    def test_passwords_stay_ephemeral_and_are_not_cross_shared(self):
        self.assertIn('std::array<QuickDraft, 4> quickDrafts_', self.header)
        self.assertIn('draft.password = quickPassword_->text();', self.source)
        self.assertIn('quickPassword_->setText(draft.password);', self.source)
        self.assertIn("quickDrafts_[selection].password.fill(QChar('\\0'))", self.source)
        quick_section = self.source.split('void MainWindow::saveQuickDraft()')[1].split('void MainWindow::buildEditor()')[0]
        self.assertNotIn('secretsRpc(', quick_section)
        self.assertNotIn('QSettings', quick_section)

    def test_existing_quick_host_reuses_tab_but_rebuilds_target_pane(self):
        self.assertIn('quickSessionId_', self.header)
        self.assertIn('quickSessionHost_ != host', self.source)
        self.assertIn('new SshPane(profile.host, profile.sshPort, profile.sshUser, sub)', self.source)
        self.assertIn('new VncPane(profile.host, profile.vncPort, sub)', self.source)
        self.assertIn('sub->insertTab(kSsh, fresh, QStringLiteral("SSH"))', self.source)
        self.assertIn('sub->insertTab(kVnc, fresh, QStringLiteral("VNC"))', self.source)

    def test_future_panes_are_explicitly_nonfunctional(self):
        self.assertIn('interfaz preparada; motor de conexión pendiente.', self.source)
        self.assertIn('sub->addTab(placeholder, protocolName);', self.source)
        self.assertIn('protocol == kSsh', self.source)
        self.assertIn('protocol == kVnc', self.source)
        self.assertNotIn('VSCodePane', self.source)
        self.assertNotIn('SunshinePane', self.source)

    def test_quick_is_independent_of_structured_secrets_storage(self):
        store = read("ui/desktop/ConnectStructures.cpp")
        for op in ("structure.get", "structure.list", "structure.put",
                   "structure.delete"):
            self.assertIn(f'StringLiteral("{op}")', store)
        self.assertIn('if (!profile.id.startsWith(QStringLiteral("quick:"))) {', self.source)
        self.assertIn('void MainWindow::closeSavedSessions()', self.source)
        self.assertIn('if (!tabs_->tabBar()->tabData(i).toString().startsWith(QStringLiteral("quick:")))', self.source)


if __name__ == '__main__':
    unittest.main()
