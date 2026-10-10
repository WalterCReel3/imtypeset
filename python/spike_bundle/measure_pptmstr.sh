#!/bin/sh
# The pptmstr measurements of docs/advtext-rework-plan.md, as they were run.
#
#   sh python/spike_bundle/measure_pptmstr.sh [out.jsonl]
#
# Needs build/advtext-rework-plan-pptmstr/libimtypeset_bundle.so:
#   IMTYPESET_BUNDLE_OUT=build/advtext-rework-plan-pptmstr \
#       sh python/host/build_bundle_library.sh ../imgui_bundle ../pptmstr/.venv/bin/python
# One JSON line per run, each preceded by a comment line with the time, the load
# average and the arguments. Every process is under `ulimit -v 6000000` and a
# timeout. python/spike_bundle/summarise.py reads the file.
R="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:-$R/build/advtext-rework-plan-run/results-pptmstr.jsonl}"
mkdir -p "$(dirname "$out")" "$R/build/advtext-rework-plan-run/cfg"
one() {
    echo "# $(date +%H:%M:%S) load $(cut -d' ' -f1 /proc/loadavg) :: $*" >> "$out"
    ( ulimit -v 6000000
      XDG_CONFIG_HOME="$R/build/advtext-rework-plan-run/cfg" PYTHONDONTWRITEBYTECODE=1 \
      IMTYPESET_LIBRARY="$R/build/advtext-rework-plan-pptmstr/libimtypeset_bundle.so" \
      timeout 600 sh "$R/tools/shot.sh" --screen 1600x1000x24 -- \
        "$R/../pptmstr/.venv/bin/python" -P -B "$R/python/spike_bundle/pptmstr_real_panes.py" "$@" ) 2>&1 \
        | grep -v "^shot.sh" | tail -1 >> "$out"
}
for rep in 1 2 3; do
  for pane in today document; do
    one --pane $pane --pick median --scenario steady --frames 300
    one --pane $pane --pick p90 --scenario steady --frames 300
    one --pane $pane --pick max --scenario steady --frames 300
    one --pane $pane --pick median --repeat 10 --scenario steady --frames 300
    one --pane $pane --mode wrap --pick median --scenario steady --frames 300
    one --pane $pane --mode wrap --pick max --scenario steady --frames 300
    one --pane $pane --mode wrap --pick max --scenario steady --frames 300 --lazy-copy
    one --pane $pane --mode wrap --pick median --scenario steady --frames 300 --lazy-copy
    one --pane $pane --pick max --scenario steady --frames 300 --lazy-copy
    one --pane $pane --pick median --scenario steady --frames 300 --lazy-copy
    one --pane $pane --source repo --scenario steady --frames 300
    one --pane $pane --source repo --reports 3 --scenario steady --frames 300
  done
done
for pane in today document; do
  one --pane $pane --pick median --scenario steady --frames 300 --profile
  one --pane $pane --pick max --scenario steady --frames 300 --profile
  one --pane $pane --source repo --scenario steady --frames 300 --profile
  one --pane $pane --pick max --scenario scroll --frames 500
  one --pane $pane --source repo --scenario scroll --frames 500
  one --pane $pane --pick median --scenario stream --frames 900
  one --pane $pane --pick max --scenario stream --frames 900
  one --pane $pane --source repo --scenario stream --tail 3 --chunk 24 --frames 2500
  one --pane $pane --pick median --scenario resize --frames 300
  one --pane $pane --pick max --scenario resize --frames 300
  one --pane $pane --source repo --scenario resize --frames 300
  one --pane $pane --pick max --scenario find --frames 400
  one --pane $pane --mode wrap --pick max --scenario stream --frames 900
  one --pane $pane --mode wrap --pick max --scenario resize --frames 300
  one --pane $pane --pick max --repeat 10 --scenario steady --frames 200
done
one --pane document --pick max --scenario select --frames 220
one --pane document --pick median --scenario steady --settle --frames 120
one --pane document --pick max --scenario steady --settle --frames 120
one --pane document --pick median --repeat 10 --scenario steady --settle --frames 120
echo "# done $(date +%H:%M:%S)" >> "$out"
