"""C64F: fail-closed tests for the narrow authenticated Esbirro libproxy RUNPATH.

The positive physical DEB/SHA and recursive-closure gate runs on the Ryzen.
These unit tests deliberately never authorize unverified ELF material.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest import TestCase, mock
import importlib.util

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "tooling/esbirro/build_connect_107.py"
SPEC = importlib.util.spec_from_file_location("build_connect_107_c64f", RECIPE)
connect_107 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(connect_107)


def fake_dynamic(text):
    return SimpleNamespace(stdout=text)


class CertifiedRunpathContractTests(TestCase):
    def read(self, path, dynamic, sysroot=None):
        with mock.patch.object(
            connect_107.subprocess, "run",
            return_value=fake_dynamic(dynamic),
        ):
            return connect_107._needed(path, sysroot)

    def test_regular_elf_without_runpath(self):
        needed, paths = self.read(
            Path("/untrusted/placeholder"),
            "0x01 (NEEDED) Shared library: [libQt6Widgets.so.6]",
        )
        self.assertEqual(needed, ["libQt6Widgets.so.6"])
        self.assertEqual(paths, [])

    def test_arbitrary_absolute_runpath_denied(self):
        dynamic = (
            "0x1d (RUNPATH) Library runpath: "
            "[/usr/lib/x86_64-linux-gnu]"
        )
        with self.assertRaisesRegex(RuntimeError, "Unapproved ELF RUNPATH"):
            self.read(Path("/untrusted/file"), dynamic, Path("/fake/sysroot"))

    def test_rpath_denied_even_on_proxy(self):
        dynamic = (
            "0x0f (RPATH) Library rpath: "
            "[/usr/lib/x86_64-linux-gnu/libproxy]"
        )
        with self.assertRaisesRegex(RuntimeError, "DT_RPATH not authorized"):
            self.read(Path("/untrusted/file"), dynamic, Path("/fake/sysroot"))

    def test_literal_runpath_without_trusted_sysroot_denied(self):
        dynamic = (
            "0x1d (RUNPATH) Library runpath: "
            "[/usr/lib/x86_64-linux-gnu/libproxy]"
        )
        with self.assertRaisesRegex(RuntimeError, "Unapproved ELF RUNPATH"):
            self.read(Path("/untrusted/file"), dynamic)

    def test_literal_runpath_on_wrong_elf_denied(self):
        dynamic = (
            "0x1d (RUNPATH) Library runpath: "
            "[/usr/lib/x86_64-linux-gnu/libproxy]"
        )
        with TemporaryDirectory() as temp:
            sysroot = Path(temp)
            fake = sysroot / "usr/lib/x86_64-linux-gnu/libother.so"
            fake.parent.mkdir(parents=True)
            fake.write_bytes(b"untrusted")
            with self.assertRaisesRegex(
                RuntimeError, "RUNPATH ELF identity not certified"
            ):
                self.read(fake, dynamic, sysroot)

    def test_literal_runpath_on_tampered_proxy_denied(self):
        dynamic = (
            "0x1d (RUNPATH) Library runpath: "
            "[/usr/lib/x86_64-linux-gnu/libproxy]"
        )
        with TemporaryDirectory() as temp:
            sysroot = Path(temp)
            fake = sysroot / "usr/lib/x86_64-linux-gnu/libproxy.so.0.5.9"
            fake.parent.mkdir(parents=True)
            fake.write_bytes(b"tampered libproxy is not trusted")
            with self.assertRaisesRegex(
                RuntimeError, "RUNPATH ELF identity not certified"
            ):
                self.read(fake, dynamic, sysroot)
