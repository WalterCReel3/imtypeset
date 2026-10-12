"""python/host/fetch_headers.py against local sources: a git repository made
here in place of GitHub's, and a file:// URL in place of raw.githubusercontent.com.

    python3 -I -B tools/ci/test_fetch_headers.py [-v]

What this proves is the fetch, the checks, and that a cache is held to the
pins and not to its own record. It does not reach the
network, so that github.com serves the pinned commits by id is not shown here.

Where the sibling clones ../imgui_bundle and ../imgui exist (they do not on a
CI runner), two more tests run: every entry of python/host/bundle_pins.toml is
derived again from them, and the stand-in headers are fetched from them.
"""

import contextlib
import hashlib
import importlib.util
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location(
    "fetch_headers", os.path.join(REPO, "python", "host", "fetch_headers.py"))
fetch_headers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch_headers)

BUNDLE_CLONE = os.path.join(REPO, "..", "imgui_bundle")
IMGUI_CLONE = os.path.join(REPO, "..", "imgui")
CONFIG = b"#pragma once\n#define ImDrawIdx unsigned int\n"


def git(cwd, *arguments) -> str:
    environment = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_SYSTEM=os.devnull,
                       GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.invalid",
                       GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.invalid")
    return subprocess.run(["git", *arguments], cwd=cwd, env=environment, check=True,
                          capture_output=True, text=True).stdout.strip()


class Fetch(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="imtypeset-fetch-test.")
        self.addCleanup(shutil.rmtree, self.root, True)
        # A repository standing where the fork does: two commits, so that the
        # pinned one can be the older and not a branch's tip.
        self.fork = os.path.join(self.root, "fork")
        os.makedirs(os.path.join(self.fork, "backends"))
        git(self.fork, "init", "-q", "-b", "main")
        for name, text in (*self.FORK_FILES.items(), ("imgui.cpp", "// not a header\n"),
                           ("backends/imgui_impl_x.h", "// not top level\n")):
            pathlib.Path(self.fork, name).write_text(text)
        git(self.fork, "add", "-A")
        git(self.fork, "commit", "-q", "-m", "one")
        self.commit = git(self.fork, "rev-parse", "HEAD")
        git(self.fork, "tag", "pinned")
        pathlib.Path(self.fork, "imgui.h").write_text("// a later imgui.h\n")
        git(self.fork, "commit", "-q", "-am", "two")
        self.later = git(self.fork, "rev-parse", "HEAD")

        self.config = os.path.join(self.root, "served", "imgui_bundle_cmake")
        os.makedirs(self.config)
        pathlib.Path(self.config, "imgui_bundle_config.h").write_bytes(CONFIG)
        self.config_url = pathlib.Path(self.root, "served").as_uri() + "/{path}"
        self.cache = os.path.join(self.root, "cache")
        self.pins = os.path.join(self.root, "pins.toml")
        self.write_pins()

    FORK_FILES = {"imgui.h": "// the fork's imgui.h\n#include \"imconfig.h\"\n", "imconfig.h": "// imconfig\n",
                  "imgui_internal.h": "// internal\n", "LICENSE.txt": "MIT\n"}

    def recorded(self, **files) -> dict:
        """The checksums of the fork's files at the pinned commit, as the pins
        record them; `files` replaces some."""
        sums = {name: hashlib.sha256(text.encode()).hexdigest()
                for name, text in dict(self.FORK_FILES, **files).items()}
        later = dict(sums, **{"imgui.h": hashlib.sha256(b"// a later imgui.h\n").hexdigest()})
        return dict(imgui_h_sha256=sums["imgui.h"], imgui_headers_sha256=fetch_headers.headers_digest(sums),
                    standin_headers_sha256=fetch_headers.headers_digest(later))

    def write_pins(self, unrecorded=(), **changed):
        entry = dict(bundle_tag="v9.9.900", bundle_commit="0" * 40, imgui_version="9.9.9",
                     imgui_commit=self.commit, config_sha256=hashlib.sha256(CONFIG).hexdigest(),
                     standin_tag="v9.9.9-docking", standin_commit=self.later, **self.recorded())
        entry.update(changed)
        for key in unrecorded:
            del entry[key]
        lines = ['[bundle."9.9.900"]'] + [f'{key} = "{value}"' for key, value in entry.items()]
        pathlib.Path(self.pins).write_text("\n".join(lines) + "\n")

    def fetch(self, kind="fork", **arguments):
        arguments.setdefault("imgui_sources", [self.fork])
        return fetch_headers.fetch("9.9.900", kind, self.cache, self.pins,
                                   config_url=self.config_url, **arguments)

    def test_the_pinned_commit_arrives_and_not_the_branch_tip(self):
        result = self.fetch()
        self.assertEqual(result["label"], "fork:" + self.commit)
        self.assertFalse(result["from_cache"])
        directory = pathlib.Path(result["dir"])
        self.assertEqual((directory / "imgui" / "imgui.h").read_text(),
                         "// the fork's imgui.h\n#include \"imconfig.h\"\n")
        self.assertEqual(sorted(p.name for p in (directory / "imgui").iterdir()),
                         ["LICENSE.txt", "imconfig.h", "imgui.h", "imgui_internal.h"])
        self.assertEqual((directory / "imgui_bundle_config.h").read_bytes(), CONFIG)
        source = result["source"]
        self.assertEqual(source["imgui_source"], self.fork)
        self.assertTrue(source["config_source"].startswith("file://"))
        self.assertEqual(source["sha256"]["imgui_bundle_config.h"],
                         hashlib.sha256(CONFIG).hexdigest())

    def test_a_second_fetch_is_the_cache_and_a_changed_file_is_fetched_again(self):
        first = self.fetch()
        self.assertEqual(first["unrecorded"], [])
        # No source at all: only the cache can answer.
        nowhere = [os.path.join(self.root, "nowhere")]
        second = self.fetch(imgui_sources=nowhere)
        self.assertTrue(second["from_cache"])
        self.assertEqual(second["dir"], first["dir"])
        pathlib.Path(first["dir"], "imgui", "imgui.h").write_text("// edited in the cache\n")
        with self.assertRaises(fetch_headers.FetchError):
            self.fetch(imgui_sources=nowhere)
        third = self.fetch()
        self.assertFalse(third["from_cache"])
        self.assertIn("the fork's", pathlib.Path(third["dir"], "imgui", "imgui.h").read_text())

    def edit_with_its_record(self, directory, relative, text):
        """Changes a cached file AND the sha256 the cache's own SOURCE.json has for it."""
        import json

        pathlib.Path(directory, relative).write_text(text)
        record = pathlib.Path(directory, "SOURCE.json")
        source = json.loads(record.read_text())
        source["sha256"][relative] = hashlib.sha256(text.encode()).hexdigest()
        record.write_text(json.dumps(source))

    def test_the_cache_is_held_to_the_pins_and_not_to_its_own_record(self):
        nowhere = [os.path.join(self.root, "nowhere")]
        for relative in ("imgui/imgui.h", "imgui/imgui_internal.h", "imgui/LICENSE.txt",
                         "imgui_bundle_config.h"):
            with self.subTest(edited=relative):
                first = self.fetch()
                self.edit_with_its_record(first["dir"], relative, "// edited, and SOURCE.json with it\n")
                with self.assertRaises(fetch_headers.FetchError):
                    self.fetch(imgui_sources=nowhere)
                self.assertFalse(self.fetch()["from_cache"])
        # A file added to the cache, a file taken out of it, and no SOURCE.json at all.
        first = self.fetch()
        pathlib.Path(first["dir"], "imgui", "imgui_extra.h").write_text("// planted\n")
        with self.assertRaises(fetch_headers.FetchError):
            self.fetch(imgui_sources=nowhere)
        first = self.fetch()
        os.remove(os.path.join(first["dir"], "imgui", "imgui_internal.h"))
        with self.assertRaises(fetch_headers.FetchError):
            self.fetch(imgui_sources=nowhere)
        first = self.fetch()
        os.remove(os.path.join(first["dir"], "SOURCE.json"))
        self.assertTrue(self.fetch(imgui_sources=nowhere)["from_cache"])

    def test_headers_the_pins_do_not_record_are_never_taken_from_a_cache(self):
        said = []
        self.write_pins(unrecorded=("imgui_h_sha256", "imgui_headers_sha256"))
        summary = os.path.join(self.root, "summary.md")
        saved = os.environ.get("GITHUB_STEP_SUMMARY")
        os.environ["GITHUB_STEP_SUMMARY"] = summary
        self.addCleanup(lambda: os.environ.pop("GITHUB_STEP_SUMMARY", None) if saved is None
                        else os.environ.__setitem__("GITHUB_STEP_SUMMARY", saved))
        first = self.fetch(log=said.append)
        want = self.recorded()
        self.assertEqual(first["unrecorded"], [f'imgui_h_sha256 = "{want["imgui_h_sha256"]}"',
                                               f'imgui_headers_sha256 = "{want["imgui_headers_sha256"]}"'])
        # The lines to paste are printed, and are on the job's summary page.
        for text in ("\n".join(said), pathlib.Path(summary).read_text()):
            for line in first["unrecorded"]:
                self.assertIn(line, text)
            self.assertIn('[bundle."9.9.900"]', text)
        with self.assertRaises(fetch_headers.FetchError):
            self.fetch(imgui_sources=[os.path.join(self.root, "nowhere")])  # no cache stands in
        self.assertFalse(self.fetch()["from_cache"])
        # A release asks for recorded headers, and gets a refusal that says what to record.
        with self.assertRaises(fetch_headers.FetchError) as caught:
            self.fetch(require_recorded=True)
        self.assertIn("imgui_h_sha256", str(caught.exception))
        # Stand-in is recorded here, so it is cached and a release could ask for it.
        self.assertFalse(self.fetch("stand-in", require_recorded=True)["from_cache"])
        self.assertTrue(self.fetch("stand-in", require_recorded=True,
                                   imgui_sources=[os.path.join(self.root, "nowhere")])["from_cache"])
        # Once pasted, the fork is too: the copy fetched before hashes to what
        # the pins now record.
        self.write_pins()
        self.assertTrue(self.fetch(require_recorded=True,
                                   imgui_sources=[os.path.join(self.root, "nowhere")])["from_cache"])

    def test_changed_pins_do_not_reuse_the_cache(self):
        self.fetch()
        later = hashlib.sha256(b"// a later imgui.h\n").hexdigest()
        self.write_pins(imgui_commit=self.later, imgui_h_sha256=later,
                        imgui_headers_sha256=self.recorded()["standin_headers_sha256"])
        result = self.fetch()
        self.assertFalse(result["from_cache"])
        self.assertEqual(pathlib.Path(result["dir"], "imgui", "imgui.h").read_text(),
                         "// a later imgui.h\n")
        # The commit alone changed, and the recorded checksums did not: refused.
        self.write_pins(imgui_commit=self.later)
        with self.assertRaises(fetch_headers.FetchError):
            self.fetch()

    def test_a_commit_the_source_does_not_have_is_refused(self):
        self.write_pins(imgui_commit="1234567890abcdef1234567890abcdef12345678")
        with self.assertRaises(fetch_headers.FetchError) as caught:
            self.fetch()
        self.assertIn("1234567890abcdef1234567890abcdef12345678", str(caught.exception))
        self.assertFalse(os.path.exists(os.path.join(self.cache, "9.9.900", "fork")))

    def test_the_second_source_is_tried_when_the_first_fails(self):
        result = self.fetch(imgui_sources=[os.path.join(self.root, "nowhere"), self.fork])
        self.assertEqual(result["source"]["imgui_source"], self.fork)

    def test_a_configuration_header_with_another_checksum_is_refused(self):
        self.write_pins(config_sha256="f" * 64)
        with self.assertRaises(fetch_headers.FetchError) as caught:
            self.fetch()
        self.assertIn("is recorded", str(caught.exception))
        self.assertFalse(os.path.exists(os.path.join(self.cache, "9.9.900", "fork")))

    def test_a_missing_configuration_header_is_refused(self):
        os.remove(os.path.join(self.config, "imgui_bundle_config.h"))
        with self.assertRaises(fetch_headers.FetchError):
            self.fetch()

    def test_a_recorded_checksum_is_checked_on_every_fetch(self):
        for key in ("imgui_h_sha256", "imgui_headers_sha256"):
            self.write_pins(**{key: "e" * 64})
            with self.assertRaises(fetch_headers.FetchError) as caught:
                self.fetch()
            self.assertIn(key, str(caught.exception))
            self.assertFalse(os.path.exists(os.path.join(self.cache, "9.9.900", "fork")))
        self.write_pins(standin_headers_sha256="e" * 64)
        with self.assertRaises(fetch_headers.FetchError):
            self.fetch("stand-in")
        good = self.recorded()["imgui_h_sha256"]
        self.write_pins()
        self.assertEqual(self.fetch()["source"]["imgui_h_sha256"], good)

    def test_stand_in_is_another_commit_another_label_another_directory(self):
        fork = self.fetch()
        stand_in = self.fetch("stand-in")
        self.assertEqual(stand_in["label"], "stand-in:ocornut-v9.9.9-docking")
        self.assertNotEqual(stand_in["dir"], fork["dir"])
        self.assertEqual(pathlib.Path(stand_in["dir"], "imgui", "imgui.h").read_text(),
                         "// a later imgui.h\n")
        self.assertIn("the fork's", pathlib.Path(fork["dir"], "imgui", "imgui.h").read_text())

    def test_an_unknown_wheel_version_is_refused_with_instructions(self):
        with self.assertRaises(fetch_headers.UnknownVersion) as caught:
            fetch_headers.fetch("1.0.0", "fork", self.cache, self.pins)
        self.assertIn("does not guess", str(caught.exception))
        self.assertIn("9.9.900", str(caught.exception))
        with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            status = fetch_headers.main(["--bundle-version", "1.0.0", "--pins", self.pins,
                                         "--cache", self.cache])
        self.assertEqual(status, 2)

    def test_the_command_line_prints_the_directory_and_the_label(self):
        done = subprocess.run(
            [sys.executable, "-I", "-B", os.path.join(REPO, "python", "host", "fetch_headers.py"),
             "--bundle-version", "9.9.900", "--pins", self.pins, "--cache", self.cache,
             "--imgui-source", self.fork, "--config-url", self.config_url],
            capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        lines = dict(line.split("=", 1) for line in done.stdout.splitlines())
        self.assertEqual(lines["label"], "fork:" + self.commit)
        self.assertTrue(os.path.isfile(os.path.join(lines["dir"], "imgui", "imgui.h")))

    def test_a_local_bundle_clone_supplies_the_configuration_header(self):
        bundle = os.path.join(self.root, "bundle")
        os.makedirs(os.path.join(bundle, "imgui_bundle_cmake"))
        git(bundle, "init", "-q", "-b", "main")
        pathlib.Path(bundle, "imgui_bundle_cmake", "imgui_bundle_config.h").write_bytes(CONFIG)
        git(bundle, "add", "-A")
        git(bundle, "commit", "-q", "-m", "bundle")
        self.write_pins(bundle_commit=git(bundle, "rev-parse", "HEAD"))
        os.remove(os.path.join(self.config, "imgui_bundle_config.h"))  # the URL would fail
        result = self.fetch(bundle_checkout=bundle)
        self.assertIn("(git objects)", result["source"]["config_source"])


@unittest.skipUnless(os.path.isdir(os.path.join(BUNDLE_CLONE, ".git"))
                     and os.path.isdir(os.path.join(IMGUI_CLONE, ".git")),
                     "the sibling clones ../imgui_bundle and ../imgui are not here")
class TrackedPins(unittest.TestCase):
    def versions(self):
        import tomllib

        with open(fetch_headers.PINS, "rb") as handle:
            return tomllib.load(handle)["bundle"]

    def test_every_entry_is_what_the_local_clones_say(self):
        for version, pin in self.versions().items():
            with self.subTest(version=version):
                tag = pin["bundle_tag"]
                self.assertEqual(tag, "v" + version)
                self.assertEqual(git(BUNDLE_CLONE, "rev-parse", tag + "^{commit}"),
                                 pin["bundle_commit"])
                self.assertEqual(git(BUNDLE_CLONE, "ls-tree", tag, "external/imgui/imgui").split()[2],
                                 pin["imgui_commit"])
                self.assertIn("https://github.com/pthom/imgui.git",
                              git(BUNDLE_CLONE, "show", tag + ":.gitmodules"))
                config = subprocess.run(
                    ["git", "-C", BUNDLE_CLONE, "show", tag + ":" + fetch_headers.CONFIG_PATH],
                    check=True, capture_output=True).stdout
                self.assertEqual(hashlib.sha256(config).hexdigest(), pin["config_sha256"])
                self.assertEqual(pin["standin_tag"], f"v{pin['imgui_version']}-docking")
                self.assertEqual(git(IMGUI_CLONE, "rev-parse", pin["standin_tag"] + "^{commit}"),
                                 pin["standin_commit"])

    def test_stand_in_headers_are_fetched_from_the_local_clones(self):
        cache = tempfile.mkdtemp(prefix="imtypeset-fetch-test.")
        self.addCleanup(shutil.rmtree, cache, True)
        for version, pin in self.versions().items():
            with self.subTest(version=version):
                result = fetch_headers.fetch(version, "stand-in", cache,
                                             imgui_sources=[IMGUI_CLONE],
                                             bundle_checkout=BUNDLE_CLONE)
                header = pathlib.Path(result["dir"], "imgui", "imgui.h").read_text()
                self.assertIn(f'#define IMGUI_VERSION       "{pin["imgui_version"]}"', header)
                self.assertEqual(result["label"], "stand-in:ocornut-" + pin["standin_tag"])
                # The tracked pins record these headers, and a second fetch
                # with no source is the cache, held to them.
                self.assertEqual(result["unrecorded"], [])
                again = fetch_headers.fetch(version, "stand-in", cache,
                                            imgui_sources=[os.path.join(cache, "nowhere")],
                                            bundle_checkout=BUNDLE_CLONE)
                self.assertTrue(again["from_cache"])

    def test_what_is_recorded_and_what_waits_for_a_first_fetch(self):
        # Stand-in checksums are recorded for every version. The fork's are
        # recorded together or not at all; this fails when one is pasted
        # without the other.
        for version, pin in self.versions().items():
            with self.subTest(version=version):
                self.assertRegex(pin.get("standin_headers_sha256", ""), "^[0-9a-f]{64}$")
                fork = [bool(pin.get(key)) for key, _ in fetch_headers.RECORDED["fork"]]
                self.assertIn(fork, ([True, True], [False, False]))


if __name__ == "__main__":
    unittest.main()
