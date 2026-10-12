#!/bin/sh
# Puts the probe's report where a person sees it: the job's summary page on
# GitHub ($GITHUB_STEP_SUMMARY), or standard output anywhere else.
#
#   tools/ci/sidecar_summary.sh <report-directory>
#
# Always exits 0. A probe that was killed leaves report-partial.md, which is
# shown under a line saying that is what it is; one that never started leaves
# only pip's log.
set -u
out="$1"
target="${GITHUB_STEP_SUMMARY:-/dev/stdout}"
{
    if [ -f "$out/report.md" ]; then
        cat "$out/report.md"
    elif [ -f "$out/report-partial.md" ]; then
        echo "# Sidecar probe: DID NOT FINISH"
        echo
        echo "The probe was stopped before it wrote its verdict. What it had recorded:"
        echo
        cat "$out/report-partial.md"
    else
        echo "# Sidecar probe: DID NOT START"
        echo
        echo "tools/ci/sidecar_probe.py wrote no report. The job's log has why."
    fi
    if [ -f "$out/pip-install.log" ]; then
        echo
        echo "## pip"
        echo
        echo '```'
        tail -n 25 "$out/pip-install.log"
        echo '```'
    fi
} >> "$target"
exit 0
