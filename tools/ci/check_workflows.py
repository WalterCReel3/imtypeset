"""Checks .github/workflows/*.yml without GitHub: that each file is YAML, and
the rules this repository holds its workflows to.

    python3 -B tools/ci/check_workflows.py        (needs PyYAML; exit 77 without it)

  - `permissions` is exactly `contents: read`, at the top, and no job asks for
    more, but three, all in wheels.yml, each of which needs the job that
    builds and checks the wheels:
      release           `contents: write`; attaches wheels to a GitHub Release
      publish-pypi      `id-token: write` (PyPI's Trusted Publishing), in the
                        environment `pypi`, and no index URL: PyPI
      publish-testpypi  the same in the environment `testpypi`, with
                        TestPyPI's URL
    `release` and `publish-pypi` run only for a PUSHED TAG v<version>;
    `publish-testpypi` only for a run started by hand with its input set.
    So no push to a branch sends anything anywhere;
  - the index's action is used by those two jobs and by nothing else, takes
    no password, and each job's step before it runs
    tools/ci/wheel_publish.py, which refuses what is not a whole release;
  - no secret is read;
  - every action is named by a commit id, with the exact version it was on
    the same line as a comment (`owner/name@<40 hex> # vX.Y.Z`): a tag can be
    moved, a commit id cannot;
  - no `run` holds a `${{ }}` expression: a value reaches the shell through
    `env:`, as a variable's value, and never as text of the command (a ref
    name or an input may hold characters a shell acts on);
  - every job has `timeout-minutes`;
  - every `run` step runs a script under tools/ci that exists, so that a
    workflow holds no logic of its own;
  - what tools/ci/sidecar_matrix.py prints is the matrix sidecar-probe.yml
    reads, and tools/ci/wheel_matrix.py the one wheels.yml reads: every key the
    workflow uses, and no two jobs with one id.

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
PINNED = re.compile(r"^[\w.-]+/[\w.-]+@[0-9a-f]{40}$")
PINNED_LINE = re.compile(r"uses:\s*([\w.-]+/[\w.-]+@[0-9a-f]{40})\s+#\s*v\d+\.\d+\.\d+\s*$")
SCRIPT = re.compile(r"tools/ci/[\w./-]+\.(?:sh|py)")
PUBLISH_ACTION = "pypa/gh-action-pypi-publish"
TAG_PUSH = "github.event_name == 'push' && startsWith(github.ref, 'refs/tags/v')"
BY_HAND = "github.event_name == 'workflow_dispatch' && github.event.inputs.testpypi == 'true'"
# The jobs of wheels.yml that may do more than read: their permissions, the
# exact condition each runs under, the environment, and the index's URL.
PRIVILEGED = {
    "release": ({"contents": "write"}, TAG_PUSH, None, None),
    "publish-pypi": ({"contents": "read", "id-token": "write"}, TAG_PUSH, "pypi", None),
    "publish-testpypi": ({"contents": "read", "id-token": "write"}, BY_HAND, "testpypi",
                         "https://test.pypi.org/legacy/"),
}


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
    # The version comments, which YAML does not keep: every `uses:` line of
    # the text carries one, and one action is one commit throughout.
    commits = {}
    for line in text.splitlines():
        if re.match(r"\s*(-\s*)?uses:", line):
            found = PINNED_LINE.search(line)
            if not found:
                problems.append(f"`{line.strip()}`: not `owner/name@<commit id> # vX.Y.Z`")
            else:
                name, commit = found.group(1).split("@")
                if commits.setdefault(name, commit) != commit:
                    problems.append(f"{name} is named by two commit ids")
    jobs = document.get("jobs")
    if not isinstance(jobs, dict) or not jobs:
        return problems + ["no jobs"]
    if os.path.basename(path) == "wheels.yml":
        for name in sorted(set(PRIVILEGED) - set(jobs)):
            problems.append(f"no job {name}")
    for name, job in jobs.items():
        privileged = PRIVILEGED.get(name) if os.path.basename(path) == "wheels.yml" else None
        publishes = [step for step in job.get("steps") or []
                     if str(step.get("uses", "")).startswith(PUBLISH_ACTION + "@")]
        environment = job.get("environment")
        environment = environment.get("name") if isinstance(environment, dict) else environment
        if privileged is None:
            if "permissions" in job:
                problems.append(f"job {name}: has its own `permissions`")
            if publishes:
                problems.append(f"job {name}: uploads to an index, and is not a publishing job")
            if environment is not None:
                problems.append(f"job {name}: runs in an environment")
        else:
            permissions, condition, want_environment, index = privileged
            if job.get("permissions") != permissions:
                problems.append(f"job {name}: `permissions` is not exactly {permissions}")
            if " ".join(str(job.get("if", "")).split()) != condition:
                problems.append(f"job {name}: `if` is not exactly `{condition}`")
            needs = job.get("needs")
            if "wheel" not in ([needs] if isinstance(needs, str) else needs or []):
                problems.append(f"job {name}: does not need the job `wheel`")
            if environment != want_environment:
                problems.append(f"job {name}: environment is {environment!r}, not {want_environment!r}")
            if (len(publishes) == 1) != (want_environment is not None):
                problems.append(f"job {name}: {len(publishes)} steps upload to an index")
            for step in publishes:
                given = step.get("with") or {}
                if given.get("repository-url") != index:
                    problems.append(f"job {name}: the index is {given.get('repository-url')!r}, not {index!r}")
                if "password" in given or "user" in given:
                    problems.append(f"job {name}: the upload is given a user or a password")
                before = job["steps"][job["steps"].index(step) - 1]
                if "tools/ci/wheel_publish.py" not in str(before.get("run", "")):
                    problems.append(f"job {name}: the step before the upload is not wheel_publish.py")
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
                problems.append(f"{where}: `{step['uses']}` is not named by a commit id")
            if "run" in step:
                if "${{" in str(step["run"]):
                    problems.append(f"{where}: `run` holds a ${{{{ }}}} expression; pass it through `env:`")
                scripts = SCRIPT.findall(str(step["run"]))
                if not scripts:
                    problems.append(f"{where}: runs no script under tools/ci")
                for script in scripts:
                    if not os.path.isfile(os.path.join(REPO, script)):
                        problems.append(f"{where}: {script} does not exist")
    return problems


def check_matrix(script: str = "sidecar_matrix.py", workflow: str = "sidecar-probe.yml") -> list:
    done = subprocess.run([sys.executable, "-B", os.path.join(REPO, "tools", "ci", script)],
                          capture_output=True, text=True, env=dict(os.environ, IMTYPESET_CI_LINUX=""))
    if done.returncode != 0 or not done.stdout.startswith("matrix=") or done.stdout.count("\n") != 1:
        return [f"{script} did not print one `matrix=` line: {done.stdout!r} {done.stderr!r}"]
    include = json.loads(done.stdout[len("matrix="):])["include"]
    with open(os.path.join(REPO, ".github", "workflows", workflow), encoding="utf-8") as handle:
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
        if os.path.basename(path) == "wheels.yml":
            problems += check_matrix("wheel_matrix.py", "wheels.yml")
        print(("FAIL " if problems else "ok   ") + os.path.relpath(path, REPO))
        for problem in problems:
            print("     " + problem)
        failed = failed or bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
