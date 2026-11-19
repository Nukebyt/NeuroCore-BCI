# NeuroDrive top-level Makefile. Single entry point for humans and CI.
#
# Policy (ADR-0001): a target whose sources don't exist yet prints "SKIP: ..." and
# succeeds; a target whose sources exist but whose tool is missing FAILS.
# Firmware targets only build; flashing is done with idf.py by hand.

SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := help
MAKEFLAGS += --no-print-directory

VENV        ?= .venv
PYTHON312   ?= python3.12
VERIBLE_DIR := $(CURDIR)/.tools/verible

# Prefer tools from the venv and the pinned verible install.
export PATH := $(abspath $(VENV))/bin:$(VERIBLE_DIR)/bin:$(PATH)
PY := $(if $(wildcard $(VENV)/bin/python),$(VENV)/bin/python,python3)

EXCLUDE_DIRS := -path ./$(VENV) -prune -o -path ./.tools -prune -o -path ./.git -prune \
                -o -path '*/build' -prune -o -path '*/managed_components' -prune -o
C_FILES  := $(shell find . $(EXCLUDE_DIRS) -type f \( -name '*.c' -o -name '*.h' \) -print 2>/dev/null)
SV_FILES := $(shell find ./rtl $(EXCLUDE_DIRS) -type f \( -name '*.sv' -o -name '*.svh' \) -print 2>/dev/null)
SBY_FILES   := $(wildcard rtl/formal/*.sby)
SIM_TESTS   := $(wildcard rtl/tb/test_*.py rtl/tb/*/test_*.py)
FW_HOST_TESTS := $(wildcard firmware/*/test/host/CMakeLists.txt protocol/nip/tests/c/CMakeLists.txt)
KICAD_PRO   := $(wildcard hardware/neurocore/*.kicad_pro)

define need_tool
command -v $(1) >/dev/null 2>&1 || { echo "ERROR: '$(1)' not found but $(2) exist. See docs/STATUS.md toolchain notes."; exit 1; }
endef

.PHONY: help venv tools check lint-py lint-c lint-sv format test test-py test-fw-host \
        sim formal bench fw-car fw-neurocore hw-check clean

help: ## List targets
	@grep -hE '^[a-zA-Z0-9_-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  %-14s %s\n", $$1, $$2}'

venv: ## Create .venv (Python 3.12), install dev deps, install pre-commit hooks
	$(PYTHON312) -m venv $(VENV)
	$(VENV)/bin/python -m pip install --upgrade pip
	$(VENV)/bin/python -m pip install -e ".[dev]"
	$(VENV)/bin/pre-commit install

tools: ## Install pinned verible into .tools/ (Linux x86_64)
	scripts/install-verible.sh $(VERIBLE_DIR)

# --------------------------------------------------------------------------- lint
check: lint-py lint-c lint-sv ## Lint + format check (ruff, black, clang-format, verible)

lint-py:
	$(PY) -m ruff check .
	$(PY) -m black --check --diff .

lint-c:
	@if [ -z "$(strip $(C_FILES))" ]; then echo "SKIP clang-format: no C sources yet"; else \
	  $(call need_tool,clang-format,C sources); \
	  clang-format --dry-run --Werror $(C_FILES); echo "clang-format: OK"; fi

lint-sv:
	@if [ -z "$(strip $(SV_FILES))" ]; then echo "SKIP verible: no SystemVerilog sources yet"; else \
	  $(call need_tool,verible-verilog-lint,SystemVerilog sources); \
	  verible-verilog-lint $(SV_FILES); \
	  for f in $(SV_FILES); do verible-verilog-format --verify $$f; done; echo "verible: OK"; fi

format: ## Apply formatters in place
	$(PY) -m ruff check --fix .
	$(PY) -m black .
	@if [ -n "$(strip $(C_FILES))" ]; then clang-format -i $(C_FILES); fi
	@for f in $(SV_FILES); do verible-verilog-format --inplace $$f; done

# --------------------------------------------------------------------------- tests
test: test-py test-fw-host ## pytest (host, ml, protocol) + firmware host tests

test-py:
	$(PY) -m pytest

test-fw-host:
	@if [ -z "$(strip $(FW_HOST_TESTS))" ]; then echo "SKIP firmware host tests: none yet"; else \
	  $(call need_tool,cmake,firmware host tests); \
	  for f in $(FW_HOST_TESTS); do d=$$(dirname $$f); \
	    cmake -S $$d -B $$d/build -DCMAKE_BUILD_TYPE=Debug && cmake --build $$d/build && \
	    ctest --test-dir $$d/build --output-on-failure; done; fi

SIM ?= verilator
export SIM
# CI uses the YoWASP build: SBY="yowasp-sby --yosys yowasp-yosys --smtbmc yowasp-yosys-smtbmc ..."
SBY ?= sby
SIM_BIN := $(if $(filter icarus,$(SIM)),iverilog,$(SIM))

sim: ## cocotb RTL tests (SIM=verilator default, SIM=icarus also works)
	@if [ -z "$(strip $(SIM_TESTS))" ]; then echo "SKIP sim: no cocotb tests yet (Phase 7)"; else \
	  $(call need_tool,$(SIM_BIN),cocotb tests); \
	  $(PY) -m pytest rtl/tb; fi

formal: ## SymbiYosys proofs (rtl/formal/*.sby)
	@if [ -z "$(strip $(SBY_FILES))" ]; then echo "SKIP formal: no .sby files yet (Phase 7)"; else \
	  $(call need_tool,$(firstword $(SBY)),.sby files); \
	  for f in $(SBY_FILES); do (cd rtl/formal && $(SBY) -f $$(basename $$f)) || exit 1; done; fi

bench: ## ML benchmark -> docs/reports/bench.md
	@if [ ! -f ml/eval/bench.py ]; then echo "SKIP bench: ml/eval/bench.py not written yet (Phase 6)"; else \
	  $(PY) -m ml.eval.bench --out docs/reports/bench.md; fi

# --------------------------------------------------------------------------- firmware (build only)
fw-car: ## idf.py build for firmware/car (flashing is human-only)
	@if [ ! -f firmware/car/CMakeLists.txt ]; then echo "SKIP fw-car: no project yet (Phase 1)"; else \
	  $(call need_tool,idf.py,firmware/car); idf.py -C firmware/car build; fi

fw-neurocore: ## idf.py build for firmware/neurocore (flashing is human-only)
	@if [ ! -f firmware/neurocore/CMakeLists.txt ]; then echo "SKIP fw-neurocore: no project yet (Phase 4)"; else \
	  $(call need_tool,idf.py,firmware/neurocore); idf.py -C firmware/neurocore build; fi

# --------------------------------------------------------------------------- hardware
hw-check: ## kicad-cli ERC/DRC + BOM + gerbers into hardware/neurocore/fab/
	@if [ -z "$(KICAD_PRO)" ]; then echo "SKIP hw-check: no KiCad project yet (Phase 3)"; else \
	  $(call need_tool,kicad-cli,KiCad project); \
	  base=$(basename $(KICAD_PRO)); mkdir -p hardware/neurocore/fab; \
	  kicad-cli sch erc --severity-error --exit-code-violations -o hardware/neurocore/fab/erc.rpt $$base.kicad_sch; \
	  kicad-cli pcb drc --severity-error --schematic-parity --exit-code-violations -o hardware/neurocore/fab/drc.rpt $$base.kicad_pcb; \
	  kicad-cli sch export bom -o hardware/neurocore/fab/bom-raw.csv $$base.kicad_sch; \
	  kicad-cli pcb export gerbers -o hardware/neurocore/fab/ $$base.kicad_pcb; \
	  kicad-cli pcb export drill -o hardware/neurocore/fab/ $$base.kicad_pcb; fi

clean: ## Remove caches and build outputs (never touches data/)
	rm -rf .pytest_cache .ruff_cache .hypothesis
	find . $(EXCLUDE_DIRS) -type d -name __pycache__ -print0 | xargs -0 rm -rf
