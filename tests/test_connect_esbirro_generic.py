"""Regressions for the permanent, version-neutral NEEBLES Connect Esbirro builder.

No DEB extraction, compilation, network access or installation occurs here.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest import TestCase, mock
import importlib.util
import json

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "tooling/esbirro/build_connect.py"
PROFILE = ROOT / "tooling/esbirro/connect-build-profile.json"
SPEC = importlib.util.spec_from_file_location("connect_generic_esbirro", RECIPE)
engine = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(engine)


class GenericBuildContractTests(TestCase):
    def _metadata(self, base: Path, version: str, cargo_version: str) -> Path:
        (base / "runtime").mkdir()
        (base / "manifest.json").write_text(
            json.dumps({"name": "neebles-connect", "version": version}),
            encoding="utf-8",
        )
        (base / "runtime/Cargo.toml").write_text(
            '[package]\nname = "neebles-connect-runtime"\n'
            f'version = "{cargo_version}"\nedition = "2021"\n',
            encoding="utf-8",
        )
        return base

    def test_release_versions_are_derived_from_metadata(self):
        for version in ("1.0.7", "1.0.8", "2.0.0", "2.1.3-rc.1"):
            with self.subTest(version=version), TemporaryDirectory() as temp:
                path = self._metadata(Path(temp), version, version)
                self.assertEqual(engine.release_version(path), version)

    def test_mismatched_manifest_and_cargo_fail_closed(self):
        with TemporaryDirectory() as temp:
            path = self._metadata(Path(temp), "1.0.8", "1.0.7")
            with self.assertRaisesRegex(RuntimeError, "version mismatch"):
                engine.release_version(path)

    def test_no_hardcoded_connect_release_in_engine_or_profile(self):
        source = RECIPE.read_text(encoding="utf-8")
        profile = json.loads(PROFILE.read_text(encoding="utf-8"))
        self.assertNotIn("CONNECT_107_CANDIDATOS", source)
        self.assertNotIn("Connect 1.0.7:", source)
        self.assertNotIn("version", profile)
        self.assertEqual(profile["schema"], 1)
        self.assertEqual(profile["module"], "neebles-connect")
        self.assertIn("git('ls-files','-z',binary=True)", source)

    def test_profile_loads_qt_gcc_and_real_package_pins(self):
        self.assertEqual(engine.QT_VERSION, "6.8.2")
        self.assertEqual(engine.GCC_MAJOR, 14)
        self.assertEqual(engine.PROFILE_SYSROOT, "build_sysroot_6.8.2")
        self.assertEqual(
            engine.PROXY_ELFS["usr/lib/x86_64-linux-gnu/libproxy.so.0.5.9"],
            "88a785edfa73af6727fcea089e008cbfbb253cd77c2532d7577e8f2c4cafc805",
        )
        self.assertEqual(engine.Q08D_DEB_SHA, engine.DEBS["libvncclient1"][1])

    def test_bad_profile_relative_path_denied(self):
        for path in ("/etc/passwd", "../outside", "abc/../xyz", ""):
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                engine._rel(path)

    def test_bad_sha_pin_denied(self):
        for value in ("0" * 63, "z" * 64, "", "deadbeef"):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                engine._pin(value)

    def test_untrusted_runpaths_still_denied(self):
        fake = SimpleNamespace(
            stdout="0x000000000000001d (RUNPATH) Library runpath: [/tmp/untrusted]"
        )
        with mock.patch.object(engine.subprocess, "run", return_value=fake):
            with self.assertRaisesRegex(RuntimeError, "Unapproved ELF RUNPATH"):
                engine._needed(Path("/tmp/fake"), Path("/tmp/sysroot"))

    def test_rpath_still_denied(self):
        fake = SimpleNamespace(
            stdout=(
                "0x000000000000000f (RPATH) Library rpath: "
                "[/usr/lib/x86_64-linux-gnu/libproxy]"
            )
        )
        with mock.patch.object(engine.subprocess, "run", return_value=fake):
            with self.assertRaisesRegex(RuntimeError, "DT_RPATH not authorized"):
                engine._needed(Path("/tmp/fake"), Path("/tmp/sysroot"))
