#!/bin/sh
# The advtext measurements of docs/advtext-rework-plan.md, as they were run.
#
#   sh python/spike_advtext/measure.sh [out.jsonl]
#
# Needs build/advtext-rework-plan-advtext/libimtypeset_bundle.so:
#   IMTYPESET_BUNDLE_OUT=build/advtext-rework-plan-advtext sh python/host/build_bundle_library.sh
# One JSON line per run, as python/spike_bundle/measure_pptmstr.sh writes them.
R="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:-$R/build/advtext-rework-plan-run/results-advtext.jsonl}"
mkdir -p "$(dirname "$out")"
one() {
    echo "# $(date +%H:%M:%S) load $(cut -d' ' -f1 /proc/loadavg) :: $*" >> "$out"
    ( ulimit -v 6000000
      PYTHONDONTWRITEBYTECODE=1 \
      IMTYPESET_LIBRARY="$R/build/advtext-rework-plan-advtext/libimtypeset_bundle.so" \
      timeout 600 sh "$R/tools/shot.sh" --screen 1600x1000x24 -- \
        "$R/../imgui-adv-text/.venv/bin/python" -P -B "$R/python/spike_advtext/views.py" "$@" ) 2>&1 \
        | grep -v "^shot.sh" | tail -1 >> "$out"
}
for rep in 1 2 3; do
  for pane in today document; do
    one --pane $pane --repeat 1 --scenario steady --frames 240
    one --pane $pane --repeat 10 --scenario steady --frames 240
  done
done
for pane in today document; do
  one --pane $pane --repeat 5 --scenario steady --frames 240
  one --pane $pane --repeat 20 --scenario steady --frames 240
  one --pane $pane --repeat 1 --scenario steady --frames 240 --profile
  one --pane $pane --repeat 10 --scenario steady --frames 240 --profile
  one --pane $pane --repeat 10 --scenario scroll --frames 400
  one --pane $pane --repeat 1 --scenario stream --frames 400
  one --pane $pane --repeat 10 --scenario stream --frames 400
  one --pane $pane --repeat 1 --scenario resize --frames 200
  one --pane $pane --repeat 10 --scenario resize --frames 200
  one --pane $pane --repeat 10 --scenario find --frames 300
  one --pane $pane --repeat 10 --scenario select --frames 220
  one --pane $pane --file math.md --repeat 1 --scenario steady --frames 200
done
one --pane document --repeat 100 --scenario steady --frames 200
echo "# done $(date +%H:%M:%S)" >> "$out"
