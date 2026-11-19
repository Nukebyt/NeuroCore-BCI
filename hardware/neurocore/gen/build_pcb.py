"""Build neurocore.kicad_pcb from design.json (run with KiCad's Python).

    "C:/Program Files/KiCad/9.0/bin/python.exe" hardware/neurocore/gen/build_pcb.py place
    ... route with Freerouting (see route.sh) ...
    "C:/Program Files/KiCad/9.0/bin/python.exe" hardware/neurocore/gen/build_pcb.py finish <ses>

Stage `place` creates the board (4 layers, outline, footprints linked to schematic symbols,
placement, GND plane) and exports Specctra DSN. Stage `finish` imports the routed SES,
adds GND pours and fills zones. Status: UNVERIFIED DRAFT (ADR-0006).
"""

from __future__ import annotations

import json
import math
import os
import sys
from pathlib import Path

try:  # KiCad's debug asserts open modal dialogs, which hang a headless script.
    import wx

    wx.DisableAsserts()
except Exception:
    pass

import pcbnew

HW = Path(__file__).resolve().parents[1]
FP_ROOT = Path(
    os.environ.get("KICAD9_FOOTPRINT_DIR", r"C:\Program Files\KiCad\9.0\share\kicad\footprints")
)
BOARD_W, BOARD_H = 86.0, 55.0
OX, OY = 100.0, 100.0  # board origin on the KiCad canvas (mm)
MARGIN = 1.0

# Fixed placements: ref -> (x, y, rotation deg), board-relative mm.
FIXED = {
    "H1": (3.5, 3.5, 0),
    "H2": (47.5, 3.5, 0),
    "H3": (3.5, 51.5, 0),
    "H4": (82.5, 51.5, 0),
    # analog side (left)
    "J1": (3.5, 10.0, 0),
    "J2": (3.5, 33.0, 0),
    "U10": (9.5, 15.0, 90),
    "U11": (9.5, 25.5, 90),
    "U12": (9.5, 36.5, 90),
    "U1": (25.0, 17.0, 0),
    "U5": (20.0, 36.0, 0),
    "U6": (20.0, 45.0, 0),
    "U2": (32.0, 41.0, 0),
    # analog supplies; the LM27761 charge pump (a switcher) kept >15 mm from U1
    "U13": (37.0, 5.0, 0),
    "U7": (47.0, 9.0, 0),
    "U14": (40.0, 31.0, 0),
    # digital side (right): ESP32 antenna at the top edge
    "U3": (75.5, 15.5, 0),
    "U4": (54.0, 24.0, 0),
    "J3": (73.5, 31.5, 0),
    "J4": (81.5, 38.5, 90),
    "J7": (62.5, 14.0, 0),
    "SW2": (58.0, 31.0, 0),
    "SW3": (58.0, 36.0, 0),
    "SW4": (50.0, 44.0, 0),
    # power (bottom)
    "U9": (60.0, 41.0, 0),
    "U8": (68.0, 46.0, 0),
    "U15": (55.0, 44.5, 0),
    "J6": (58.0, 52.0, 0),
    "J5": (44.0, 50.5, 0),
    "SW1": (36.0, 51.5, 0),
}
# Passive anchor hints (substring of the part note -> anchor ref).
NOTE_ANCHOR = {
    "BMI270": "U4",
    "DVDD pin": "U1",
    "ADS131M04 DVDD": "U2",
    "module supply": "U3",
    "EN RC": "U3",
    "TLV757": "U9",
    "TPS7A2025": "U13",
    "TPS7A2033": "U14",
    "LM27761": "U7",
    "BQ21040": "U8",
    "OPA2333": "U5",
    "Rd": "J6",
    "bias": "U1",
}
# ESP32 antenna area: the rule area carried by the KiCad ESP32-S3-WROOM-1 footprint, measured
# on the placed board (x 51.5..99.5, y -12.25..8.75 board mm), clipped to the outline.
KEEPOUTS = [(51.5, 0.0, 86.0, 8.75)]
SKIP_NETS = {"GND"}


def mm(v: float) -> int:
    return pcbnew.FromMM(v)


def load_fp(lib_name: str) -> pcbnew.FOOTPRINT:
    lib, name = lib_name.split(":")
    fp = pcbnew.FootprintLoad(str(FP_ROOT / f"{lib}.pretty"), name)
    if fp is None:
        raise RuntimeError(f"footprint not found: {lib_name}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))  # keep the library nickname for schematic parity
    return fp


def bbox_mm(fp: pcbnew.FOOTPRINT, pad: float = 0.4) -> tuple[float, float, float, float]:
    b = fp.GetBoundingBox(False, False)
    return (
        pcbnew.ToMM(b.GetX()) - OX - pad,
        pcbnew.ToMM(b.GetY()) - OY - pad,
        pcbnew.ToMM(b.GetRight()) - OX + pad,
        pcbnew.ToMM(b.GetBottom()) - OY + pad,
    )


def overlaps(a, b) -> bool:
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])


def inside_board(bb) -> bool:
    return (
        bb[0] >= MARGIN
        and bb[1] >= MARGIN
        and bb[2] <= BOARD_W - MARGIN
        and bb[3] <= BOARD_H - MARGIN
    )


def setup_board() -> pcbnew.BOARD:
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(4)
    ds = board.GetDesignSettings()
    try:
        nc = ds.m_NetSettings.GetDefaultNetclass()
        nc.SetTrackWidth(mm(0.2))
        nc.SetClearance(mm(0.15))
        nc.SetViaDiameter(mm(0.6))
        nc.SetViaDrill(mm(0.3))
        # The library ESP32-S3-WROOM-1 footprint has 0.2 mm thermal vias in its GND pad.
        ds.m_MinThroughDrill = mm(0.2)
    except Exception as e:  # API differences between KiCad versions
        print("netclass setup skipped:", e)
    rect = pcbnew.PCB_SHAPE(board)
    rect.SetShape(pcbnew.SHAPE_T_RECT)
    rect.SetStart(pcbnew.VECTOR2I(mm(OX), mm(OY)))
    rect.SetEnd(pcbnew.VECTOR2I(mm(OX + BOARD_W), mm(OY + BOARD_H)))
    rect.SetLayer(pcbnew.Edge_Cuts)
    rect.SetWidth(mm(0.1))
    board.Add(rect)
    add_edge_keepouts(board)
    return board


EDGE_KEEPOUT = 0.6  # mm; > 0.5 mm board edge clearance. Freerouting only sees rule areas.


def add_edge_keepouts(board) -> None:
    """No tracks/vias within EDGE_KEEPOUT of the outline (pads and footprints allowed)."""
    k, w, h = EDGE_KEEPOUT, BOARD_W, BOARD_H
    for x0, y0, x1, y1 in ((0, 0, w, k), (0, h - k, w, h), (0, 0, k, h), (w - k, 0, w, h)):
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowCopperPour(False)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        z.SetLayerSet(pcbnew.LSET.AllCuMask(4))
        chain = pcbnew.SHAPE_LINE_CHAIN()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            chain.Append(mm(OX + x), mm(OY + y))
        chain.SetClosed(True)
        z.Outline().AddOutline(chain)
        board.Add(z)


def add_zone(board, net, layer, pad=0.3, priority=0) -> None:
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetAssignedPriority(priority)
    z.SetLocalClearance(mm(0.25))
    z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)  # reflow assembly; avoids starved thermals
    o = z.Outline()
    o.NewOutline()
    chain = pcbnew.SHAPE_LINE_CHAIN()
    for x, y in (
        (pad, pad),
        (BOARD_W - pad, pad),
        (BOARD_W - pad, BOARD_H - pad),
        (pad, BOARD_H - pad),
    ):
        chain.Append(mm(OX + x), mm(OY + y))
    chain.SetClosed(True)
    o.RemoveAllContours()
    o.AddOutline(chain)
    board.Add(z)


def strap_adjacent_pins(board, max_pitch_mm: float = 0.55, width_mm: float = 0.2) -> int:
    """Join neighbouring same-net SMD pins of one footprint with a straight F.Cu track.

    Freerouting occasionally leaves such pairs (e.g. ADS1299 AVDD pins) unjoined; prerouting
    them makes the result deterministic. Only equal-size pads on one axis are strapped.
    """
    n = 0
    for fp in board.GetFootprints():
        pads = [
            p for p in fp.Pads() if p.GetNetname() and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD
        ]
        for i, a in enumerate(pads):
            for b in pads[i + 1 :]:
                if a.GetNetCode() != b.GetNetCode() or a.GetSize() != b.GetSize():
                    continue
                pa, pb = a.GetPosition(), b.GetPosition()
                if pa.x != pb.x and pa.y != pb.y:
                    continue
                if pcbnew.ToMM(abs(pa.x - pb.x) + abs(pa.y - pb.y)) > max_pitch_mm:
                    continue
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pa)
                t.SetEnd(pb)
                t.SetWidth(mm(width_mm))
                t.SetLayer(pcbnew.F_Cu)
                t.SetNet(a.GetNet())
                board.Add(t)
                n += 1
    print(f"strapped {n} adjacent same-net pin pairs")
    return n


# Owner's name on the back silkscreen, inside the antenna keepout (no copper there on any
# layer, so it can't land on pads or vias). Board mm, centre of the text.
BACK_TEXT = ("Priyanuj's NeuroCore", 68.75, 4.4, 2.0)  # text, x, y, height


def add_back_text(board) -> None:
    text, x, y, h = BACK_TEXT
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetLayer(pcbnew.B_SilkS)
    t.SetMirrored(True)  # reads correctly when the board is flipped
    t.SetTextSize(pcbnew.VECTOR2I(mm(h), mm(h)))
    t.SetTextThickness(mm(h * 0.15))
    t.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y)))
    board.Add(t)


def place(design: dict, nc_names: dict[tuple[str, str], str]) -> pcbnew.BOARD:
    """nc_names: (ref, pin) -> KiCad's "unconnected-(...)" net name for no-connect pins."""
    board = setup_board()
    nets = {}
    for part in design["parts"]:
        for n in part["pins"].values():
            if n and n not in nets:
                # Root-sheet local labels: KiCad names these nets "/<label>".
                ni = pcbnew.NETINFO_ITEM(board, "/" + n)
                board.Add(ni)
                nets[n] = ni

    fps = {}
    for part in design["parts"]:
        fp = load_fp(part["footprint"])
        fp.SetReference(part["ref"])
        fp.SetValue(part["value"])
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        fp.Reference().SetTextThickness(mm(0.12))
        if part["ref"].startswith("H"):  # mounting holes: label would hang off the edge
            fp.Reference().SetVisible(False)
        fp.SetPath(pcbnew.KIID_PATH("/" + part["symbol_uuid"]))
        for pad in fp.Pads():
            num = pad.GetNumber()
            n = part["pins"].get(num)
            if n:
                pad.SetNet(nets[n])
            elif (part["ref"], num) in nc_names:  # match the schematic for parity checks
                nc = nc_names[(part["ref"], num)]
                if nc not in nets:
                    nets[nc] = pcbnew.NETINFO_ITEM(board, nc)
                    board.Add(nets[nc])
                pad.SetNet(nets[nc])
        board.Add(fp)
        fps[part["ref"]] = (fp, part)

    placed = []
    for ref, (x, y, rot) in FIXED.items():
        fp = fps[ref][0]
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y)))
        placed.append(bbox_mm(fp, 0.3))

    # Count pins per net for anchor scoring.
    def anchor_for(part) -> str:
        for key, ref in NOTE_ANCHOR.items():
            if key in part["note"]:
                return ref
        mine = {n for n in part["pins"].values() if n and n not in SKIP_NETS}
        best, score = "U3", -1
        for ref in FIXED:
            other = fps[ref][1]["pins"].values()
            s = sum(1 for n in other if n in mine)
            if s > score:
                best, score = ref, s
        return best

    for ref, (fp, part) in fps.items():
        if ref in FIXED:
            continue
        anchor = fps[anchor_for(part)][0]
        ax, ay = (pcbnew.ToMM(v) for v in (anchor.GetPosition().x, anchor.GetPosition().y))
        ax, ay = ax - OX, ay - OY
        done = False
        for ring in range(1, 60):
            r = 1.2 * ring
            steps = max(8, int(2 * math.pi * r / 1.6))
            for k in range(steps):
                ang = 2 * math.pi * k / steps
                x, y = ax + r * math.cos(ang), ay + r * math.sin(ang)
                for rot in (0, 90):
                    fp.SetOrientationDegrees(rot)
                    fp.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y)))
                    bb = bbox_mm(fp, 0.25)
                    if not inside_board(bb) or any(overlaps(bb, k2) for k2 in KEEPOUTS):
                        continue
                    if any(overlaps(bb, p) for p in placed):
                        continue
                    placed.append(bb)
                    done = True
                    break
                if done:
                    break
            if done:
                break
        if not done:
            raise RuntimeError(f"could not place {ref}")

    strap_adjacent_pins(board)
    add_back_text(board)
    add_zone(board, nets["GND"], pcbnew.In1_Cu)
    return board


def finish(board_path: Path, ses: Path) -> None:
    board = pcbnew.LoadBoard(str(board_path))
    if not pcbnew.ImportSpecctraSES(board, str(ses)):
        raise RuntimeError("SES import failed")
    gnd = board.FindNet("/GND")
    for layer in (pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
        add_zone(board, gnd, layer)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(str(board_path))


def main() -> None:
    stage = sys.argv[1]
    pcb = HW / "neurocore.kicad_pcb"
    if stage == "place":
        design = json.loads((HW / "design.json").read_text(encoding="utf-8"))
        from check_netlist import DEFAULT_CLI, kicad_netlist  # same dir; stdlib only

        sch = kicad_netlist(os.environ.get("KICAD_CLI", DEFAULT_CLI))
        nc_names = {k: v for k, v in sch.items() if v.startswith("unconnected-")}
        board = place(design, nc_names)
        # No zone fill here: ZONE_FILLER crashes on an in-memory board without a project.
        board.Save(str(pcb))
        dsn = Path(sys.argv[2]) if len(sys.argv) > 2 else HW / "neurocore.dsn"
        if not pcbnew.ExportSpecctraDSN(board, str(dsn)):
            raise RuntimeError("DSN export failed")
        print(f"placed {len(design['parts'])} parts; DSN -> {dsn}")
    elif stage == "finish":
        finish(pcb, Path(sys.argv[2]))
        print("routed board saved")
    else:
        raise SystemExit("stage must be place or finish")


if __name__ == "__main__":
    main()
