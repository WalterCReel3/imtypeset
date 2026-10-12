"""The parts of the Python distribution that need no library and no wheel:
the version line, what the import gate compares (content, not paths), what it
refuses and in which words, and which gate runs where.

    python3 -I -B tools/ci/test_wheel.py [-v]

What needs a built wheel and an environment to install it into is
tools/wheel/check_installed.py. The Mach-O cases here are run on a file made
by hand from the format as remembered: they show that the reader agrees with
itself, not that it reads what Apple's linker writes.
"""

import importlib.util
import json
import os
import platform
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
sys.path.insert(0, os.path.join(REPO, "tools", "wheel"))

import build_wheel  # noqa: E402
import version as imtypeset_version  # noqa: E402

_spec = importlib.util.spec_from_file_location(
    "_bundle", os.path.join(REPO, "python", "imtypeset", "_bundle.py"))
gate = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gate)


def macho(text: bytes, signature: bytes = b"", linkedit_size: int = 64) -> bytes:
    """A 64-bit Mach-O with one __TEXT,__text section, a zero-fill section, a
    __LINKEDIT segment, LC_UUID and LC_CODE_SIGNATURE."""
    def segment(name, fileoff, filesize, sections=b"", count=0):
        body = struct.pack("<16sQQQQiiII", name, 0x1000, 0x4000, fileoff, filesize, 7, 5, count, 0)
        return struct.pack("<II", 0x19, 8 + len(body) + len(sections)) + body + sections

    def section(sectname, segname, size, offset, flags):
        return struct.pack("<16s16sQQIIIIIIII", sectname, segname, 0x1000, size, offset, 2, 0, 0, flags, 0, 0, 0)

    data_at = 4096
    sections = section(b"__text", b"__TEXT", len(text), data_at, 0x80000400) + \
        section(b"__bss", b"__DATA", 4096, 0, 0x1)
    commands = [
        segment(b"__TEXT", 0, data_at + len(text), sections, 2),
        segment(b"__LINKEDIT", data_at + len(text), linkedit_size),
        struct.pack("<II16s", 0x1B, 24, bytes(range(16))),
        struct.pack("<IIII", 0x1D, 16, data_at + len(text), len(signature)),
    ]
    blob = b"".join(commands)
    header = struct.pack("<IiiIIIII", 0xFEEDFACF, 0x0100000C, 0, 6, len(commands), len(blob), 0, 0)
    return (header + blob).ljust(data_at, b"\0") + text + signature


class Content(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="imtypeset-test-")
        self.addCleanup(shutil.rmtree, self.dir, True)

    def write(self, name: str, data: bytes) -> str:
        path = os.path.join(self.dir, name)
        with open(path, "wb") as handle:
            handle.write(data)
        return path

    @unittest.skipUnless(sys.platform.startswith("linux"), "needs an ELF file")
    def test_elf_what_is_mapped(self):
        import _ctypes

        with open(_ctypes.__file__, "rb") as handle:
            data = handle.read()
        whole = self.write("a.so", data)
        digest = gate.image_sha256(whole)
        self.assertTrue(digest.startswith("elf-load:"))
        # Bytes after everything that is mapped are not part of it.
        self.assertEqual(gate.image_sha256(self.write("b.so", data + b"appended")), digest)
        self.assertNotEqual(gate.sha256_file(os.path.join(self.dir, "b.so")), gate.sha256_file(whole))
        # A byte inside the first segment is; so is a cut through it.
        changed = bytearray(data)
        changed[0x200] ^= 0x01
        self.assertNotEqual(gate.image_sha256(self.write("c.so", bytes(changed))), digest)
        self.assertIsNone(gate.image_sha256(self.write("d.so", data[:len(data) // 3])))
        self.assertIsNone(gate.image_sha256(self.write("e.so", data[:40])))
        self.assertIsNone(gate.image_sha256(self.write("f.so", b"")))
        # The linker's id survives all of those, which is why it accepts nothing.
        self.assertEqual(gate.image_id(os.path.join(self.dir, "c.so")), gate.image_id(whole))
        self.assertEqual(gate.same_content(gate.identity(whole), os.path.join(self.dir, "b.so")), "image")
        self.assertEqual(gate.same_content(gate.identity(whole), whole), "sha256")
        self.assertIsNone(gate.same_content(gate.identity(whole), os.path.join(self.dir, "c.so")))
        self.assertIsNone(gate.same_content(gate.identity(whole), os.path.join(self.dir, "missing.so")))

    def test_macho_made_by_hand(self):
        text = bytes(range(256)) * 8
        plain = self.write("a.dylib", macho(text))
        digest = gate.image_sha256(plain)
        self.assertTrue(digest.startswith("macho-sections:"))
        self.assertEqual(gate.image_id(plain), "macho-uuid:" + bytes(range(16)).hex())
        # A signature added at the end, and the load commands that describe it.
        signed = self.write("b.dylib", macho(text, signature=b"S" * 300, linkedit_size=364))
        self.assertEqual(gate.image_sha256(signed), digest)
        self.assertNotEqual(gate.sha256_file(signed), gate.sha256_file(plain))
        self.assertEqual(gate.same_content(gate.identity(plain), signed), "image")
        other = bytearray(text)
        other[100] ^= 0x10
        self.assertNotEqual(gate.image_sha256(self.write("c.dylib", macho(bytes(other)))), digest)
        self.assertIsNone(gate.image_sha256(self.write("d.dylib", macho(text)[:4096 + 100])))
        self.assertIsNone(gate.image_sha256(self.write("e.dylib", macho(text)[:20])))

    def test_not_an_image(self):
        self.assertIsNone(gate.image_sha256(self.write("x", b"neither one nor the other" * 10)))
        self.assertIsNone(gate.image_id(self.write("y", b"\x7fELF")))
        self.assertIsNone(gate.image_sha256(os.path.join(self.dir, "missing")))


class Refusals(unittest.TestCase):
    def test_a_refusal_is_a_plain_import_error_with_a_sentence_first(self):
        error = gate.refusal("damaged", "Install it again.", "'/a/path': why")
        self.assertIs(type(error), ImportError)
        self.assertEqual(str(error).splitlines(), ["imtypeset: Install it again.", "'/a/path': why"])
        self.assertEqual((error.kind, error.sentence, error.detail, error.name),
                         ("damaged", "Install it again.", "'/a/path': why", "imtypeset"))

    def with_environment(self, **values):
        saved = {name: os.environ.get(name) for name in values}
        frozen = getattr(sys, "frozen", None)

        def restore():
            for name, value in saved.items():
                os.environ.pop(name, None) if value is None else os.environ.__setitem__(name, value)
            if frozen is None and hasattr(sys, "frozen"):
                del sys.frozen

        self.addCleanup(restore)
        for name, value in values.items():
            os.environ.pop(name, None) if value is None else os.environ.__setitem__(name, value)

    def test_which_gate_runs(self):
        self.with_environment(IMTYPESET_GATE=None)
        self.assertEqual(gate.mode("installed", True), "attested")
        self.assertEqual(gate.mode("IMTYPESET_LIBRARY", True), "attested")
        self.assertEqual(gate.mode("IMTYPESET_LIBRARY", False), "probe")
        with self.assertRaises(ImportError) as caught:
            gate.mode("installed", False)  # an installed package never starts an interpreter
        self.assertEqual(caught.exception.kind, "unchecked")
        os.environ["IMTYPESET_GATE"] = "probe"
        self.assertEqual(gate.mode("installed", True), "probe")
        os.environ["IMTYPESET_GATE"] = "attested"
        with self.assertRaises(ImportError) as caught:
            gate.mode("IMTYPESET_LIBRARY", False)
        self.assertEqual(caught.exception.kind, "unchecked")
        os.environ["IMTYPESET_GATE"] = "sometimes"
        with self.assertRaises(ImportError) as caught:
            gate.mode("installed", True)
        self.assertEqual(caught.exception.kind, "configuration")

    def test_a_frozen_application_never_gets_the_probe(self):
        self.with_environment(IMTYPESET_GATE=None)
        sys.frozen = True
        self.assertEqual(gate.mode("installed", True), "attested")
        for route in ("installed", "IMTYPESET_LIBRARY"):
            with self.assertRaises(ImportError) as caught:
                gate.mode(route, False)
            self.assertEqual(caught.exception.kind, "unchecked")
        os.environ["IMTYPESET_GATE"] = "probe"
        with self.assertRaises(ImportError) as caught:
            gate.mode("installed", True)
        self.assertEqual(caught.exception.kind, "configuration")

    def test_precheck_before_anything_is_loaded(self):
        self.with_environment(IMTYPESET_GATE=None)
        directory = tempfile.mkdtemp(prefix="imtypeset-test-")
        self.addCleanup(shutil.rmtree, directory, True)
        library = os.path.join(directory, "libimtypeset_bundle.so")
        with open(library, "wb") as handle:
            handle.write(b"not a library at all, and never loaded by this test")
        attestation = os.path.join(directory, "_attestation.json")

        def refused(document, route="installed"):
            with open(attestation, "w", encoding="utf-8") as handle:
                handle.write(document if isinstance(document, str) else json.dumps(document))
            with self.assertRaises(ImportError) as caught:
                gate.precheck(library, route)
            first = str(caught.exception).splitlines()[0]
            self.assertNotIn(os.sep, first)
            return caught.exception.kind

        good = {
            "schema": 1, "imtypeset_version": "0.3.0", "wheel_version": "0.0.1",
            "library": gate.identity(library), "module": {"sha256": "0" * 64},
            "platform": {"system": platform.system(), "machine": platform.machine()},
            "accepted_by": "probe", "probes": [["a", True, ""]], "headers": "fork:x",
        }
        self.assertEqual(refused("{"), "damaged")
        self.assertEqual(refused({"schema": 2}), "damaged")
        self.assertEqual(refused(dict(good, schema=7)), "damaged")
        self.assertEqual(refused(dict(good, platform={"system": "Plan9", "machine": "pdp11"})), "wrong-platform")
        self.assertEqual(refused(dict(good, library={"sha256": "1" * 64})), "damaged")
        # The library is the attested one; the imgui-bundle is not, or is absent.
        self.assertIn(refused(good), ("bundle-mismatch", "bundle-missing"))
        os.remove(attestation)
        with self.assertRaises(ImportError) as caught:
            gate.precheck(library, "installed")
        self.assertEqual(caught.exception.kind, "unchecked")
        self.assertIsNone(gate.precheck(library, "IMTYPESET_LIBRARY"))  # the probe decides, later


class Versions(unittest.TestCase):
    def test_one_number(self):
        self.assertEqual(imtypeset_version.problems(), [])
        self.assertRegex(imtypeset_version.version(), r"^\d+\.\d+\.\d+$")

    def test_the_bundle_line_orders_as_the_versions_do(self):
        self.assertEqual(build_wheel.bundle_line("1.92.900"), "192900")
        self.assertEqual(build_wheel.bundle_line("1.92.801"), "192801")
        lines = [int(build_wheel.bundle_line(v)) for v in ("1.92.5", "1.92.801", "1.92.900", "1.93.0", "2.0.0")]
        self.assertEqual(lines, sorted(lines))
        self.assertEqual(build_wheel.distribution_version("0.3.0", "1.92.900", False), "0.3.0.192900")
        self.assertEqual(build_wheel.distribution_version("0.3.0", "1.92.900", True), "0.3.0.192900+standin")
        for bad in ("1.92", "1.92.900.1", "1.192.0", "1.92.1000", "v1.92.900"):
            with self.assertRaises(build_wheel.Failed):
                build_wheel.bundle_line(bad)

    def test_a_release_is_never_stand_in(self):
        with self.assertRaises(build_wheel.Failed):
            build_wheel.build(sys.executable, tempfile.gettempdir(), headers="stand-in", release=True)

    def test_the_archive_is_a_function_of_its_inputs(self):
        directory = tempfile.mkdtemp(prefix="imtypeset-test-")
        self.addCleanup(shutil.rmtree, directory, True)
        files = [("imtypeset/b.py", b"b", False), ("imtypeset/a.so", b"a", True),
                 ("imtypeset-1.dist-info/RECORD", b"", False)]
        build_wheel.write_wheel(os.path.join(directory, "one.whl"), files)
        build_wheel.write_wheel(os.path.join(directory, "two.whl"), list(reversed(files)))
        with open(os.path.join(directory, "one.whl"), "rb") as one, \
                open(os.path.join(directory, "two.whl"), "rb") as two:
            self.assertEqual(one.read(), two.read())


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1)
