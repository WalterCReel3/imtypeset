"""Writes the attestation for a sidecar library: the record that the full field
probe, run now in a child interpreter, accepted this library beside the
imgui_bundle wheel of the interpreter running this script.

    python -P -B tools/bundle_route/attest.py <libimtypeset_bundle.so> [out.json [more.json]]

Default output: <library>.attestation.json, where `import imtypeset` looks for
it when IMTYPESET_LIBRARY names the library. python/imtypeset/_bundle.py says
what an attestation is and what importing the package does with one.
`more.json` holds an object whose members are added to it (the wheel builder
records the distribution's version and tag that way). Exit 0
if accepted and written, 1 with the refusal otherwise.
"""

import importlib.util
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location(
    "_bundle", os.path.join(REPO, "python", "imtypeset", "_bundle.py")
)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

if len(sys.argv) not in (2, 3, 4):
    sys.exit(__doc__)
more = {"attested_where": "a development build"}
if len(sys.argv) == 4:
    import json

    with open(sys.argv[3], encoding="utf-8") as handle:
        more.update(json.load(handle))
try:
    written = gate.attest(sys.argv[1], sys.argv[2] if len(sys.argv) >= 3 else "", more)
except ImportError as refused:
    print(f"attest.py: not attested\n{refused}", file=sys.stderr)
    sys.exit(1)
print(
    f"attested: {written['library']['file']} sha256 {written['library']['sha256'][:16]}... beside "
    f"imgui_bundle {written['wheel_version']} ({written['module']['file']} sha256 "
    f"{written['module']['sha256'][:16]}...), headers {written['headers']}, accepted by "
    f"{written['accepted_by']}, {len(written['probes'])} probe rows"
)
