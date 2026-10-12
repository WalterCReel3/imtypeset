"""Does the field probe notice a wrong offset? Moves each compiled offset in turn,
each in a child interpreter (a wrong offset may crash, which counts as noticed
and is reported as such), and lists the moves the probe lets through.

    python -P -B tools/bundle_route/mutate_gate.py <libimtypeset_bundle.so>

Exit 0 if no mutant survives.
"""

import importlib.util
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location(
    "_bundle", os.path.join(REPO, "python", "imtypeset", "_bundle.py")
)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

# The wheel's module first: a library bound by lookup finds Dear ImGui in it.
library, _preload = gate.open_library(os.path.abspath(sys.argv[1]), bundle_first=True)
facts = gate._facts(library)

if len(sys.argv) == 4:  # the child: one mutant
    name, delta = sys.argv[2], int(sys.argv[3])
    facts[name] = (facts[name][0] + delta, facts[name][1])
    failed = [n for n, passed, _ in gate._probe(library, facts) if not passed]
    print("caught by " + ", ".join(failed) if failed else "SURVIVED")
    sys.exit(0)

assert all(passed for _, passed, _ in gate._probe(library, facts)), "the unmutated probe fails"
names = [name for name in facts if "." in name]
survivors, crashes, total = [], 0, 0
for name in names:
    for delta in (4, 8, -4):
        if facts[name][0] + delta < 0:
            continue
        total += 1
        child = subprocess.run(
            [sys.executable, "-P", "-B", os.path.abspath(__file__), sys.argv[1], name, str(delta)],
            capture_output=True, text=True, timeout=120,
        )
        outcome = child.stdout.strip().splitlines()[-1] if child.returncode == 0 else (
            f"caught by a crash (exit {child.returncode})"
        )
        crashes += child.returncode != 0
        print(f"{name:40s} {delta:+d}: {outcome}", flush=True)
        if outcome == "SURVIVED":
            survivors.append((name, delta))
print(f"{len(names)} offsets, {total} mutants, {crashes} crashed, {len(survivors)} survived: {survivors}")
sys.exit(1 if survivors else 0)
