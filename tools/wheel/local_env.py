"""A throwaway virtual environment that SEES another environment's packages
without anything being installed into, or written in, that environment: the
offline stand-in for `pip install imgui-bundle==X` on a machine with no index.

    <python of the source environment> -P -B tools/wheel/local_env.py <new directory>

Makes a virtual environment on the same base interpreter (`venv --without-pip`)
and fills its site-packages with SYMBOLIC LINKS to the entries of the source
environment's site-packages: imgui_bundle and what it imports, their
`.dist-info` directories (so that pip counts them as installed and holds a new
wheel's `Requires-Dist` to them), and pip itself. Left out: `.pth` files,
editable installs, and anything named imtypeset.

What is then installed with the new environment's pip goes into the new
directory as real files, beside the links: the layout a real install has.
Prints the new environment's interpreter. CI does not use this: it installs
imgui-bundle from PyPI.
"""

import os
import subprocess
import sys
import sysconfig


def main(argv) -> int:
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    target = os.path.abspath(argv[0])
    source = sysconfig.get_paths()["purelib"]
    if os.path.exists(target):
        print(f"local_env.py: {target} exists; give a new directory", file=sys.stderr)
        return 2
    subprocess.run([sys.executable, "-m", "venv", "--without-pip", target], check=True)
    python = os.path.join(target, "bin", "python")
    site = subprocess.run(
        [python, "-I", "-c", "import sysconfig; print(sysconfig.get_paths()['purelib'])"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    linked = 0
    for name in sorted(os.listdir(source)):
        path = os.path.join(source, name)
        if name.endswith(".pth") or name == "__pycache__" or name.lower().startswith("imtypeset"):
            continue
        if name.startswith("_editable_impl") or name.startswith("__editable__"):
            continue
        if name.endswith(".dist-info") and os.path.exists(os.path.join(path, "direct_url.json")):
            continue  # an editable or local install: not something an index would supply
        os.symlink(path, os.path.join(site, name))
        linked += 1
    print(f"local_env.py: {linked} links into {site}", file=sys.stderr)
    print(python)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
