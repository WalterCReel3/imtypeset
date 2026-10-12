"""Does the imtypeset sidecar work beside the imgui-bundle wheel installed in
this interpreter, on this machine? Answers by doing it, and writes down
everything it saw whether the answer is yes or no.

    <python> -P -B tools/ci/sidecar_probe.py --out DIR
        [--bundle-checkout DIR] [--also-lookup] [--mutate]
        [--expect-no-wheel] [--strict]

`<python>` is the interpreter whose wheel is in question (3.11 or later). It
is the only thing this needs besides a C++ compiler, git, and nm.

Stages, each of which records what it ran, its exit status and the end of its
output, and none of which stops the ones after it that can still run:

  1. the machine: system, compiler, linker, how this Python loads extension
     modules;
  2. the wheel: version, the module's file, its kind and what it links, and
     whether its symbols are visible to the rest of the process once imported;
  3. what the module exports, against the Dear ImGui symbols a Linux build of
     the library needs (tools/ci/expected_imgui_symbols.txt): an answer to
     "are the symbols there" that needs nothing to compile here;
  4. the library, built by python/host/build_bundle_library.sh against the
     wheel's own headers (fetched); and, only if that does not end in an
     accepted library, against stand-in headers, so that a failure of the
     headers does not hide the answer about binding;
     with the same comparison as 3 for the objects compiled HERE, the link, and
     the audit of what was linked;
  5. the load: the import gate (Dear ImGui's own layout check and the field
     probe), `import imtypeset`, and the headless checks of tools/bundle_route.
     A library bound by lookup is tried three ways: with nothing loaded first,
     with imgui_bundle imported first, and with its module made global first.

DIR gets report.md (the verdict first), verdict.json, and a log or listing per
stage. report-partial.md is rewritten after every stage, so a run that is
killed leaves what it had. Exit 0 whatever was found, unless --strict: then 1
when the verdict is not "works, with the wheel's own headers".

--expect-no-wheel: for a platform PyPI has no wheel for. The verdict is then
"ok" exactly when imgui_bundle cannot be imported, and the report says so.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import sysconfig
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import sidecar_symbols  # noqa: E402

LINUX = sys.platform.startswith("linux")
MACOS = sys.platform == "darwin"
BUILD = os.path.join(REPO, "python", "host", "build_bundle_library.sh")
PRELOAD = os.path.join(HERE, "preload")
# Address space for a Python process that loads the library, where the system
# enforces one (Linux). A runaway probe must end itself and not the machine.
LIMIT_KB = 6_000_000


class Report:
    def __init__(self, out: str):
        self.out = out
        self.sections = []
        self.started = time.time()

    def add(self, title: str, body: str) -> None:
        self.sections.append((title, body.rstrip() + "\n"))
        self.write("report-partial.md", "# Sidecar probe (unfinished)\n\n")

    def write(self, name: str, head: str) -> None:
        with open(os.path.join(self.out, name), "w", encoding="utf-8") as handle:
            handle.write(head)
            for title, body in self.sections:
                handle.write(f"## {title}\n\n{body}\n")

    def save(self, name: str, text: str) -> str:
        with open(os.path.join(self.out, name), "w", encoding="utf-8", errors="replace") as handle:
            handle.write(text)
        return name


def run(command: list, env: dict | None = None, timeout: int = 900, limit: bool = False) -> dict:
    """Runs one command; never raises. {"status", "output", "seconds", "command"}."""
    if limit and LINUX:
        command = ["sh", "-c", f"ulimit -v {LIMIT_KB}; exec \"$@\"", "sh", *command]
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    environment.update(env or {})
    start = time.time()
    try:
        done = subprocess.run(command, env=environment, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=timeout)
        status, output = done.returncode, done.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as exc:
        status = "timeout"
        output = (exc.stdout or b"").decode("utf-8", "replace") + f"\n[killed after {timeout} s]"
    except OSError as exc:
        status, output = "not run", f"{type(exc).__name__}: {exc}"
    return {"status": status, "output": output, "seconds": time.time() - start,
            "command": " ".join(command[-6:]) if limit and LINUX else " ".join(command)}


def tail(text: str, lines: int = 40) -> str:
    kept = text.rstrip().splitlines()
    cut = f"[... {len(kept) - lines} earlier lines in the log ...]\n" if len(kept) > lines else ""
    return cut + "\n".join(kept[-lines:])


def fenced(text: str) -> str:
    return "```\n" + (text.rstrip() or "(no output)") + "\n```\n"


def first_line(command: list) -> str:
    result = run(command, timeout=60)
    lines = result["output"].strip().splitlines()
    return lines[0] if lines and result["status"] == 0 else f"({result['status']}: {' '.join(command)})"


def dlopen_flags() -> str:
    flags = sys.getdlopenflags()
    names = [name for name in ("RTLD_LAZY", "RTLD_NOW", "RTLD_GLOBAL", "RTLD_LOCAL", "RTLD_NODELETE")
             if getattr(os, name, 0) and flags & getattr(os, name)]
    return f"{flags:#x} ({' | '.join(names) or 'no named bit'}); RTLD_GLOBAL is " \
           f"{getattr(os, 'RTLD_GLOBAL', '?'):#x}, RTLD_LOCAL is {getattr(os, 'RTLD_LOCAL', 0):#x}"


# --- 1. the machine ---------------------------------------------------------------------

def machine(report: Report, cxx: str) -> None:
    rows = [
        ("system", f"{platform.system()} {platform.release()} {platform.machine()}"),
        ("platform", platform.platform()),
        ("python", f"{sys.version.split()[0]} at {sys.executable}"),
        ("extension suffix", str(sysconfig.get_config_var("EXT_SUFFIX"))),
        ("how this Python links an extension module (LDSHARED)",
         str(sysconfig.get_config_var("LDSHARED"))),
        ("how this Python loads one (sys.getdlopenflags())", dlopen_flags()),
        ("compiler", f"`{cxx}`: " + first_line([cxx, "--version"])),
        ("git", first_line(["git", "--version"])),
    ]
    if MACOS:
        rows += [
            ("macOS", first_line(["sw_vers", "-productVersion"])),
            ("Xcode", first_line(["xcodebuild", "-version"])),
            ("linker", first_line(["ld", "-v"])),
            ("Python's MACOSX_DEPLOYMENT_TARGET",
             str(sysconfig.get_config_var("MACOSX_DEPLOYMENT_TARGET"))),
        ]
    else:
        rows.append(("linker", first_line(["ld", "--version"])))
    report.add("1. The machine", "\n".join(f"- **{k}**: {v}" for k, v in rows))


# --- 2. the wheel -----------------------------------------------------------------------

_WHEEL_FACTS = r"""
import json, sys
facts = {}
try:
    import imgui_bundle
    from imgui_bundle import imgui
    facts["version"] = imgui_bundle.__version__
    facts["imgui_version"] = imgui.get_version()
    facts["module"] = imgui_bundle._imgui_bundle.__file__
    try:
        from importlib import metadata
        wheel = metadata.distribution("imgui-bundle").read_text("WHEEL") or ""
        facts["tags"] = [l.split(":", 1)[1].strip() for l in wheel.splitlines() if l.startswith("Tag:")]
    except Exception as exc:
        facts["tags"] = [f"{type(exc).__name__}: {exc}"]
    # Is a Dear ImGui symbol of the module visible to the rest of the process?
    # dlopen(NULL) answers for what a later library's lookup would search.
    import ctypes
    name = "_ZN5ImGui5GetIOEv"
    facts["visible_after_import"] = hasattr(ctypes.CDLL(None), name)
    facts["in_the_module_itself"] = hasattr(ctypes.CDLL(facts["module"]), name)
    ctypes.CDLL(facts["module"], mode=ctypes.RTLD_GLOBAL)
    facts["visible_after_reopening_global"] = hasattr(ctypes.CDLL(None), name)
except Exception as exc:
    facts["error"] = f"{type(exc).__name__}: {exc}"
print("WHEEL-FACTS " + json.dumps(facts))
"""


def wheel(report: Report) -> dict:
    result = run([sys.executable, "-P", "-B", "-c", _WHEEL_FACTS], timeout=300, limit=True)
    facts = {"error": f"the child ended with status {result['status']}"}
    for line in result["output"].splitlines():
        if line.startswith("WHEEL-FACTS "):
            facts = json.loads(line[len("WHEEL-FACTS "):])
    pip = run([sys.executable, "-m", "pip", "--version"], timeout=60)["output"].strip()
    if "module" not in facts:
        report.add("2. The wheel", "**imgui_bundle cannot be imported in this interpreter**: "
                   f"`{facts.get('error')}`\n\n" + fenced(tail(result["output"], 30)) + f"\npip: {pip}\n")
        return facts
    module = facts["module"]
    body = [
        f"- **imgui-bundle**: {facts['version']}, Dear ImGui {facts['imgui_version']}",
        f"- **wheel tags**: {', '.join(facts.get('tags', [])) or 'not found'}",
        f"- **module**: `{module}`",
        f"- **its own symbol table has `ImGui::GetIO`** (dlsym on the module's handle): "
        f"{facts.get('in_the_module_itself')}",
        f"- **visible to the whole process after a plain `import imgui_bundle`** "
        f"(dlsym on dlopen(NULL)): **{facts.get('visible_after_import')}**",
        f"- **visible after opening the module again with RTLD_GLOBAL**: "
        f"**{facts.get('visible_after_reopening_global')}**",
        "",
    ]
    commands = [["file", module]]
    commands += [["otool", "-hv", module], ["otool", "-L", module]] if MACOS else [["readelf", "-d", module]]
    for command in commands:
        got = run(command, timeout=60)
        text = got["output"]
        if command[0] == "readelf":
            text = "\n".join(l for l in text.splitlines()
                             if any(k in l for k in ("NEEDED", "SONAME", "RPATH", "RUNPATH")))
        body.append(f"`{' '.join(command[:-1])} <module>` (status {got['status']}):\n\n" + fenced(text))
    report.add("2. The wheel", "\n".join(body))
    return facts


# --- 3 and 5. symbols -------------------------------------------------------------------

def symbols(report: Report, title: str, module: str, needed_from: str | None, stem: str) -> dict | None:
    try:
        result = sidecar_symbols.comparison(module, needed_from)
        if needed_from is None:
            _, raw = sidecar_symbols.exports(module)
            report.save("module-exports.txt", raw)
    except Exception as exc:  # nm missing, or its output not as expected
        report.add(title, f"Could not be listed: `{type(exc).__name__}: {exc}`")
        return None
    report.save(stem + "-needed.txt", "\n".join(result["needed"]) + "\n")
    report.save(stem + "-missing.txt", "\n".join(result["missing"]) + "\n")
    body = [
        f"- the module exports **{result['exported']}** symbols, {result['exported_imgui']} of them "
        "in Dear ImGui's namespace or a class or namespace whose name begins with `Im`",
        f"- needed: **{len(result['needed'])}** Dear ImGui symbols, from {result['needed_from']}",
        f"- of those, the module does not export: **{len(result['missing'])}**",
    ]
    if needed_from is None:
        body.append(
            "\nThe needed list here is NOT from this machine: it is what a Linux x86-64 build "
            "against stand-in headers leaves undefined. Two of its names carry a `std::function` "
            "as libstdc++ mangles it; where the wheel is built with libc++ those two are expected "
            "in the missing list and mean nothing by themselves. The sections that "
            "follow have the list from objects compiled here.")
    if result["missing"]:
        body.append("\nMissing:\n\n" + fenced("\n".join(result["missing"])))
    report.add(title, "\n".join(body))
    return result


# --- 4, 6, 7. build and load --------------------------------------------------------------

def build_step(report: Report, log: list, out: str, step: str, headers: str, bind: str,
               options) -> dict:
    env = {"IMTYPESET_BUNDLE_OUT": out, "IMTYPESET_BUNDLE_STEPS": step,
           "IMTYPESET_BUNDLE_HEADERS": headers, "IMTYPESET_BUNDLE_BIND": bind,
           "IMTYPESET_HEADER_CACHE": os.path.join(report.out, "headers")}
    command = ["sh", BUILD, options.bundle_checkout or os.path.join(report.out, "no-bundle-checkout"),
               sys.executable]
    result = run(command, env=env, timeout=1800)
    name = report.save(f"build-{headers}-{bind}-{step}.log", result["output"])
    log.append(f"**{step}** (headers={headers}, bind={bind}): status **{result['status']}**, "
               f"{result['seconds']:.0f} s, log `{name}`\n\n" + fenced(tail(result["output"], 25)))
    return result


def gate_json(output: str) -> dict:
    """The JSON object tools/bundle_route/run_gate.py prints between its lines."""
    lines = output.splitlines()
    try:
        start = lines.index("{")
        end = lines.index("}", start)
        return json.loads("\n".join(lines[start:end + 1]))
    except ValueError:
        return {}


def load(report: Report, log: list, library: str, headers: str, bind: str, options) -> dict:
    """Tries the library. {"accepted", "preload", "imgui_check", "checks_ok", ...}."""
    answer = {"accepted": False, "preload": None, "imgui_check": None, "checks_ok": False,
              "probes_failed": None, "accepted_by": None}
    modes = [""] if bind == "link" else ["", "import", "1"]
    # With nothing asked of the harness, the loading is the package's own
    # (_bundle.open_library): `import imgui_bundle` first and, off Linux, the
    # module reopened RTLD_GLOBAL if the load still fails. The gate's log says
    # which it took, on its `preload:` line.
    described = {"": "the package's own loading, nothing more (see `preload:` in the log)",
                 "import": "`import imgui_bundle` first",
                 "1": "`import imgui_bundle` and its module reopened RTLD_GLOBAL first"}
    stem = f"load-{headers}-{bind}"
    for mode in modes:
        env = {"IMTYPESET_PRELOAD_BUNDLE": mode,
               "PYTHONPATH": os.pathsep.join(filter(None, [PRELOAD, os.environ.get("PYTHONPATH", "")]))}
        gate = run([sys.executable, "-P", "-B", os.path.join(REPO, "tools", "bundle_route", "run_gate.py"),
                    library], env=env, timeout=600, limit=True)
        name = report.save(f"{stem}-gate-preload-{mode or 'none'}.log", gate["output"])
        facts = gate_json(gate["output"])
        log.append(f"**gate, {described[mode]}**: status **{gate['status']}**, log `{name}`\n\n"
                   + fenced(tail("\n".join(l for l in gate["output"].splitlines()
                                           if not l.startswith("PASS ")), 30)))
        if gate["status"] == 0 and answer["preload"] is None:
            answer.update(accepted=True, preload=mode, imgui_check=facts.get("imgui_check"),
                          probes_failed=facts.get("probes_failed"),
                          imgui_check_message=facts.get("imgui_check_message"),
                          headers_label=facts.get("headers"), env=env)
        elif answer["preload"] is None and facts:
            answer.update(imgui_check=facts.get("imgui_check"),
                          probes_failed=facts.get("probes_failed"),
                          imgui_check_message=facts.get("imgui_check_message"))
    if not answer["accepted"]:
        return answer

    env = dict(answer.pop("env"), IMTYPESET_LIBRARY=library)
    env["PYTHONPATH"] = os.pathsep.join([os.path.join(REPO, "python"), env["PYTHONPATH"]])
    checks = [
        ("import imtypeset (the package's own gate)",
         [sys.executable, "-P", "-B", "-c",
          "import json, imgui_bundle, imtypeset\n"
          "info = imtypeset.host_info()\n"
          "bundle = info.get('bundle') or {}\n"
          "print(json.dumps({k: info.get(k) for k in ('host', 'library')}))\n"
          "print(json.dumps({k: bundle.get(k) for k in ('headers', 'accepted_by', 'checked_in', "
          "'imgui_check', 'imgui_check_message', 'probes_passed', 'probes_failed')}))\n"]),
        ("tools/bundle_route/check_document_edits.py",
         [sys.executable, "-P", "-B", os.path.join(REPO, "tools", "bundle_route", "check_document_edits.py")]),
        ("tools/bundle_route/check_swap_answers.py",
         [sys.executable, "-P", "-B", os.path.join(REPO, "tools", "bundle_route", "check_swap_answers.py")]),
    ]
    if options.mutate:
        checks.append(("tools/bundle_route/mutate_gate.py (does the probe notice a moved offset)",
                       [sys.executable, "-P", "-B",
                        os.path.join(REPO, "tools", "bundle_route", "mutate_gate.py"), library]))
    statuses = []
    for index, (what, command) in enumerate(checks):
        got = run(command, env=env, timeout=1500, limit=True)
        name = report.save(f"{stem}-check-{index}.log", got["output"])
        shown = "\n".join(l for l in got["output"].splitlines() if not l.startswith("PASS "))
        passed = sum(1 for l in got["output"].splitlines() if l.startswith("PASS "))
        log.append(f"**{what}**: status **{got['status']}**, {passed} PASS lines, log `{name}`\n\n"
                   + fenced(tail(shown, 15)))
        statuses.append(got["status"] == 0)
    answer["checks_ok"] = all(statuses)
    answer["package_import_ok"] = statuses[0]
    return answer


def attempt(report: Report, headers: str, bind: str, module: str, options, number: str) -> dict:
    """One library: fetch, compile, link, audit, load. The whole outcome."""
    out = os.path.join(report.out, f"lib-{headers}-{bind}")
    reuse = os.path.join(report.out, f"lib-{headers}-{options.default_bind}")
    log, outcome = [], {"headers": headers, "bind": bind, "reached": "nothing"}
    title = f"{number} A library with {headers} headers, bound by {bind}"
    try:
        if bind != options.default_bind and os.path.isdir(os.path.join(reuse, "obj")):
            # The same objects, linked the other way.
            shutil.rmtree(out, ignore_errors=True)
            shutil.copytree(reuse, out, ignore=shutil.ignore_patterns("libimtypeset_bundle.*", "LIBRARY"))
            steps = ["link", "audit"]
        else:
            steps = ["fetch", "compile", "link", "audit"]
        for step in steps:
            result = build_step(report, log, out, step, headers, bind, options)
            if result["status"] != 0:
                outcome["failed_at"] = step
                if step == "audit":
                    outcome["reached"] = "link"
                    break  # an audit that objects is recorded; the load is still tried
                return outcome
            outcome["reached"] = step
            if step == "compile":
                compared = symbols(report, f"Symbols the objects compiled here need ({headers} "
                                   f"headers; belongs to section {number} below)", module,
                                   os.path.join(out, "obj"), f"objects-{headers}")
                outcome["missing"] = None if compared is None else compared["missing"]
                outcome["needed"] = None if compared is None else len(compared["needed"])
        with open(os.path.join(out, "LIBRARY"), encoding="utf-8") as handle:
            library = handle.read().strip()
        if MACOS:
            for command in (["otool", "-L", library], ["nm", "-m", "-u", library]):
                got = run(command, timeout=60)
                name = report.save(f"library-{headers}-{bind}-{command[0]}.txt", got["output"])
                log.append(f"`{' '.join(command[:-1])} <library>` (status {got['status']}), whole in "
                           f"`{name}`\n\n" + fenced(tail(got["output"], 12)))
        outcome.update(load(report, log, library, headers, bind, options))
        outcome["reached"] = "accepted" if outcome["accepted"] else "load"
        outcome["library"] = library
        return outcome
    except Exception as exc:  # a defect of this script must not lose the report
        log.append(f"**sidecar_probe.py itself failed here**: `{type(exc).__name__}: {exc}`")
        outcome["failed_at"] = "sidecar_probe.py"
        return outcome
    finally:
        report.add(title, "\n".join(log))


# --- the verdict --------------------------------------------------------------------------

def works(outcome: dict | None) -> bool:
    return bool(outcome and outcome.get("accepted") and outcome.get("checks_ok")
                and "failed_at" not in outcome)


def verdict(facts: dict, expected: dict | None, outcomes: dict, options) -> tuple:
    """(verdict as data, the lines that open the report)."""
    where = f"{platform.system()} {platform.machine()}, CPython {sys.version.split()[0]}"
    data = {"platform": where, "ok": False, "h1": "untested", "h2": "untested",
            "wheel": facts.get("version"), "outcomes": outcomes}
    lines = [f"**Where:** {where}; imgui-bundle {facts.get('version', 'NOT INSTALLED')}.", ""]
    if "module" not in facts:
        data["h1"] = "no wheel"
        data["ok"] = bool(options.expect_no_wheel)
        lines.append("**H1 (does the sidecar work here): NOT TESTED. imgui_bundle cannot be imported "
                     "in this interpreter**, so there is nothing to load beside. "
                     + ("That was expected for this platform (no wheel on PyPI when this was written)."
                        if options.expect_no_wheel else "Section 2 has pip's and Python's words."))
        return data, lines
    if options.expect_no_wheel:
        lines.append("**Note: this job expected NO wheel for this platform, and there is one.** "
                     "What follows is a real answer for it; the expectation is what is out of date.\n")

    fork = outcomes.get(f"fork-{options.default_bind}")
    stand_in = outcomes.get(f"stand-in-{options.default_bind}")
    best = fork if works(fork) else stand_in if works(stand_in) else None
    how = {None: "", "": "by the package's own loading (the wheel's module imported first; the "
           "gate's `preload:` line says whether it was also reopened RTLD_GLOBAL)", "import": "once `imgui_bundle` is imported first "
           "(nothing more)", "1": "only after the wheel's module is reopened with RTLD_GLOBAL"}
    if best is not None:
        data["h1"] = "works"
        lines.append(
            f"**H1 (does the sidecar work here): YES.** A library bound by `{best['bind']}`, built "
            f"against {best['headers']} headers, was accepted by the import gate "
            f"{how[best['preload']]}, `import imtypeset` succeeded, and the headless checks passed.")
    else:
        tried = [o for o in (fork, stand_in) if o]
        data["h1"] = "fails" if any(o["reached"] in ("link", "load", "audit") for o in tried) else "not reached"
        lines.append("**H1 (does the sidecar work here): NO LIBRARY WAS ACCEPTED.** How far each got: "
                     + "; ".join(f"{o['headers']} headers: reached `{o['reached']}`"
                                 + (f", failed at `{o['failed_at']}`" if "failed_at" in o else "")
                                 + ("" if o.get("checks_ok", True) or not o.get("accepted")
                                    else ", accepted but a check failed")
                                 for o in tried) + ". The sections below have each stage's output.")

    for outcome in (fork, stand_in):
        if outcome and outcome.get("missing") is not None:
            lines.append(f"- Symbols ({outcome['headers']} headers, compiled here): "
                         f"{outcome['needed']} Dear ImGui symbols needed, "
                         f"**{len(outcome['missing'])} not exported by the wheel's module**"
                         + (": " + ", ".join(f"`{n}`" for n in outcome["missing"][:8])
                            + (" ..." if len(outcome["missing"]) > 8 else "") if outcome["missing"] else "")
                         + ".")
    if expected is not None:
        lines.append(f"- Symbols (the Linux list, nothing compiled here): {len(expected['needed'])} "
                     f"needed, {len(expected['missing'])} not exported.")
    lines.append(f"- Module visible to the process after a plain import: "
                 f"**{facts.get('visible_after_import')}**; after reopening it global: "
                 f"**{facts.get('visible_after_reopening_global')}**.")

    if fork is None:
        lines.append("\n**H2 (the wheel's own headers): not attempted.**")
    elif works(fork) and fork.get("imgui_check"):
        data["h2"] = "works"
        lines.append(f"\n**H2 (the wheel's own headers): YES.** Fetched at the pinned commit, "
                     f"compiled, and Dear ImGui's own `DebugCheckVersionAndDataLayout` accepts the "
                     f"library (`{fork.get('headers_label')}`).")
    else:
        data["h2"] = "fails"
        lines.append(f"\n**H2 (the wheel's own headers): NO.** Reached `{fork['reached']}`"
                     + (f", failed at `{fork['failed_at']}`" if "failed_at" in fork else "")
                     + (f"; Dear ImGui's own check: `{fork.get('imgui_check_message')}`"
                        if fork.get("imgui_check") is False else "") + ".")
    extra = outcomes.get("fork-lookup") if options.default_bind == "link" else None
    extra = extra or (outcomes.get("stand-in-lookup") if options.default_bind == "link" else None)
    if extra:
        lines.append(f"\nFor comparison, the same objects bound by `lookup` on this platform: "
                     + (f"accepted {how[extra['preload']]}." if extra.get("accepted")
                        else f"not accepted (reached `{extra['reached']}`)."))
    data["ok"] = data["h1"] == "works" and data["h2"] == "works"
    return data, lines


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", required=True)
    parser.add_argument("--bundle-checkout", default=None)
    parser.add_argument("--also-lookup", action="store_true",
                        help="where the default binding is `link`, also link and load the same "
                             "objects bound by lookup")
    parser.add_argument("--mutate", action="store_true", help="also run mutate_gate.py (minutes)")
    parser.add_argument("--expect-no-wheel", action="store_true")
    parser.add_argument("--only-stand-in", action="store_true",
                        help="do not try the wheel's own headers (for a machine without network)")
    parser.add_argument("--strict", action="store_true")
    options = parser.parse_args(argv)
    options.default_bind = "lookup" if MACOS else "link"
    os.makedirs(options.out, exist_ok=True)
    options.out = os.path.abspath(options.out)
    report = Report(options.out)
    cxx = os.environ.get("CXX") or ("c++" if MACOS else "g++")

    machine(report, cxx)
    facts = wheel(report)
    expected, outcomes = None, {}
    if "module" in facts:
        module = facts["module"]
        expected = symbols(report, "3. What the module exports, against the Linux list", module,
                           None, "expected")
        kinds = ["stand-in"] if options.only_stand_in else ["fork", "stand-in"]
        number = 4
        for headers in kinds:
            key = f"{headers}-{options.default_bind}"
            outcomes[key] = attempt(report, headers, options.default_bind, module, options, f"{number}.")
            number += 1
            if options.also_lookup and options.default_bind == "link" \
                    and outcomes[key]["reached"] not in ("nothing", "fetch"):
                outcomes[f"{headers}-lookup"] = attempt(report, headers, "lookup", module, options,
                                                        f"{number}.")
                number += 1
            if works(outcomes[key]):
                break  # stand-in headers are tried only when the wheel's own did not get there

    data, lines = verdict(facts, expected, outcomes, options)
    data["seconds"] = round(time.time() - report.started)
    head = "# Sidecar probe: " + data["platform"] + "\n\n" + "\n".join(lines) + "\n\n" \
           f"Written by `tools/ci/sidecar_probe.py` in {data['seconds']} s. Everything below is " \
           "what ran on this machine; a number or a message here was printed by a tool, not " \
           "recalled.\n\n"
    report.write("report.md", head)
    with open(os.path.join(options.out, "verdict.json"), "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=1, default=str)
        handle.write("\n")
    try:
        os.remove(os.path.join(options.out, "report-partial.md"))
    except OSError:
        pass
    print(head)
    print(f"sidecar_probe.py: report in {os.path.join(options.out, 'report.md')}; "
          f"ok={data['ok']} h1={data['h1']} h2={data['h2']}")
    return 1 if options.strict and not data["ok"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
