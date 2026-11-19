"""Check that KiCad reads the generated schematic exactly as design.py intended.

Exports the schematic netlist with kicad-cli and compares every (ref, pin) -> net with
design.json. Catches labels that don't land on pins, wrong pin numbers, missing parts.

    python hardware/neurocore/gen/check_netlist.py [path/to/kicad-cli]
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HW = Path(__file__).resolve().parents[1]
DEFAULT_CLI = r"C:\Program Files\KiCad\9.0\bin\kicad-cli.exe"


def kicad_netlist(cli: str) -> dict[tuple[str, str], str]:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "nl.net"
        subprocess.run(
            [
                cli,
                "sch",
                "export",
                "netlist",
                "--format",
                "kicadsexpr",
                "-o",
                str(out),
                str(HW / "neurocore.kicad_sch"),
            ],
            check=True,
            capture_output=True,
        )
        text = out.read_text(encoding="utf-8")
    result = {}
    for m in re.finditer(
        r'\(net \(code "?\d+"?\) \(name "([^"]*)"\)(.*?)\)\s*(?=\(net |\)\s*\)\s*$)', text, re.S
    ):
        name, body = m.group(1), m.group(2)
        for ref, pin in re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', body):
            result[(ref, pin)] = name.lstrip("/")
    return result


def main() -> int:
    cli = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_CLI
    design = json.loads((HW / "design.json").read_text(encoding="utf-8"))
    got = kicad_netlist(cli)
    errors = []
    expected = 0
    for part in design["parts"]:
        for pin, net in part["pins"].items():
            k = (part["ref"], pin)
            if net is None:
                if k in got and not got[k].startswith("unconnected-"):
                    errors.append(f"{k}: expected no-connect, KiCad has {got[k]}")
                continue
            expected += 1
            if got.get(k) != net:
                errors.append(f"{k}: expected {net}, KiCad has {got.get(k)}")
    for e in errors[:40]:
        print("MISMATCH", e)
    print(f"checked {expected} connected pins: {len(errors)} mismatches")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
