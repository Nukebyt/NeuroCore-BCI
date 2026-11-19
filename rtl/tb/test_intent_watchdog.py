"""cocotb tests for rtl/src/intent_watchdog.sv (docs/safety.md S1 on the FPGA path).

Run: make sim            (SIM=verilator by default; SIM=icarus also works)
"""

from __future__ import annotations

import os
import random
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import FallingEdge, ReadOnly, RisingEdge

RTL = Path(__file__).resolve().parents[1] / "src"
TIMEOUT = 20  # small for simulation; the proof uses 8, hardware uses 300 ms of clocks
STOP, FWD, LEFT = 0, 2, 4


async def setup(dut) -> None:
    Clock(dut.clk, 20, unit="ns").start()
    dut.rst_n.value = 0
    dut.valid_in.value = 0
    dut.intent_in.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    await FallingEdge(dut.clk)
    dut.rst_n.value = 1


async def step(dut, valid: bool, intent: int = 0) -> tuple[int, int]:
    """Drive inputs on the falling edge; return (intent_out, timed_out) after the rising edge."""
    await FallingEdge(dut.clk)
    dut.valid_in.value = int(valid)
    dut.intent_in.value = intent
    await RisingEdge(dut.clk)
    await ReadOnly()
    return int(dut.intent_out.value), int(dut.timed_out.value)


@cocotb.test()
async def reset_outputs_stop(dut) -> None:
    await setup(dut)
    assert await step(dut, False) == (STOP, 1)


@cocotb.test()
async def passes_intent_then_times_out_exactly(dut) -> None:
    await setup(dut)
    assert await step(dut, True, FWD) == (FWD, 0)
    for k in range(1, TIMEOUT):  # TIMEOUT-1 silent cycles: still FWD
        assert await step(dut, False) == (FWD, 0), k
    assert await step(dut, False) == (STOP, 1)  # TIMEOUT silent cycles: STOP
    assert await step(dut, True, LEFT) == (LEFT, 0)  # recovers on the next valid


@cocotb.test()
async def random_traffic_matches_model(dut) -> None:
    await setup(dut)
    rng = random.Random(1)
    held, idle = STOP, TIMEOUT
    for _ in range(3000):
        valid = rng.random() < (0.02 if rng.random() < 0.5 else 0.3)
        intent = rng.randrange(9)
        got = await step(dut, valid, intent)
        if valid:
            held, idle = intent, 0
        else:
            idle = min(TIMEOUT, idle + 1)
        assert got == ((STOP, 1) if idle >= TIMEOUT else (held, 0))


def test_intent_watchdog() -> None:
    from cocotb_tools.runner import get_runner

    sim = os.getenv("SIM", "verilator")
    here = Path(__file__).resolve().parent
    build_dir = here / "sim_build" / f"intent_watchdog_{sim}"
    runner = get_runner(sim)
    runner.build(
        sources=[RTL / "intent_watchdog.sv"],
        hdl_toplevel="intent_watchdog",
        parameters={"TIMEOUT_CYCLES": TIMEOUT},
        build_dir=build_dir,
        timescale=("1ns", "1ps"),
        always=True,
    )
    runner.test(
        hdl_toplevel="intent_watchdog",
        test_module="test_intent_watchdog",
        test_dir=here,
        build_dir=build_dir,
    )
