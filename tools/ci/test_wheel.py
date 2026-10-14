"""The parts of the Python distribution that need no library and no wheel:
the version line, what the import gate compares (content, not paths), what it
refuses and in which words, and which gate runs where.

    python3 -I -B tools/ci/test_wheel.py [-v]

What needs a built wheel and an environment to install it into is
tools/wheel/check_installed.py. THE MACH-O CASES HERE ARE RUN ON A FILE MADE
BY HAND from the format as remembered (`macho()` below): they show that the
reader agrees with itself, not that it reads what Apple's linker writes, nor
that Apple's tools change only what `macho()`'s arguments change.
"""

import importlib
import importlib.util
import json
import os
import platform
import shutil
import struct
import subprocess
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


def macho(text: bytes, signature: bytes = b"", linkedit: bytes = b"L" * 64, rpaths=(),
          text_address: int = 0x1000) -> bytes:
    """A 64-bit Mach-O MADE BY HAND: one __TEXT,__text section, a zero-fill
    section, a __LINKEDIT segment holding `linkedit` and then `signature`,
    LC_SYMTAB into __LINKEDIT, LC_UUID, one LC_RPATH per entry of `rpaths`,
    and LC_CODE_SIGNATURE when there is a signature. Every offset and size in
    it is true of the bytes returned."""
    def segment(name, vmaddr, fileoff, filesize, sections=b"", count=0):
        body = struct.pack("<16sQQQQiiII", name, vmaddr, 0x4000, fileoff, filesize, 7, 5, count, 0)
        return struct.pack("<II", 0x19, 8 + len(body) + len(sections)) + body + sections

    def section(sectname, segname, addr, size, offset, flags):
        return struct.pack("<16s16sQQIIIIIIII", sectname, segname, addr, size, offset, 2, 0, 0, flags, 0, 0, 0)

    def rpath(path: bytes):
        body = path + b"\0" * (8 - len(path) % 8)
        return struct.pack("<III", 0x8000001C, 12 + len(body), 12) + body

    data_at = 4096
    linkedit_at = data_at + len(text)
    sections = section(b"__text", b"__TEXT", text_address, len(text), data_at, 0x80000400) + \
        section(b"__bss", b"__DATA", text_address + 0x2000, 4096, 0, 0x1)
    commands = [
        segment(b"__TEXT", text_address - 0x1000, 0, linkedit_at, sections, 2),
        segment(b"__LINKEDIT", 0x8000, linkedit_at, len(linkedit) + len(signature)),
        struct.pack("<IIIIII", 0x2, 24, linkedit_at, len(linkedit) // 32, linkedit_at + len(linkedit) // 2,
                    len(linkedit) - len(linkedit) // 2),
        struct.pack("<II16s", 0x1B, 24, bytes(range(16))),
    ] + [rpath(path) for path in rpaths]
    if signature:
        commands.append(struct.pack("<IIII", 0x1D, 16, linkedit_at + len(linkedit), len(signature)))
    blob = b"".join(commands)
    assert 32 + len(blob) <= data_at
    header = struct.pack("<IiiIIIII", 0xFEEDFACF, 0x0100000C, 0, 6, len(commands), len(blob), 0, 0)
    return (header + blob).ljust(data_at, b"\0") + text + linkedit + signature


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

    @unittest.skipUnless(sys.platform.startswith("linux"), "needs an ELF file")
    def test_elf_cut_at_its_tail(self):
        # The loader maps nothing of an ELF file's tail (the section headers),
        # so a file cut there would load. It is not the file that was
        # installed, and the file's own headers say so.
        import _ctypes

        with open(_ctypes.__file__, "rb") as handle:
            data = handle.read()
        recorded = gate.identity(self.write("a.so", data))
        (shoff,) = struct.unpack_from("<Q", data, 0x28)
        shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
        self.assertEqual(shoff + shentsize * shnum, len(data), "this test expects the table last")
        for cut in (1, shentsize, shentsize * shnum, shentsize * shnum + 1):
            path = self.write(f"cut-{cut}.so", data[:len(data) - cut])
            self.assertIsNone(gate.image_sha256(path), f"cut {cut} bytes from the tail")
            self.assertIsNone(gate.same_content(recorded, path))

    @unittest.skipUnless(sys.platform.startswith("linux"), "needs an ELF file")
    def test_elf_what_the_loader_is_told(self):
        # The program headers and the ELF header's fields the loader reads are
        # part of the hash; where the section headers are is not.
        import _ctypes

        with open(_ctypes.__file__, "rb") as handle:
            data = handle.read()
        digest = gate.image_sha256(self.write("a.so", data))
        (phoff,) = struct.unpack_from("<Q", data, 0x20)
        phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
        for name, at in (("e_entry", 0x18), ("e_flags", 0x30), ("p_flags of the first header", phoff + 4),
                         ("p_memsz of the last header", phoff + (phnum - 1) * phentsize + 0x28)):
            changed = bytearray(data)
            changed[at] ^= 0x01
            self.assertNotEqual(gate.image_sha256(self.write("b.so", bytes(changed))), digest, name)

    @unittest.skipUnless(sys.platform.startswith("linux") and shutil.which("strip") and shutil.which("cc"),
                         "needs cc and strip")
    def test_elf_stripped(self):
        # A library with debugging sections and a symbol table, compiled here,
        # and the same file after `strip` took them off.
        source = os.path.join(self.dir, "a.c")
        with open(source, "w", encoding="utf-8") as handle:
            handle.write("int imtypeset_test_value = 7;\nint imtypeset_test(void) { return imtypeset_test_value; }\n")
        whole = os.path.join(self.dir, "a.so")
        subprocess.run(["cc", "-shared", "-fPIC", "-g", "-o", whole, source], check=True)
        recorded = gate.identity(whole)
        for option in ("--strip-debug", "--strip-all"):
            stripped = os.path.join(self.dir, "b.so")
            shutil.copyfile(whole, stripped)
            subprocess.run(["strip", option, stripped], check=True)
            self.assertNotEqual(gate.sha256_file(stripped), recorded["sha256"], option)
            self.assertEqual(gate.same_content(recorded, stripped), "image", option)

    def test_macho_made_by_hand(self):
        text = bytes(range(256)) * 8
        plain = self.write("a.dylib", macho(text))
        digest = gate.image_sha256(plain)
        self.assertTrue(digest.startswith("macho-sections:"))
        self.assertEqual(gate.image_id(plain), "macho-uuid:" + bytes(range(16)).hex())
        # A signature added at the end, and the load commands that describe it.
        signed = self.write("b.dylib", macho(text, signature=b"S" * 300))
        self.assertEqual(gate.image_sha256(signed), digest)
        self.assertNotEqual(gate.sha256_file(signed), gate.sha256_file(plain))
        self.assertEqual(gate.same_content(gate.identity(plain), signed), "image")
        other = bytearray(text)
        other[100] ^= 0x10
        self.assertNotEqual(gate.image_sha256(self.write("c.dylib", macho(bytes(other)))), digest)
        self.assertIsNone(gate.image_sha256(self.write("d.dylib", macho(text)[:4096 + 100])))
        self.assertIsNone(gate.image_sha256(self.write("e.dylib", macho(text)[:20])))

    def test_macho_made_by_hand_cut_at_its_tail(self):
        # __LINKEDIT is the end of the file and no section is in it. A file
        # cut inside it keeps every section, and is refused by its length.
        text = bytes(range(256)) * 8
        for name, whole in (("unsigned", macho(text)), ("signed", macho(text, signature=b"S" * 300))):
            recorded = gate.identity(self.write("whole.dylib", whole))
            self.assertIsNotNone(recorded["image_sha256"])
            for cut in (1, 16, 63, 64, 200, 363):
                if cut >= len(whole) - 4096 - len(text):
                    continue
                path = self.write("cut.dylib", whole[:len(whole) - cut])
                self.assertIsNone(gate.image_sha256(path), f"{name}, cut {cut} bytes from the tail")
                self.assertIsNone(gate.same_content(recorded, path), f"{name}, cut {cut}")
        # The same for each table a load command places, taken one at a time
        # with the segment's own length left true.
        whole = bytearray(macho(text, signature=b"S" * 300))
        ncmds = struct.unpack_from("<I", whole, 16)[0]
        at, placed = 32, 0
        for _ in range(ncmds):
            command, size = struct.unpack_from("<II", whole, at)
            if command in (0x2, 0x1D):
                beyond = bytearray(whole)
                field = at + (12 if command == 0x2 else 12)  # nsyms; datasize
                struct.pack_into("<I", beyond, field, 1 << 20)
                self.assertIsNone(gate.image_sha256(self.write("beyond.dylib", bytes(beyond))), hex(command))
                placed += 1
            at += size
        self.assertEqual(placed, 2)

    def test_macho_made_by_hand_what_is_and_is_not_hashed(self):
        text = bytes(range(256)) * 8
        digest = gate.image_sha256(self.write("a.dylib", macho(text)))
        # Not hashed: run paths (install_name_tool), the signature, __LINKEDIT.
        for name, other in (
            ("a run path added", macho(text, rpaths=(b"@loader_path/../imgui_bundle",))),
            ("a run path added and signed", macho(text, rpaths=(b"@executable_path/../Frameworks",),
                                                   signature=b"S" * 500)),
            ("another __LINKEDIT", macho(text, linkedit=b"M" * 128)),
        ):
            self.assertEqual(gate.image_sha256(self.write("b.dylib", other)), digest, name)
        # Hashed: where a section is mapped, and its flags.
        self.assertNotEqual(gate.image_sha256(self.write("c.dylib", macho(text, text_address=0x3000))), digest)
        flags = bytearray(macho(text))
        at = 32 + 72 + 64  # the first section's flags
        self.assertEqual(struct.unpack_from("<I", flags, at)[0], 0x80000400)
        struct.pack_into("<I", flags, at, 0x80000000)
        self.assertNotEqual(gate.image_sha256(self.write("d.dylib", bytes(flags))), digest)

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
        self.assertEqual(gate.mode("IMTYPESET_LIBRARY", True), "probe")
        os.environ["IMTYPESET_GATE"] = "attested"
        with self.assertRaises(ImportError) as caught:
            gate.mode("IMTYPESET_LIBRARY", False)
        self.assertEqual(caught.exception.kind, "unchecked")
        os.environ["IMTYPESET_GATE"] = "sometimes"
        with self.assertRaises(ImportError) as caught:
            gate.mode("IMTYPESET_LIBRARY", True)
        self.assertEqual(caught.exception.kind, "configuration")

    def test_an_installed_distribution_reads_no_variable(self):
        # Whatever IMTYPESET_GATE says, the attestation and nothing else.
        for value in ("probe", "attested", "sometimes", ""):
            self.with_environment(IMTYPESET_GATE=value, IMTYPESET_LIBRARY="/somewhere/else.so",
                                  IMTYPESET_PRELOAD_BUNDLE="1", IMTYPESET_PYTHON=None)
            self.assertEqual(gate.mode("installed", True), "attested")
            with self.assertRaises(ImportError) as caught:
                gate.mode("installed", False)
            self.assertEqual(caught.exception.kind, "unchecked")
        self.with_environment(IMTYPESET_GATE="probe", IMTYPESET_LIBRARY="/x.so",
                              IMTYPESET_PRELOAD_BUNDLE="1", IMTYPESET_PYTHON="")
        self.assertEqual(gate.ignored_environment("installed"),
                         ["IMTYPESET_LIBRARY", "IMTYPESET_GATE", "IMTYPESET_PRELOAD_BUNDLE"])
        self.assertEqual(gate.ignored_environment("IMTYPESET_LIBRARY"), [])

    def as_an_application(self, frozen: bool, executable: str):
        saved = sys.executable, getattr(sys, "frozen", None)

        def restore():
            sys.executable = saved[0]
            if saved[1] is None and hasattr(sys, "frozen"):
                del sys.frozen

        self.addCleanup(restore)
        sys.executable = executable
        if frozen:
            sys.frozen = True

    def test_what_is_taken_for_an_interpreter(self):
        self.assertEqual(gate.child_interpreter(), (sys.executable, ""))
        directory = tempfile.mkdtemp(prefix="imtypeset-test-")
        self.addCleanup(shutil.rmtree, directory, True)
        names = {"python": True, "python3": True, "python3.11": True, "python3.13t": True,
                 "pythonw.exe": True, "pypy3.10": True, "Application": False, "my-app": False,
                 "pythonista-app": False, "app.bin": False}
        saved = sys.executable
        self.addCleanup(setattr, sys, "executable", saved)
        for name, taken in names.items():
            path = os.path.join(directory, name)
            with open(path, "wb"):
                pass
            sys.executable = path
            python, why = gate.child_interpreter()
            self.assertEqual(python is not None, taken, f"{name}: {why}")
        sys.executable = os.path.join(directory, "python3-that-is-not-there")
        self.assertIsNone(gate.child_interpreter()[0])
        sys.executable = ""
        self.assertIsNone(gate.child_interpreter()[0])
        sys.executable = saved
        sys.frozen = True
        self.addCleanup(lambda: hasattr(sys, "frozen") and delattr(sys, "frozen"))
        self.assertIsNone(gate.child_interpreter()[0])

    def test_an_application_never_gets_the_probe(self):
        # sys.frozen (PyInstaller, py2app), and a packager that sets no flag
        # and starts the application from a binary named for it.
        self.with_environment(IMTYPESET_GATE=None)
        real = sys.executable
        for frozen, executable in ((True, real), (False, "/nonexistent/Application"), (False, __file__)):
            self.as_an_application(frozen, executable)
            self.assertEqual(gate.mode("installed", True), "attested")
            self.assertEqual(gate.mode("IMTYPESET_LIBRARY", True), "attested")
            for route in ("installed", "IMTYPESET_LIBRARY"):
                with self.assertRaises(ImportError) as caught:
                    gate.mode(route, False)
                self.assertEqual(caught.exception.kind, "unchecked")
            os.environ["IMTYPESET_GATE"] = "probe"
            self.assertEqual(gate.mode("installed", True), "attested")
            with self.assertRaises(ImportError) as caught:
                gate.mode("IMTYPESET_LIBRARY", True)
            self.assertEqual(caught.exception.kind, "unchecked")
            with self.assertRaises(ImportError) as caught:
                gate.probe_in_child(__file__)  # and nothing is started to find out
            self.assertEqual(caught.exception.kind, "unchecked")
            os.environ.pop("IMTYPESET_GATE")
            sys.executable = real
            if hasattr(sys, "frozen"):
                del sys.frozen

    def scratch_library(self):
        directory = tempfile.mkdtemp(prefix="imtypeset-test-")
        self.addCleanup(shutil.rmtree, directory, True)
        library = os.path.join(directory, "libimtypeset_bundle.so")
        with open(library, "wb") as handle:
            handle.write(b"not a library at all, and never loaded by this test")
        return directory, library

    def good_attestation(self, library: str) -> dict:
        return {
            "schema": 1, "imtypeset_version": "0.3.0", "wheel_version": "0.0.1",
            "library": gate.identity(library), "module": {"sha256": "0" * 64},
            "platform": {"system": platform.system(), "machine": platform.machine()},
            "accepted_by": "probe", "probes": [["a", True, ""]], "headers": "fork:x",
        }

    def test_precheck_before_anything_is_loaded(self):
        self.with_environment(IMTYPESET_GATE=None)
        directory, library = self.scratch_library()
        attestation = os.path.join(directory, "_attestation.json")

        def refused(document, route="installed"):
            with open(attestation, "w", encoding="utf-8") as handle:
                handle.write(document if isinstance(document, str) else json.dumps(document))
            with self.assertRaises(ImportError) as caught:
                gate.precheck(library, route)
            first = str(caught.exception).splitlines()[0]
            self.assertNotIn(os.sep, first)
            return caught.exception.kind

        good = self.good_attestation(library)
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

    def test_an_attestation_of_another_shape_is_damaged_and_nothing_else(self):
        # Hand-edited or foreign files: every one is an ImportError with a
        # kind, and never another exception.
        self.with_environment(IMTYPESET_GATE=None)
        directory, library = self.scratch_library()
        attestation = os.path.join(directory, "_attestation.json")
        good = self.good_attestation(library)
        shapes = [
            "[]", "null", "7", '"text"', "{}",
            dict(good, library="a string"), dict(good, library=None), dict(good, library=[1]),
            dict(good, module="a string"), dict(good, module=7),
            dict(good, platform="Linux"), dict(good, platform={"system": 1, "machine": 2}),
            dict(good, platform={"system": "Linux"}),
            dict(good, probes=[]), dict(good, probes="all"), dict(good, probes=[["a", True]]),
            dict(good, probes=[["a", True, "", "extra"]]), dict(good, probes=[{"name": "a"}]),
            dict(good, probes=[["a", "yes", ""]]), dict(good, probes=[[1, True, ""]]),
            dict(good, wheel_version=None), dict(good, imtypeset_version=3),
            dict(good, headers=None), dict(good, accepted_by=[]),
            dict(good, library={"sha256": 5, "image_sha256": 6}),
            dict(good, library={}),
        ]
        for shape in shapes:
            with open(attestation, "w", encoding="utf-8") as handle:
                handle.write(shape if isinstance(shape, str) else json.dumps(shape))
            for route in ("installed", "IMTYPESET_LIBRARY"):
                with self.assertRaises(ImportError) as caught:
                    gate.precheck(library, route)
                self.assertEqual(caught.exception.kind, "damaged", repr(shape)[:80])
        for record in (None, "x", 5, [], {"sha256": None}, {"sha256": 5, "image_sha256": []}):
            self.assertIsNone(gate.same_content(record, library))
        self.assertIsNone(gate.same_content({"sha256": "0" * 64}, None))

    def test_a_named_library_with_no_attestation_is_loaded_in_a_child_first(self):
        # A source checkout's route. The file here is not a library: the
        # child says so, and this process never maps it.
        self.with_environment(IMTYPESET_GATE=None, IMTYPESET_PRELOAD_BUNDLE=None)
        directory, library = self.scratch_library()
        with self.assertRaises(ImportError) as caught:
            gate.precheck(library, "IMTYPESET_LIBRARY")
        self.assertEqual(caught.exception.kind, "not-a-library")
        if sys.platform.startswith("linux"):
            with open("/proc/self/maps", encoding="utf-8", errors="replace") as handle:
                self.assertNotIn(library, handle.read())

    @unittest.skipUnless(sys.platform.startswith("linux"), "needs an ELF file to cut")
    def test_a_truncated_library_with_no_attestation_ends_a_child_and_not_this_process(self):
        import _ctypes

        self.with_environment(IMTYPESET_GATE=None, IMTYPESET_PRELOAD_BUNDLE=None)
        directory, library = self.scratch_library()
        with open(_ctypes.__file__, "rb") as handle:
            data = handle.read()
        (phoff,) = struct.unpack_from("<Q", data, 0x20)
        phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
        mapped = 0
        for index in range(phnum):
            p_type, _flags, p_offset, _vaddr, _paddr, p_filesz = struct.unpack_from(
                "<IIQQQQ", data, phoff + index * phentsize)
            if p_type == 1:  # PT_LOAD
                mapped = max(mapped, p_offset + p_filesz)
        self.assertGreater(mapped, 0)
        # Kept: the headers and half of WHAT THE LOADER MAPS. Half of the file
        # is not that: an interpreter built with debugging sections has a
        # _ctypes whose segments end in its first quarter (GitHub's hosted
        # CPython 3.11.17: 606,824 bytes, mapped up to 133,664), and such a
        # file cut to half its length is whole to the loader and loads.
        with open(library, "wb") as handle:
            handle.write(data[:mapped // 2])
        with self.assertRaises(ImportError) as caught:
            gate.precheck(library, "IMTYPESET_LIBRARY")
        self.assertIn(caught.exception.kind, ("not-a-library", "refused"))
        with open("/proc/self/maps", encoding="utf-8", errors="replace") as handle:
            self.assertNotIn(library, handle.read())
        self.assertGreater(phoff, 0)

    def test_the_wheels_module_is_found_without_importing_it(self):
        directory = tempfile.mkdtemp(prefix="imtypeset-test-")
        self.addCleanup(shutil.rmtree, directory, True)
        import importlib.machinery

        package = os.path.join(directory, "imgui_bundle")
        os.makedirs(package)
        with open(os.path.join(package, "__init__.py"), "w", encoding="utf-8") as handle:
            handle.write("raise RuntimeError('imgui_bundle was imported')\n")
        module = os.path.join(package, "_imgui_bundle" + importlib.machinery.EXTENSION_SUFFIXES[0])
        with open(module, "wb") as handle:
            handle.write(b"not a module, and never loaded")
        saved = {name: sys.modules.pop(name) for name in list(sys.modules)
                 if name == "imgui_bundle" or name.startswith("imgui_bundle.")}
        sys.path.insert(0, directory)

        def restore():
            sys.path.remove(directory)
            for name in list(sys.modules):
                if name == "imgui_bundle" or name.startswith("imgui_bundle."):
                    del sys.modules[name]
            sys.modules.update(saved)
            importlib.invalidate_caches()

        self.addCleanup(restore)
        importlib.invalidate_caches()
        self.assertEqual(gate.module_file(), module)
        self.assertNotIn("imgui_bundle", sys.modules)
        # precheck hashes that file and refuses it BEFORE the import: the
        # package above would raise RuntimeError, and the refusal would then
        # be `bundle-missing`.
        self.with_environment(IMTYPESET_GATE=None)
        scratch, library = self.scratch_library()
        with open(os.path.join(scratch, "_attestation.json"), "w", encoding="utf-8") as handle:
            json.dump(self.good_attestation(library), handle)
        with self.assertRaises(ImportError) as caught:
            gate.precheck(library, "installed")
        self.assertEqual(caught.exception.kind, "bundle-mismatch")
        self.assertNotIn("imgui_bundle", sys.modules)
        # With no package at all, a sentence and not an AttributeError.
        os.remove(module)
        with self.assertRaises(ImportError) as caught:
            gate.precheck(library, "installed")
        self.assertEqual(caught.exception.kind, "bundle-missing")


class Route(unittest.TestCase):
    """_capi.library_route on copies of the package's two loader modules: which
    route a directory is, and that an installed one reads no variable."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="imtypeset-test-")
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.name = "imtypeset_route_" + os.path.basename(self.dir).replace("-", "_")
        self.package = os.path.join(self.dir, self.name)
        os.makedirs(self.package)
        for module in ("_capi.py", "_bundle.py"):
            shutil.copyfile(os.path.join(REPO, "python", "imtypeset", module),
                            os.path.join(self.package, module))
        with open(os.path.join(self.package, "__init__.py"), "w", encoding="utf-8"):
            pass
        sys.path.insert(0, self.dir)
        self.addCleanup(sys.path.remove, self.dir)
        self.capi = importlib.import_module(self.name + "._capi")
        self.addCleanup(lambda: [sys.modules.pop(n) for n in list(sys.modules) if n.startswith(self.name)])
        self.mine = self.capi.PACKAGED_LIBRARIES[self.capi._platform()]
        self.named = os.path.join(self.dir, "named.so")
        self.touch(self.named)
        saved = os.environ.get("IMTYPESET_LIBRARY")
        self.addCleanup(lambda: os.environ.pop("IMTYPESET_LIBRARY", None) if saved is None
                        else os.environ.__setitem__("IMTYPESET_LIBRARY", saved))

    def touch(self, path):
        with open(path, "wb") as handle:
            handle.write(b"x")

    def kind(self):
        with self.assertRaises(ImportError) as caught:
            self.capi.library_route()
        return caught.exception.kind

    @unittest.skipUnless(sys.platform.startswith("linux") or sys.platform == "darwin", "no packaged library")
    def test_routes(self):
        packaged = os.path.join(self.package, self.mine)
        attestation = os.path.join(self.package, "_attestation.json")
        # A source checkout: the variable, or nothing.
        os.environ.pop("IMTYPESET_LIBRARY", None)
        self.assertEqual(self.kind(), "no-library")
        os.environ["IMTYPESET_LIBRARY"] = self.named
        self.assertEqual(self.capi.library_route(), (self.named, "IMTYPESET_LIBRARY"))
        os.environ["IMTYPESET_LIBRARY"] = self.named + ".missing"
        self.assertEqual(self.kind(), "not-a-library")
        # An installed distribution, whole or in part: the variable is not read.
        for variable in (self.named, self.named + ".missing", None):
            if variable is None:
                os.environ.pop("IMTYPESET_LIBRARY", None)
            else:
                os.environ["IMTYPESET_LIBRARY"] = variable
            self.touch(packaged)
            self.touch(attestation)
            self.assertEqual(self.capi.library_route(), (packaged, "installed"))
            os.remove(attestation)  # the attestation lost: still installed, and refused later
            self.assertEqual(self.capi.library_route(), (packaged, "installed"))
            os.remove(packaged)
            self.touch(attestation)  # the library lost
            self.assertEqual(self.kind(), "damaged")
            other = [n for n in self.capi.PACKAGED_LIBRARIES.values() if n != self.mine][0]
            self.touch(os.path.join(self.package, other))  # a package from another system
            self.assertEqual(self.kind(), "wrong-platform")
            os.remove(os.path.join(self.package, other))
            os.remove(attestation)


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
        self.assertEqual(build_wheel.distribution_version("0.3.0", "1.92.900", True),
                         "0.3.0.192900.dev0+standin")
        for bad in ("1.92", "1.92.900.1", "1.192.0", "1.92.1000", "v1.92.900"):
            with self.assertRaises(build_wheel.Failed):
                build_wheel.bundle_line(bad)

    def test_a_stand_in_build_orders_below_the_release_of_its_line(self):
        # PEP 440's ordering, by whichever implementation of it is at hand:
        # `packaging`, or pip's own copy of it. Skipped where neither is.
        try:
            from packaging.specifiers import SpecifierSet
            from packaging.version import Version
        except ImportError:
            try:
                from pip._vendor.packaging.specifiers import SpecifierSet
                from pip._vendor.packaging.version import Version
            except ImportError:
                self.skipTest("neither packaging nor pip is importable here")
        release = build_wheel.distribution_version("0.3.0", "1.92.900", False)
        stand_in = build_wheel.distribution_version("0.3.0", "1.92.900", True)
        self.assertLess(Version(stand_in), Version(release))
        self.assertTrue(Version(stand_in).is_devrelease and Version(stand_in).local == "standin")
        # What this replaced ordered ABOVE the release.
        self.assertGreater(Version(release + "+standin"), Version(release))
        # A stand-in of one line is still above the release of an older line.
        self.assertGreater(Version(stand_in), Version(build_wheel.distribution_version("0.3.0", "1.92.801", False)))
        # An exact pin of the release does not match the stand-in; a pin of the
        # library version matches both lines and, by default, no stand-in.
        self.assertFalse(SpecifierSet("==" + release).contains(stand_in, prereleases=True))
        line = SpecifierSet("==0.3.0.*")
        self.assertEqual(list(line.filter([stand_in, release, "0.3.0.192801"])), [release, "0.3.0.192801"])
        # Alone, it is installable when development releases are asked for
        # (pip's --pre). Unasked, implementations differ: packaging 26 falls
        # back to it for this range, packaging 21.3 (inside pip 23.0.1) does not.
        self.assertEqual(list(line.filter([stand_in], prereleases=True)), [stand_in])
        self.assertIn(list(line.filter([stand_in])), ([stand_in], []))
        self.assertFalse(SpecifierSet("==0.3.0").contains(release))

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


class Publishing(unittest.TestCase):
    """What an index is given: the metadata, and which files make a release."""

    FACTS = {"wheel_version": "1.92.900", "imgui_version": "1.92.9", "python": "3.11",
             "system": "Linux", "machine": "x86_64"}

    def attestation(self, stand_in=False):
        return {"stand_in": stand_in, "imtypeset_version": "0.4.1",
                "headers": "stand-in:ocornut-v1.92.9-docking" if stand_in else "fork:" + "c" * 40,
                "accepted_by": "probe" if stand_in else "imgui-check+probe", "probes": [["a", True, ""]],
                "attested_on": "2026-10-12T00:00:00Z", "attested_where": "a test",
                "library": {"file": "libimtypeset_bundle.so"}}

    def fields(self, text):
        head, _, body = text.partition("\n\n")
        fields = {}
        for line in head.splitlines():
            key, _, value = line.partition(": ")
            fields.setdefault(key, []).append(value)
        return fields, body

    def test_the_metadata_is_what_an_index_takes(self):
        text = build_wheel.metadata("0.4.1.192900", self.FACTS, self.attestation())
        fields, body = self.fields(text)
        self.assertEqual(fields["Metadata-Version"], ["2.4"])
        self.assertEqual(fields["License-Expression"], ["MIT"])
        self.assertEqual(fields["License-File"], ["LICENSE", "THIRD-PARTY.md", "NOTICE.txt"])
        # Metadata 2.4: no `License` beside License-Expression, no licence classifier.
        self.assertNotIn("License", fields)
        self.assertFalse([c for c in fields["Classifier"] if c.startswith("License ::")])
        self.assertEqual(fields["Requires-Dist"], ["imgui-bundle==1.92.900"])
        self.assertEqual(fields["Requires-Python"], [">=3.11,<3.15"])
        self.assertEqual([u.split(",")[0] for u in fields["Project-URL"]],
                         ["Repository", "Changelog", "Documentation", "Issues"])
        self.assertTrue(fields["Description-Content-Type"][0].startswith("text/markdown"))
        with open(build_wheel.README, encoding="utf-8") as handle:
            self.assertIn(handle.read().strip(), body)
        self.assertNotIn("STAND-IN", text)
        for key, values in fields.items():
            for value in values:
                self.assertEqual(value, value.strip(), key)
        # The body is the description as it is: no line of it may read as a header's end.
        self.assertTrue(text.endswith("\n"))
        try:
            from packaging.metadata import Metadata
        except ImportError:
            return  # the standard library alone: what is above
        parsed = Metadata.from_email(text.encode("utf-8"), validate=True)
        self.assertEqual(str(parsed.version), "0.4.1.192900")
        self.assertEqual(str(parsed.license_expression), "MIT")

    def test_a_stand_in_build_says_so_on_its_page(self):
        text = build_wheel.metadata("0.4.1.192900.dev0+standin", self.FACTS, self.attestation(True))
        fields, body = self.fields(text)
        self.assertIn("STAND-IN HEADERS", fields["Summary"][0])
        self.assertTrue(body.startswith("> **THIS FILE WAS BUILT AGAINST STAND-IN HEADERS**"))

    def test_the_notice_names_the_licence(self):
        text = build_wheel.notice("0.4.1.192900", self.FACTS, self.attestation())
        self.assertIn("MIT: LICENSE", text)
        with open(os.path.join(REPO, "LICENSE"), encoding="utf-8") as handle:
            self.assertTrue(handle.read().startswith("MIT License\n\nCopyright (c) "))

    def test_a_release_is_every_cell_of_the_matrix(self):
        sys.path.insert(0, os.path.join(REPO, "tools", "ci"))
        import wheel_matrix
        import wheel_publish

        cells = wheel_publish.expected("0.4.1")
        self.assertEqual(len(cells), len(wheel_matrix.matrix({})["include"]))
        self.assertIn(("cp311", "0.4.1.192900", "linux-x86_64"), cells)
        self.assertIn(("cp313", "0.4.1.192801", "macos-arm64"), cells)
        for name, cell in (
            ("imtypeset-0.4.1.192900-cp311-none-manylinux_2_28_x86_64.whl", ("cp311", "0.4.1.192900", "linux-x86_64")),
            ("imtypeset-0.4.1.192801-cp312-none-macosx_14_0_arm64.whl", ("cp312", "0.4.1.192801", "macos-arm64")),
        ):
            self.assertEqual(wheel_publish.cell_of(name), cell)
            self.assertIn(cell, cells)
        for name in ("imtypeset-0.4.1.192900-cp311-none-linux_x86_64.whl",
                     "imtypeset-0.4.1.192900-cp311-none-manylinux_2_28_aarch64.whl",
                     "imtypeset-0.4.1.192900-cp310-none-manylinux_2_28_x86_64.whl",
                     "imtypeset-0.4.1.192900.dev0+standin-cp311-none-manylinux_2_28_x86_64.whl",
                     "imtypeset-0.4.0.192900-cp311-none-manylinux_2_28_x86_64.whl"):
            self.assertNotIn(wheel_publish.cell_of(name), cells, name)


class RequiresPython(unittest.TestCase):
    """One Requires-Python per release. An index keeps one per release and an
    installer filters every file by it; 0.4.1 declared `==3.11.*` in its
    cp311 files and `==3.12.*` ... in the others, the index kept `==3.11.*`,
    and no CPython but 3.11 could install any file of it (request H52)."""

    def setUp(self):
        sys.path.insert(0, os.path.join(REPO, "tools", "ci"))
        self.addCleanup(sys.path.remove, os.path.join(REPO, "tools", "ci"))
        import check_wheel
        import dependent_check
        import wheel_matrix
        import wheel_publish
        self.check_wheel, self.dependent_check = check_wheel, dependent_check
        self.wheel_matrix, self.wheel_publish = wheel_matrix, wheel_publish

    def test_the_range_is_the_matrix(self):
        m = self.wheel_matrix
        self.assertEqual(m.requires_python(), ">=3.11,<3.15")
        self.assertEqual(m.requires_python(("3.12", "3.11")), ">=3.11,<3.13")
        cpythons = {entry["python"] for entry in m.matrix({})["include"]}
        self.assertEqual(cpythons, set(m.sidecar_matrix.PYTHONS))
        for python in cpythons:
            self.assertIs(m.admits(m.requires_python(), python), True, python)
        for python in ("3.10", "3.15", "4.0"):
            self.assertIs(m.admits(m.requires_python(), python), False, python)
        for hole in (("3.11", "3.13"), (), ("2.7",), ("3.11.2",)):
            with self.assertRaises(ValueError, msg=str(hole)):
                m.requires_python(hole)

    def test_admits(self):
        admits = self.wheel_matrix.admits
        for spec, python, want in (
                ("==3.11.*", "3.11", True), ("==3.11.*", "3.12", False),
                (">=3.11,<3.15", "3.14", True), (">=3.11, <3.15", "3.11", True),
                (">=3.11,<3.15", "3.15", False), (">3.11", "3.11", False), (">3.11", "3.12", True),
                ("<=3.14", "3.14", False),  # 3.14.1 is above 3.14
                ("==3.11", "3.11", False),  # 3.11.0 only
                ("!=3.12.*", "3.12", False), ("!=3.12.*", "3.13", True),
                ("~=3.11", "3.12", None), (">=3.11a1", "3.12", None), ("", "3.12", None),
                (">=3.11,", "3.12", None), (">=3.*", "3.12", None)):
            self.assertIs(admits(spec, python), want, f"{spec!r} {python}")
        try:
            from packaging.specifiers import SpecifierSet
        except ImportError:
            return  # the standard library alone: what is above
        for spec in (">=3.11,<3.15", "==3.11.*", ">3.11", "<=3.14", "!=3.12.*", ">=3.12"):
            for python in ("3.10", "3.11", "3.12", "3.14", "3.15"):
                both = [SpecifierSet(spec).contains(python + patch) for patch in (".0", ".99")]
                self.assertEqual(admits(spec, python), all(both), f"{spec!r} {python}")

    def test_every_file_writes_the_same_range(self):
        facts = dict(Publishing.FACTS)
        texts = []
        for python, system in (("3.11", "Linux"), ("3.14", "Darwin"), ("3.12", "Linux")):
            facts.update(python=python, system=system)
            text = build_wheel.metadata("0.4.2.192900", facts, Publishing.attestation(Publishing()))
            fields, _body = Publishing.fields(Publishing(), text)
            texts.append(fields)
        for fields in texts:
            self.assertEqual(fields["Requires-Python"], [">=3.11,<3.15"])
            self.assertEqual(fields["Classifier"], texts[0]["Classifier"])
        for python in ("3.11", "3.12", "3.13", "3.14"):
            self.assertIn(f"Programming Language :: Python :: {python}", texts[0]["Classifier"])

    def test_a_wheel_is_held_to_the_range(self):
        problems = self.check_wheel.requires_python_problems
        self.assertEqual(problems([">=3.11,<3.15"], "cp311"), [])
        self.assertEqual(problems([">=3.11,<3.15"], "cp314"), [])
        # 0.4.1's files, each as it was written.
        for python in ("11", "12", "13", "14"):
            self.assertTrue(problems([f"==3.{python}.*"], f"cp3{python}"), python)
        self.assertTrue(problems([], "cp312"))
        self.assertTrue(problems([">=3.11,<3.15", ">=3.11,<3.15"], "cp312"))
        self.assertTrue(problems([">=3.11,<3.15"], "cp315"))
        self.assertTrue(problems([">=3.11,<3.15"], "py3"))

    def rows(self, spec_of):
        return [(f"imtypeset-0.4.1.{line}-cp3{minor}-none-{platform}.whl", f"cp3{minor}", spec_of(minor))
                for line in ("192900", "192801") for minor in (11, 12, 13, 14)
                for platform in ("manylinux_2_28_x86_64", "macosx_14_0_arm64")]

    def test_a_release_is_held_to_one_range(self):
        problems = self.wheel_publish.requires_python_problems
        self.assertEqual(problems(self.rows(lambda minor: [">=3.11,<3.15"])), [])
        # 0.4.1 as it was built: each file its own CPython. Refused as disagreeing.
        found = problems(self.rows(lambda minor: [f"==3.{minor}.*"]))
        self.assertEqual(len(found), 1)
        self.assertIn("disagree", found[0])
        # 0.4.1 as the index kept it: every file `==3.11.*`. Refused for each
        # file of 3.12 to 3.14, which no installer would take.
        found = problems(self.rows(lambda minor: ["==3.11.*"]))
        self.assertEqual(len(found), 12)
        self.assertTrue(all("does not admit cp31" in p for p in found))
        self.assertFalse([p for p in found if "-cp311-" in p])
        # One range that leaves a CPython of the set out.
        found = problems(self.rows(lambda minor: [">=3.11,<3.14"]))
        self.assertEqual(len(found), 4)
        self.assertTrue(all("cp314" in p for p in found))
        # A specifier the check cannot read is not taken as a yes.
        self.assertEqual(len(problems(self.rows(lambda minor: ["~=3.11"]))), 16)
        self.assertTrue(problems(self.rows(lambda minor: [])))

    def test_the_dependents_marker_is_the_range(self):
        marker = self.dependent_check.MARKER
        self.assertTrue(marker.startswith("python_version >= '3.11' and python_version < '3.15' and "))
        self.assertEqual(self.dependent_check.python_marker(("3.11", "3.12")),
                         "python_version >= '3.11' and python_version < '3.13'")
        with open(os.path.join(REPO, "docs", "releasing.md"), encoding="utf-8") as handle:
            self.assertIn(f"; {marker}\"", handle.read())

    def test_a_cpython_takes_only_its_own_cp_tag(self):
        """Why the range is safe: the per-CPython restriction is the tag. With
        `packaging` (what pip vendors), CPython 3.12 does not take a
        cp311-none-<platform> wheel, nor any cp3XY-none but its own."""
        try:
            from packaging import tags
        except ImportError:
            self.skipTest("packaging is not installed")
        for minor in (11, 12, 13, 14):
            for platform_tag in ("manylinux_2_28_x86_64", "macosx_14_0_arm64"):
                accepted = {str(t) for t in tags.cpython_tags((3, minor), abis=[f"cp3{minor}"],
                                                              platforms=[platform_tag])}
                accepted |= {str(t) for t in tags.compatible_tags((3, minor), interpreter=f"cp3{minor}",
                                                                  platforms=[platform_tag])}
                ours = sorted(t for t in accepted if t.startswith("cp") and t.endswith("-none-" + platform_tag))
                self.assertEqual(ours, [f"cp3{minor}-none-{platform_tag}"])


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1)
