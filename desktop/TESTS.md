# Test suite

Two levels protect the calculation logic. The first runs automatically; the
second needs real saves and is therefore run by hand before every merge into
`main`.

## 1 · Unit tests (automatic in CI)

`ctest` runs every Qt Test suite under `src/tests/`. CI does this on every push
on Windows and Linux (`.github/workflows/ci.yml`, `linux.yml`), and the release
workflow runs it once more before a release.

```
cmake --preset linux-release      # or msvc-debug on Windows
cmake --build --preset linux-release
ctest --preset linux-release
```

Covered core modules (selection):

| Module | Test |
|---|---|
| DWRS calculation | `test_dwrs` |
| Best XI / B team | `test_squadbuilder`, `test_roleratings` |
| Default role assignment (additive) | `test_roleassignment` |
| DWRS recalculation, 1 % threshold, cache patch | `test_ratingsupdater`, `test_roleratings` |
| Gap analysis (displacement, wrong side, drop-off, empty slots) | `test_gapanalysis` |
| Tactic explorer (coverage, depth, median, ranking) | `test_tacticexplorer` |
| Talent score | `test_talentengine` |
| CSV/HTML import | `test_csvimport`, `test_htmlimporter` |
| Pitch geometry per version | `test_pitchgeometry` |
| Save profiles (separation, migration from 2.0.4, atomic saving) | `test_profile` |
| Two line-ups (flow, bridges, absence with exactly one replacement, who takes over, starting XI limit) | `test_twophase` |
| Tactic views (combined map between both places, overview markers, clicks) | `test_pitchviews` |
| Scouting with several requirements (AND, bridge in/out of possession) | `test_scoutsearch` |
| Profiles via AppContext: switch A → B → A, restart, copy, logo | `test_profileswitch` |

The tests use `legacy/config/definitions.json` as a fixed data basis (FM24).
New version profiles (FM27) get their own test cases once their roles and
formations are settled.

## 2 · Golden master comparison (by hand, before every merge into `main`)

Compares the C++ results value by value with the old Python version. It needs a
real legacy save and therefore does not run in CI. **Only work on a copy**;
never write to real saves (`brighton.db` is suitable for testing).

1. Create the reference with the Python version (in the `legacy/` folder):
   ```
   python tools/export_golden.py <db_name> <out>
   python tools/export_golden_squad.py <db_name> <out>
   ```
2. Convert the save to the new format:
   ```
   fmmigrate legacy/databases/<db_name>.db <out>/migrated.db
   ```
3. Compare:
   ```
   fmgolden <out>/migrated.db legacy/config/config.ini legacy/config/definitions.json \
            <out>/dwrs_<db_name>.csv <out>/talent_<db_name>.csv
   fmsquad  <out>/migrated.db legacy/config/config.ini legacy/config/definitions.json \
            <out>/squad_<db_name>.json
   ```
   Both tools print the number of differences.

Deliberate differences from the legacy version (loan logic, additive role
assignment) can show up here as a difference. They are intended and are not
"fixed".
