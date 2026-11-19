# data/

Local raw recordings. **Everything in this directory except this README is gitignored and blocked by a pre-commit hook** (CLAUDE.md rule 7). Small, anonymized samples for tests go in `ml/data/samples/` instead.

Layout (ROADMAP §11.4): `data/<subject>/<YYYYMMDD>_<session>/`

| File | Contents |
|---|---|
| `raw.fif` or `raw.csv` | All channels in µV, with timestamps |
| `events.csv` | `onset_s, duration_s, label, source` (labels such as `clench_L`, `ssvep_10Hz`, `rest`) |
| `meta.json` | Hardware rev, firmware hash, montage, sample rates, electrode type, skin prep, impedances, room notes, consent ID |
| `calib.json` | Thresholds from the calibration routine |

`<subject>` is a pseudonymous ID (e.g. `S01`), never a name.
