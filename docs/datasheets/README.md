# Datasheets

The only allowed source for register addresses, bit fields, timing, pinouts, footprints and power limits (CLAUDE.md rule 2). If a needed datasheet is not here, Claude Code stops and asks for it.

- PDFs are stored with git-lfs (`.gitattributes`). Run `git lfs install` once per clone.
- Name PDFs `<part>-<doc-id>-<revision>.pdf`, e.g. `ads1299-sbas499-revC.pdf`.
- For each part used, add `<part>.md` summarizing the exact sections relied on (section number, what it says, where it is used in code or schematic).
- Code comments cite the section: `// ADS1299 DS §9.6.1.3 CONFIG1`.

## Needed by phase

| Phase | Parts |
|---|---|
| 1 | ESP32-C3 module, TB6612FNG or DRV8833, the truck's regulator choice |
| 3–4 | ADS1299-4, ADS131M04, ESP32-S3-WROOM-1, BMI270 or LSM6DSO, OPA2333 or OPA2376, charger IC, LDOs, ESD diodes |
| 5 | VL53L0X or VL53L1X |
