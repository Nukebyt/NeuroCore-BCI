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

rm -f "$HW/neurocore.kicad_pcb"
"$KICAD_PY" "$HERE/build_pcb.py" place "$WORK/neurocore.dsn"
# Optimizer off: with it on, 2.1.0 wrote an SES missing most vias (ADR-0006).
java -jar "$JAR" -de "$WORK/neurocore.dsn" -do "$WORK/neurocore.ses" -mp 150 \
    --gui.enabled=false --router.optimizer.enabled=false
"$KICAD_PY" "$HERE/build_pcb.py" finish "$WORK/neurocore.ses"
"$KICAD_CLI" pcb drc --severity-all -o "$WORK/drc.rpt" "$HW/neurocore.kicad_pcb" || true
echo "reports in $WORK"
