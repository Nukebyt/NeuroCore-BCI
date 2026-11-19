"""cocotb: rtl/src/delta_encoder.sv is bit-exact with ml/models/snn/delta.py."""

from __future__ import annotations

import math
import os
import random
import sys
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import FallingEdge, ReadOnly, RisingEdge

HERE = Path(__file__).resolve().parent
RTL = HERE.parents[0] / "src"
sys.path.insert(0, str(HERE.parents[1]))  # repo root, for the Python reference

from ml.models.snn.delta import delta_encode  # noqa: E402

W, THRESH = 16, 64


def stimulus(n: int, seed: int) -> list[int]:
    """EMG-envelope-like bursts plus noise and full-scale steps, clipped to W bits."""
    rng = random.Random(seed)
    lo, hi = -(1 << (W - 1)), (1 << (W - 1)) - 1
    x = []
    for k in range(n):
        burst = 6000 * max(0.0, math.sin(2 * math.pi * k / 400))
        v = int(burst + rng.gauss(0, 40))
        if rng.random() < 0.01:
            v = rng.choice([lo, hi])
        x.append(max(lo, min(hi, v)))
    return x


@cocotb.test()
async def matches_python_reference(dut) -> None:
    Clock(dut.clk, 20, unit="ns").start()
    dut.rst_n.value = 0
    dut.valid_in.value = 0
    dut.x_in.value = 0
    for _ in range(2):
        await RisingEdge(dut.clk)
    await FallingEdge(dut.clk)
    dut.rst_n.value = 1

    rng = random.Random(7)
    x = stimulus(4000, seed=3)
    expect = delta_encode(x, THRESH)
    got = []
    i = 0
    while i < len(x):
        await FallingEdge(dut.clk)
        valid = rng.random() < 0.7  # gaps in valid_in must not change the result
        dut.valid_in.value = int(valid)
        dut.x_in.value = x[i] if valid else rng.randrange(-1000, 1000)
        await RisingEdge(dut.clk)
        await ReadOnly()
        if valid:
            got.append(int(dut.up.value) - int(dut.down.value))
            assert int(dut.valid_out.value) == 1
            i += 1
        else:
            assert int(dut.valid_out.value) == 0
            assert int(dut.up.value) == 0 and int(dut.down.value) == 0
    assert got == expect
    assert expect.count(1) > 50 and expect.count(-1) > 50  # stimulus exercises both


def test_delta_encoder() -> None:
    from cocotb_tools.runner import get_runner

    sim = os.getenv("SIM", "verilator")
    build_dir = HERE / "sim_build" / f"delta_encoder_{sim}"
    runner = get_runner(sim)
    runner.build(
        sources=[RTL / "delta_encoder.sv"],
        hdl_toplevel="delta_encoder",
        parameters={"W": W, "THRESH": THRESH},
        build_dir=build_dir,
        timescale=("1ns", "1ps"),
        always=True,
    )
    runner.test(
        hdl_toplevel="delta_encoder",
        test_module="test_delta_encoder",
        test_dir=HERE,
        build_dir=build_dir,
    )
