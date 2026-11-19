# ADR-0001: Build entry point, pinned tooling and the SKIP policy

- Status: proposed
- Date: 2026-11-19
- Phase: 0
- Safety-relevant: no

## Context
Phase 0 must make `make check` and the (empty) test suites green in CI before most code exists. The repo mixes Python, C (ESP-IDF), SystemVerilog and KiCad, and the owner develops on Fedora while CI runs on Ubuntu. Lint results must not differ between the two because of tool versions.

## Options
1. **Make + pinned tools from PyPI/GitHub releases**: one entry point; ruff, black and clang-format come from PyPI wheels at fixed versions; verible comes from a fixed GitHub release checked by SHA-256.
2. **Distro packages** (dnf/apt): simpler to install, but Fedora and Ubuntu ship different clang-format versions, which format differently.
3. **pre-commit as the only runner**: good for local hooks, but awkward for test, sim and firmware build targets.

## Decision
Option 1, with pre-commit as an extra local layer at the same versions.

- `Makefile` is the single entry point for humans and CI.
- Tool versions: `pyproject.toml [dev]`, `.pre-commit-config.yaml` and `scripts/install-verible.sh` (verible `v0.0-4298-g085eef05`, SHA-256 checked) are kept in step.
- Python is pinned to 3.12 (`requires-python = ">=3.12,<3.13"`), matching the venv in CLAUDE.md.
- **SKIP policy:** a target whose sources don't exist yet prints `SKIP: <reason>` and exits 0. A target whose sources exist but whose tool is missing **fails**. This lets CI be green in Phase 0 without hiding a missing tool later.
- Make targets never flash hardware. Flashing stays human-only.
- C style: `.clang-format` based on LLVM with 4-space indent and 100 columns; include sorting off because some ESP-IDF headers are order-sensitive.
- Python style: ruff (E, F, W, I, B, UP, SIM, RUF) + black, 100 columns.

## Consequences
- Upgrading a tool means changing two or three pins together.
- CI does not yet run `sim`, `formal`, `fw-*` or `hw-check` with real tools. When the first RTL, firmware or KiCad sources land (Phases 1, 3, 7), CI must install Verilator/SymbiYosys, ESP-IDF or KiCad in the same change, or the SKIP→FAIL rule will turn CI red, which is the intended reminder.
- verible is only installed for Linux x86_64; other hosts need a matching manual install.
