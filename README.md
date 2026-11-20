# NeuroDrive

NeuroCore is a battery-powered EEG + EMG acquisition board (ADS1299-4 + ADS131M04 + ESP32-S3 + IMU). It decodes biosignals into a small set of *intents* and sends them over the NeuroCore Intent Protocol (NIP) to device adapters: a toy RC truck first, then a BLE gamepad, smart plugs and a nano drone.

This is a hobby and research project. It is **not a medical device** and makes no medical claims.

- Progress: [docs/STATUS.md](docs/STATUS.md)
- Safety rules (read before touching hardware): [docs/safety.md](docs/safety.md)
- Design decisions: [docs/decisions/](docs/decisions/)

## Getting started (Fedora)

```bash
make venv        # Python 3.12 venv + dev tools + pre-commit hooks
make tools       # pinned verible for SystemVerilog lint
make check       # lint + format check
make test        # unit tests
make help        # all targets
```

Targets for parts that don't exist yet print `SKIP: ...` and succeed (see ADR-0001).

## Licence

Software and firmware: Apache-2.0. Hardware: CERN-OHL-W or CERN-OHL-P (to be decided before the Rev A release). Third-party attributions are in [NOTICE.md](NOTICE.md).
