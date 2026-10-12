"""Prints the bundle import gate's whole report for a library.

    python -P -B tools/bundle_route/run_gate.py build/py-host-bundle/libimtypeset_bundle.so

Loads python/imtypeset/_bundle.py by path, so the package's own import gate is
not in the way of seeing why it refuses. Exit 0 if accepted, 1 if not.
"""

import importlib.util
import json
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location(
    "_bundle", os.path.join(REPO, "python", "imtypeset", "_bundle.py")
)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

# The wheel's module first: a library bound by lookup finds Dear ImGui in it.
library, _preload = gate.open_library(os.path.abspath(sys.argv[1]), bundle_first=True)
print("preload:", _preload, "(what had to be in the process before the library: _bundle.open_library)")
start = time.perf_counter()
report = gate.check(library)
seconds = time.perf_counter() - start
for name, passed, detail in report["probes"]:
    print("PASS" if passed else "FAIL", name, detail)
print(json.dumps({k: v for k, v in report.items() if k != "probes"}, indent=1))
verdict = gate.accepted(report)
print("accepted:", verdict, f"({seconds * 1000:.1f} ms)")
sys.exit(0 if verdict[0] else 1)
