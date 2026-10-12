"""The Dear ImGui headers an installed imgui-bundle wheel was compiled from.

    python3 -I -B python/host/fetch_headers.py --bundle-version 1.92.900
        [--kind fork|stand-in] [--cache DIR] [--pins FILE]
        [--imgui-source URL-OR-PATH] [--bundle-checkout DIR] [--config-url TEMPLATE]
        [--require-recorded]

A wheel ships no headers. What it was built from is recorded per wheel version
in bundle_pins.toml, beside this file, and this script fetches exactly that:

  fork      (the default) the wheel's own Dear ImGui: github.com/pthom/imgui at
            the commit the bundle's release tag pins. Every top-level *.h and
            LICENSE.txt, fetched with git by commit id.
  stand-in  upstream Dear ImGui's docking tag of the same version, at the
            commit recorded for it. Not the wheel's headers: the fork changes
            them. Only for a caller that asked for it by name.

and, for both, the configuration header the bundle compiles ImGui with
(imgui_bundle_cmake/imgui_bundle_config.h of github.com/pthom/imgui_bundle at
the release tag's commit), one file by URL, checked against a recorded sha256.

Three files are what a build includes: imgui.h, the imconfig.h it includes, and
imgui_bundle_config.h (named by IMGUI_USER_CONFIG). The other top-level headers
are taken because whether the fork's imgui.h includes a sibling has not been
read: no copy of the fork has been on the machine this was written on.

Prints `dir=<directory>` and `label=<what the library should say it was built
against>`; the directory holds imgui/ (the headers), imgui_bundle_config.h and
SOURCE.json (where each file came from and its sha256). Exit 0; 2 for a wheel
version bundle_pins.toml does not know; 1 for anything that could not be
fetched or did not match what is recorded. Nothing is guessed and nothing is
substituted: a failure names what was wanted and from where.

WHAT IS RECORDED, AND WHAT A CACHE IS HELD TO. A fetch is checked by the
commit id (git's own hash of everything fetched) and by config_sha256. Beside
those, bundle_pins.toml records sha256 values of the fetched headers
themselves, which need no git to check:

  imgui_h_sha256           the fork's imgui.h
  imgui_headers_sha256     every file fetched from the fork (`headers_digest`)
  standin_headers_sha256   every file fetched from upstream, for stand-in

A recorded value is always checked. A result is kept under --cache (default
build/bundle-headers in this repository, or IMTYPESET_HEADER_CACHE) and used
again ONLY when the files in it hash to what THE PINS record: the cache's own
SOURCE.json says where the files came from and vouches for nothing. So a kind
whose values are not recorded yet is fetched again every time.

The fork's two values are not recorded until a machine has fetched the fork.
The first fetch that does prints them as the lines to add to
bundle_pins.toml, and writes the same to $GITHUB_STEP_SUMMARY when that is
set (the first run of the workflows does this). --require-recorded, or
IMTYPESET_HEADERS_REQUIRE_RECORDED=1, refuses a fetch whose values are not
recorded: a release build sets it (tools/wheel/build_wheel.py --release).

Needs Python 3.11 (tomllib) and git. Standard library only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import tomllib
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PINS = os.path.join(HERE, "bundle_pins.toml")

FORK_SOURCE = "https://github.com/pthom/imgui.git"
STANDIN_SOURCE = "https://github.com/ocornut/imgui.git"
CONFIG_PATH = "imgui_bundle_cmake/imgui_bundle_config.h"
CONFIG_URL = "https://raw.githubusercontent.com/pthom/imgui_bundle/{commit}/{path}"

REQUIRED = ("bundle_tag", "bundle_commit", "imgui_version", "imgui_commit", "config_sha256",
            "standin_tag", "standin_commit")
# The recorded sha256 values of the headers themselves, per kind: (key, what of).
RECORDED = {
    "fork": (("imgui_h_sha256", "imgui.h"), ("imgui_headers_sha256", None)),
    "stand-in": (("standin_headers_sha256", None),),
}


class FetchError(Exception):
    """Something could not be fetched, or was not what the pins record."""


class UnknownVersion(FetchError):
    """bundle_pins.toml has no entry for the wheel version asked for."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_pin(version: str, pins_path: str = PINS) -> dict:
    """The entry for one wheel version. UnknownVersion says how to add one."""
    with open(pins_path, "rb") as handle:
        table = tomllib.load(handle).get("bundle", {})
    if version not in table:
        known = ", ".join(sorted(table)) or "none"
        raise UnknownVersion(
            f"imgui-bundle {version} is not in {pins_path} (known: {known}). This repository "
            "does not guess which Dear ImGui a wheel was built from. To add the version: derive "
            "the entry in a clone of github.com/pthom/imgui_bundle that has the tag "
            f"v{version}, by the commands at the top of that file, and add it there."
        )
    pin = dict(table[version])
    missing = [key for key in REQUIRED if not pin.get(key)]
    if missing:
        raise FetchError(f"{pins_path}: the entry for {version} lacks {', '.join(missing)}")
    return pin


def _git(arguments: list, cwd: str | None = None) -> bytes:
    environment = dict(os.environ, GIT_TERMINAL_PROMPT="0", LC_ALL="C")
    done = subprocess.run(["git", *arguments], cwd=cwd, env=environment, capture_output=True)
    if done.returncode != 0:
        raise FetchError(
            f"git {' '.join(arguments)} ended with status {done.returncode}: "
            + done.stderr.decode("utf-8", "replace").strip()
        )
    return done.stdout


def fetch_git_headers(source: str, commit: str, destination: str) -> dict:
    """Every top-level *.h and LICENSE.txt of `commit` in the repository `source`
    (a URL or a local path), written into `destination`. {name: sha256}.

    The commit is asked for by its id and what arrives is checked to BE that
    commit; git names every object by the hash of its content, so the id is the
    checksum of all of it.
    """
    os.makedirs(destination, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=os.path.dirname(os.path.abspath(destination))) as work:
        _git(["init", "-q", work])
        _git(["-c", "fetch.fsckObjects=true", "fetch", "-q", "--depth", "1", "--no-tags",
              source, commit], cwd=work)
        got = _git(["rev-parse", "FETCH_HEAD^{commit}"], cwd=work).decode("ascii").strip()
        if got != commit:
            raise FetchError(f"{source} answered commit {got} when asked for {commit}")
        names = _git(["ls-tree", "--name-only", "FETCH_HEAD"], cwd=work).decode("utf-8").split("\n")
        wanted = sorted(n for n in names if n.endswith(".h") or n == "LICENSE.txt")
        if "imgui.h" not in wanted or "imconfig.h" not in wanted:
            raise FetchError(f"{source} at {commit} has no imgui.h and imconfig.h at its top level")
        sums = {}
        for name in wanted:
            data = _git(["cat-file", "blob", f"FETCH_HEAD:{name}"], cwd=work)
            with open(os.path.join(destination, name), "wb") as handle:
                handle.write(data)
            sums[name] = sha256(data)
    return sums


def fetch_url(url: str, want_sha256: str) -> bytes:
    """The bytes at `url` (https:// or file://), which must have this sha256."""
    try:
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
    except (urllib.error.URLError, OSError, ValueError) as exc:
        raise FetchError(f"{url}: {exc}") from exc
    got = sha256(data)
    if got != want_sha256:
        raise FetchError(f"{url}: sha256 is {got}, and {want_sha256} is recorded")
    return data


def read_config(pin: dict, bundle_checkout: str | None, url_template: str) -> tuple:
    """(bytes, where from) of the bundle's ImGui configuration header at the
    release tag's commit: from a local clone's git objects when it has that
    commit, else by URL. The recorded sha256 is checked either way."""
    commit = pin["bundle_commit"]
    if bundle_checkout and os.path.isdir(bundle_checkout):
        try:
            data = _git(["-C", bundle_checkout, "cat-file", "blob", f"{commit}:{CONFIG_PATH}"])
        except FetchError:
            data = None
        if data is not None:
            got = sha256(data)
            if got != pin["config_sha256"]:
                raise FetchError(
                    f"{bundle_checkout} {commit}:{CONFIG_PATH}: sha256 is {got}, and "
                    f"{pin['config_sha256']} is recorded"
                )
            return data, f"{os.path.abspath(bundle_checkout)} (git objects) {commit}:{CONFIG_PATH}"
    url = url_template.format(commit=commit, path=CONFIG_PATH)
    return fetch_url(url, pin["config_sha256"]), url


def headers_digest(sums: dict) -> str:
    """One sha256 for a set of fetched files, {name: sha256}: of the lines
    "<sha256>  <name>" in name order, which is what `sha256sum` prints for
    them in the imgui directory, so `sha256sum $(ls | LC_ALL=C sort) | sha256sum`
    gives it again by hand."""
    return sha256("".join(f"{value}  {name}\n" for name, value in sorted(sums.items())).encode("utf-8"))


def _sums_of(directory: str) -> dict:
    """{name: sha256} of the header files in a fetched imgui directory."""
    sums = {}
    for name in sorted(os.listdir(directory)):
        with open(os.path.join(directory, name), "rb") as handle:
            sums[name] = sha256(handle.read())
    return sums


def recorded_problems(pin: dict, kind: str, sums: dict) -> tuple:
    """(what the pins record for this kind and the files do not have; the
    lines to add to the pins for what they do not record yet)."""
    wrong, unrecorded = [], []
    for key, name in RECORDED[kind]:
        have = sums.get(name, "") if name else headers_digest(sums)
        if not pin.get(key):
            unrecorded.append(f'{key} = "{have}"')
        elif pin[key] != have:
            wrong.append(f"{key}: {name or 'the fetched headers'} hash to {have}, and "
                         f"{pin[key]} is recorded")
    return wrong, unrecorded


def _cached(directory: str, pin: dict, kind: str) -> bool:
    """True when `directory` holds exactly what the PINS record for this kind:
    the configuration header by config_sha256 and the headers by every value
    of RECORDED. False when a value is not recorded: nothing would vouch for
    the files but the cache itself."""
    try:
        with open(os.path.join(directory, "imgui_bundle_config.h"), "rb") as handle:
            if sha256(handle.read()) != pin["config_sha256"]:
                return False
        sums = _sums_of(os.path.join(directory, "imgui"))
        if "imgui.h" not in sums or "imconfig.h" not in sums:
            return False
        wrong, unrecorded = recorded_problems(pin, kind, sums)
        return not wrong and not unrecorded
    except (OSError, ValueError, KeyError, TypeError):
        return False


def _announce(version: str, lines: list, log) -> None:
    """Says which values bundle_pins.toml does not record yet, as the lines to
    add to it; in a workflow, on the job's summary page as well."""
    text = [f"bundle_pins.toml does not record these for imgui-bundle {version} yet. They are what "
            "was just fetched by commit id; add them under",
            f'[bundle."{version}"] in python/host/bundle_pins.toml:'] + ["    " + line for line in lines]
    for line in text:
        log(line)
    summary = os.environ.get("GITHUB_STEP_SUMMARY", "")
    if summary:
        try:
            with open(summary, "a", encoding="utf-8") as handle:
                handle.write("\n### Header checksums to record\n\n```\n" + "\n".join(text) + "\n```\n")
        except OSError as exc:
            log(f"(could not write the job summary: {exc})")


def fetch(version: str, kind: str = "fork", cache: str | None = None, pins_path: str = PINS,
          imgui_sources: list | None = None, bundle_checkout: str | None = None,
          config_url: str = CONFIG_URL, log=lambda line: None,
          require_recorded: bool | None = None) -> dict:
    """Fetches (or finds in the cache) the headers for one wheel version.
    {"dir", "label", "kind", "from_cache", "source", "unrecorded"}; "unrecorded"
    is the lines the pins lack. Raises FetchError."""
    if require_recorded is None:
        require_recorded = os.environ.get("IMTYPESET_HEADERS_REQUIRE_RECORDED", "") not in ("", "0")
    if kind not in ("fork", "stand-in"):
        raise FetchError(f"--kind is 'fork' or 'stand-in', not {kind!r}")
    pin = load_pin(version, pins_path)
    if kind == "fork":
        commit, label = pin["imgui_commit"], f"fork:{pin['imgui_commit']}"
        defaults = [FORK_SOURCE]
        submodule = os.path.join(bundle_checkout or "", "external", "imgui", "imgui")
        if bundle_checkout and os.path.exists(os.path.join(submodule, ".git")):
            defaults.insert(0, submodule)
    else:
        commit, label = pin["standin_commit"], f"stand-in:ocornut-{pin['standin_tag']}"
        defaults = [STANDIN_SOURCE]
    sources = list(imgui_sources or defaults)

    cache = cache or os.environ.get("IMTYPESET_HEADER_CACHE") or os.path.join(
        REPO, "build", "bundle-headers")
    directory = os.path.join(os.path.abspath(cache), version, kind)
    expect = {"bundle_version": version, "kind": kind, "imgui_commit": commit,
              "bundle_commit": pin["bundle_commit"], "config_sha256": pin["config_sha256"]}
    expect.update({key: pin.get(key, "") for key, _ in RECORDED[kind]})
    if _cached(directory, pin, kind):
        log(f"headers for imgui-bundle {version} ({label}): cached in {directory}, and every "
            "file is what the pins record")
        try:
            with open(os.path.join(directory, "SOURCE.json"), encoding="utf-8") as handle:
                source = json.load(handle)
        except (OSError, ValueError):
            source = dict(expect, label=label)  # it says where from, and vouches for nothing
        return {"dir": directory, "label": label, "kind": kind, "from_cache": True,
                "source": source, "unrecorded": []}

    os.makedirs(os.path.dirname(directory), exist_ok=True)
    work = tempfile.mkdtemp(prefix=kind + ".", dir=os.path.dirname(directory))
    try:
        sums, used, refusals = None, None, []
        for source in sources:
            try:
                log(f"fetching Dear ImGui {commit} from {source}")
                sums = fetch_git_headers(source, commit, os.path.join(work, "imgui"))
                used = source
                break
            except FetchError as exc:
                refusals.append(str(exc))
                shutil.rmtree(os.path.join(work, "imgui"), ignore_errors=True)
        if sums is None:
            raise FetchError(
                f"the {kind} headers for imgui-bundle {version} (Dear ImGui commit {commit}) "
                "could not be fetched: " + " | ".join(refusals)
            )
        wrong, unrecorded = recorded_problems(pin, kind, sums)
        if wrong:
            raise FetchError(f"{used} at {commit}: " + "; ".join(wrong))
        if unrecorded:
            _announce(version, unrecorded, log)
            if require_recorded:
                raise FetchError(
                    f"the {kind} headers for imgui-bundle {version} were fetched, and "
                    f"{pins_path} does not record their checksums ({', '.join(u.split(' ')[0] for u in unrecorded)}): "
                    "a release is built only from recorded headers. Add the lines above."
                )
        config, config_from = read_config(pin, bundle_checkout, config_url)
        with open(os.path.join(work, "imgui_bundle_config.h"), "wb") as handle:
            handle.write(config)
        files = {f"imgui/{name}": value for name, value in sums.items()}
        files["imgui_bundle_config.h"] = sha256(config)
        source = dict(expect, label=label, imgui_source=used, config_source=config_from,
                      sha256=files)
        with open(os.path.join(work, "SOURCE.json"), "w", encoding="utf-8") as handle:
            json.dump(source, handle, indent=1, sort_keys=True)
            handle.write("\n")
        shutil.rmtree(directory, ignore_errors=True)
        os.replace(work, directory)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for relative, value in sorted(files.items()):
        log(f"  {value}  {relative}")
    return {"dir": directory, "label": label, "kind": kind, "from_cache": False, "source": source,
            "unrecorded": unrecorded}


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bundle-version", required=True)
    parser.add_argument("--kind", default="fork", choices=("fork", "stand-in"))
    parser.add_argument("--cache", default=None)
    parser.add_argument("--pins", default=PINS)
    parser.add_argument("--imgui-source", action="append", default=None,
                        help="a git URL or local repository to fetch Dear ImGui from, in place "
                             "of GitHub; may be given more than once")
    parser.add_argument("--bundle-checkout", default=None,
                        help="a local clone of Dear ImGui Bundle to read the configuration "
                             "header (and the fork, if its submodule is there) from")
    parser.add_argument("--config-url", default=CONFIG_URL,
                        help="where the configuration header is, with {commit} and {path}")
    parser.add_argument("--require-recorded", action="store_true", default=None,
                        help="refuse headers whose checksums bundle_pins.toml does not record "
                             "(also IMTYPESET_HEADERS_REQUIRE_RECORDED=1)")
    options = parser.parse_args(argv)

    def log(line: str) -> None:
        print("fetch_headers.py: " + line, file=sys.stderr)

    try:
        result = fetch(options.bundle_version, options.kind, options.cache, options.pins,
                       options.imgui_source, options.bundle_checkout, options.config_url, log,
                       options.require_recorded)
    except UnknownVersion as exc:
        log(str(exc))
        return 2
    except FetchError as exc:
        log(str(exc))
        return 1
    print("dir=" + result["dir"])
    print("label=" + result["label"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
