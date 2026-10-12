"""Checks .github/workflows/*.yml without GitHub: that each file is YAML, and
the rules this repository holds its workflows to.

    python3 -B tools/ci/check_workflows.py        (needs PyYAML; exit 77 without it)

  - `permissions` is exactly `contents: read`, at the top, and no job asks for more;
  - no secret is read;
  - every action is pinned to an exact version (`owner/name@vX.Y.Z`);
  - every job has `timeout-minutes`;
  - every `run` step runs a script under tools/ci that exists, so that a
    workflow holds no logic of its own;
  - what tools/ci/sidecar_matrix.py prints is the matrix sidecar-probe.yml
    reads: every key the workflow uses, and no two jobs with one id.

This does not show that GitHub accepts the files: its expression syntax, its
contexts and the runner labels are checked by the first run there and by
nothing here.
"""

import glob
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PINNED = re.compile(r"^[\w.-]+/[\w.-]+@v\d+\.\d+\.\d+$")
SCRIPT = re.compile(r"tools/ci/[\w./-]+\.(?:sh|py)")


def check(path: str, yaml) -> list:
    problems = []
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    try:
        document = yaml.safe_load(text)
    except yaml.YAMLError as exc:
        return [f"not YAML: {exc}"]
    if not isinstance(document, dict):
        return ["not a mapping"]
    # YAML 1.1, which PyYAML reads, takes the key `on` for the boolean true.
    if "on" not in document and True not in document:
        problems.append("no `on`")
    if document.get("permissions") != {"contents": "read"}:
        problems.append("top-level `permissions` is not exactly `contents: read`")
    if "secrets." in text or "secrets[" in text:
        problems.append("reads a secret")
    jobs = document.get("jobs")
    if not isinstance(jobs, dict) or not jobs:
        return problems + ["no jobs"]
    for name, job in jobs.items():
        if "permissions" in job:
            problems.append(f"job {name}: has its own `permissions`")
        if not isinstance(job.get("timeout-minutes"), int):
            problems.append(f"job {name}: no `timeout-minutes`")
        if "runs-on" not in job:
            problems.append(f"job {name}: no `runs-on`")
        steps = job.get("steps")
        if not isinstance(steps, list) or not steps:
            problems.append(f"job {name}: no steps")
            continue
        for index, step in enumerate(steps):
            where = f"job {name}, step {index + 1}"
            if ("uses" in step) == ("run" in step):
                problems.append(f"{where}: needs exactly one of `uses` and `run`")
            if "uses" in step and not PINNED.match(str(step["uses"])):
                problems.append(f"{where}: `{step['uses']}` is not pinned to an exact version")
            if "run" in step:
                scripts = SCRIPT.findall(str(step["run"]))
                if not scripts:
                    problems.append(f"{where}: runs no script under tools/ci")
                for script in scripts:
                    if not os.path.isfile(os.path.join(REPO, script)):
                        problems.append(f"{where}: {script} does not exist")
    return problems


def check_matrix() -> list:
    done = subprocess.run([sys.executable, "-B", os.path.join(REPO, "tools", "ci", "sidecar_matrix.py")],
                          capture_output=True, text=True, env=dict(os.environ, IMTYPESET_CI_LINUX=""))
    if done.returncode != 0 or not done.stdout.startswith("matrix=") or done.stdout.count("\n") != 1:
        return [f"sidecar_matrix.py did not print one `matrix=` line: {done.stdout!r} {done.stderr!r}"]
    include = json.loads(done.stdout[len("matrix="):])["include"]
    with open(os.path.join(REPO, ".github", "workflows", "sidecar-probe.yml"), encoding="utf-8") as handle:
        used = set(re.findall(r"(?<![.\w])matrix\.(\w+)", handle.read()))
    problems = []
    for entry in include:
        missing = used - set(entry)
        if missing:
            problems.append(f"matrix entry {entry.get('id')} lacks {sorted(missing)}")
    ids = [entry.get("id") for entry in include]
    if len(set(ids)) != len(ids):
        problems.append("two matrix entries share an id (the artifact's name)")
    if not include:
        problems.append("the matrix is empty")
    return problems


def main() -> int:
    try:
        import yaml
    except ImportError:
        print("check_workflows.py: SKIPPED, PyYAML is not installed in this interpreter")
        return 77
    files = sorted(glob.glob(os.path.join(REPO, ".github", "workflows", "*.yml")))
    if not files:
        print("check_workflows.py: no workflow found")
        return 1
    failed = False
    for path in files:
        problems = check(path, yaml)
        if os.path.basename(path) == "sidecar-probe.yml":
            problems += check_matrix()
        print(("FAIL " if problems else "ok   ") + os.path.relpath(path, REPO))
        for problem in problems:
            print("     " + problem)
        failed = failed or bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
