import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


class ContractScaffoldTests(unittest.TestCase):
    def test_manifest_identity(self):
        manifest = read("manifest.json")
        self.assertEqual(manifest["schema"], 4)
        self.assertEqual(manifest["name"], "neebles-connect")
        self.assertEqual(manifest["version"], "1.0.0")
        self.assertEqual(manifest["entrypoint"], "runtime/neebles-connect")
        self.assertNotIn("tray", manifest)
        self.assertNotIn("notifications", manifest)
        self.assertEqual(manifest["contracts"], [])

    def test_lifecycle_does_not_fake_runtime_open(self):
        data = read("lifecycle.json")
        self.assertNotIn("governor.open", data["hardcoded"])
        self.assertNotIn("open", data["transitions"])

    def test_surfaces_are_initially_empty(self):
        self.assertEqual(read("surfaces.json"), {"schema": 2, "items": {}})

    def test_contracts_are_not_advertised(self):
        data = read("contracts/commands.json")
        self.assertEqual(data["schema"], 1)
        self.assertEqual(data["contract"], "commands")
        self.assertEqual(data["endpoints"], {})

    def test_languages_have_equal_keys(self):
        lm = read("languages/manifest.json")
        self.assertEqual(lm["schema"], 1)
        self.assertEqual(lm["default"], "en_US")
        all_data = [read("languages/" + x["file"]) for x in lm["languages"]]
        self.assertEqual(set(all_data[0]), set(all_data[1]))
        self.assertEqual(len(all_data[0]), len(set(all_data[0])))

    def test_settings_do_not_claim_tray_running(self):
        data = read("settings/default.json")
        self.assertEqual(data["hardcoded"]["module"], "neebles-connect")
        self.assertEqual(data["ui"]["language"], "en_US")
        self.assertEqual(data["tray"]["visible"], "false")
        self.assertEqual(data["tray"]["autostart"], "false")

    def test_no_fake_critical_instructions(self):
        self.assertEqual(read("critical-update/manifest.json"), [])

    @unittest.skip("Runtime Rust/Qt no implementado: no certificar Open/Close")
    def test_runtime_registration_real(self):
        raise AssertionError("requires installed-system acceptance")


if __name__ == "__main__":
    unittest.main()
