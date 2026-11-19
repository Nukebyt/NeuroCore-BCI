#!/usr/bin/env bash
# Full board build: schematic -> checks -> placement -> Freerouting -> pours -> DRC.
# Usage: hardware/neurocore/gen/route.sh <freerouting.jar> [work dir]
# Needs: python (repo venv), KiCad 9 (python + kicad-cli), Java 21+ (Freerouting 2.1.0 on Java 21).
set -euo pipefail

JAR="${1:?path to freerouting jar (2.1.0 runs on Java 21)}"
WORK="${2:-$(mktemp -d)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
HW="$(dirname "$HERE")"
KICAD_PY="${KICAD_PY:-/c/Program Files/KiCad/9.0/bin/python.exe}"
KICAD_CLI="${KICAD_CLI:-/c/Program Files/KiCad/9.0/bin/kicad-cli.exe}"
PY="${PY:-python}"

"$PY" "$HERE/design.py"
"$KICAD_CLI" sch erc --severity-all --exit-code-violations -o "$WORK/erc.rpt" "$HW/neurocore.kicad_sch"
"$PY" "$HERE/check_netlist.py" "$KICAD_CLI"

# Freerouting 2.1.0 (optimizer off; with it on, the SES lost most vias, ADR-0006) sometimes
# writes an SES that is missing vias or wires even though it reports a complete route. The
# only trusted check is KiCad DRC on the imported board, so the whole place -> route ->
# import cycle is retried until DRC finds no unconnected items. Placement is deterministic.
ok=0
for attempt in 1 2 3 4 5; do
  rm -f "$HW/neurocore.kicad_pcb"
  "$KICAD_PY" "$HERE/build_pcb.py" place "$WORK/neurocore.dsn"
  java -jar "$JAR" -de "$WORK/neurocore.dsn" -do "$WORK/neurocore.ses" -mp 150 \
      --gui.enabled=false --router.optimizer.enabled=false > "$WORK/fr.log" 2>&1
  "$KICAD_PY" "$HERE/build_pcb.py" finish "$WORK/neurocore.ses"
  "$KICAD_CLI" pcb drc --severity-all --schematic-parity -o "$WORK/drc.rpt" \
      "$HW/neurocore.kicad_pcb" > /dev/null || true
  open_items=$(grep -oE 'Found [0-9]+ unconnected' "$WORK/drc.rpt" | grep -oE '[0-9]+')
  echo "attempt $attempt: ${open_items:-?} unconnected after import"
  if [ "${open_items:-1}" = 0 ]; then ok=1; break; fi
done
[ "$ok" = 1 ] || { echo "no complete route after 5 attempts" >&2; exit 1; }
"$KICAD_CLI" pcb drc --severity-error --schematic-parity --exit-code-violations \
    -o "$WORK/drc-errors.rpt" "$HW/neurocore.kicad_pcb"
echo "reports in $WORK"
