# FM Player Analyzer 2.3.0 development checkpoint

Status: implementation in progress; **not a complete 2.3.0-test.1**.

## Authoritative checkout

- Use the existing `feature/2.3.0-dwrs-profiles` branch in the actual development repository. Do not select recovery/review directories from the chat workspace.
- Stable base: `v2.2.3`, `9490f54f63b57a8b617d1eeb1acd5826613b6399`.
- Shared instructions are in root `AGENTS.md`, translated and preserved from the user's original local instructions. Read them before continuing.
- Unrelated `.claude/` and `FM-Player-Analyzer.zip` are untracked user content; preserve them.
- No tag moved, no release uploaded, no production save opened or migrated.

## Approved domain decision

The user chose a **4 percentage-point cutoff**, not the initially proposed 6:
equal-weight isotonic regression at 1/6/12/18, subtract the fitted first point,
disable the entire attribute if fitted 1→18 total <=4, interpolate retained
curves and hold constant from 18–20. This retains 18 measured visible attributes,
including Crossing and Vision. This is a modelling cutoff, not a statistical
significance claim. The complete comparison and all 20-point curves are in
`harvestgreen-comparison.md` / `.json` in this directory.

## Implemented and verified foundations

- `RatingCurve`: canonical weight and 20-point monotone shape, explicit unavailable
  statuses, uniform integer-range expectation, equal-weight isotonic preparation.
- `RatingProfile`: format-v1 JSON, strict field/type validation, separate metadata
  and formula identity, fieldwise position/role inheritance with origin labels.
- `RatingProfileLibrary`: conflict-safe CRUD/duplication/reset, original definitions,
  formula snapshots, QSaveFile atomic persistence and QLockFile plus optimistic
  file digest to prevent concurrent overwrites. Application/library seeding and
  save activation are not wired yet.
- Five appended visible attributes: Natural Fitness, Free Kick Taking, Long Throws,
  Penalty Taking and Punching. FM24/FM27 export headers now map to these fields;
  existing FM27 live-save extraction already provides them. Database schema v17
  adds range columns and preserves missing data. Existing databases receive a
  dedicated, non-rotated SQLite snapshot before migration, including WAL data.
- `test_classicreference` freezes 4,608 results captured with unchanged 2.2.3
  DwrsEngine: all FM24/FM27 roles, default/custom settings, levels 1/6/12/18/19/20,
  ranges and missing values. Fixture capture refuses to overwrite existing data.
- `RatingSnapshot`: immutable compiled contribution tables, role-definition-aware
  cache identity, direct Player evaluation, precise range averages, current-range
  +1 training gains, explicit goalkeeper role classification. It validates
  current-game role/position bindings at compilation.
- `TrainingAdvice` has overloads accepting RatingSnapshot for attributes and
  focus recommendations. Nonlinear priorities use expected +1 gain times the
  existing age factor; Classic retains its previous headroom-weighted behavior.
  The UI still calls the existing Classic overload until activation is integrated.

## Verification on 2026-11-10

`desktop/scripts/build.ps1 -Preset msvc-release -Test` completed successfully.
All **45 CTest suites passed**, total test time 8.27 seconds in the final run.
The new snapshot suite compares all 210 inclusive ranges against the pure
evaluator, checks plateau/capped range training, formula cache changes and metadata
renames, immutable old snapshots, and two concurrent readers over 80,000 synthetic
players. This is a correctness/concurrency check with a one-attribute synthetic
profile, not a real-squad performance acceptance measurement.

An initial new test edited the key list although Pace is preferable for AF-A;
the fixture was corrected to edit the preferable list and the comparison now
checks absolute contributions as well as normalized scores.

## Required next work — do not silently reduce scope

1. Finish built-in profile representation and factories. Exact Classic parity must
   cover group distribution and role-dependent goalkeeper extra categories, legacy
   missing-value semantics, midpoint ranges and rounding. Existing production
   `DwrsEngine` is still unchanged. Do not claim the new generic snapshot already
   reproduces Classic or ships the separate goalkeeper basis.
2. Complete profile schema for all required source/goalkeeper/Classic reproduction
   information. No exchange format has been released yet; revise it deliberately
   with round-trip tests where the exact Classic representation requires it.
3. Wire profile library, per-save selection, new-save defaults, preservation of
   individualized weights, and idempotent one-time FM27 migration choice. Avoid
   changing an active save's scoring before a coherent result is ready.
4. Implement the full bilingual Settings → DWRS Rating editor and advanced rules,
   draft undo/redo/cancel, import conflict choices, restore, affected-save warnings,
   and explicit activation.
5. Integrate every analysis consumer, preserving unavailable ratings rather than
   coercing them to zero. Add same-snapshot squad/rank/Best-XI comparison. The
   legacy engine result type currently uses doubles and needs a deliberate
   unavailable-result propagation strategy across callers.
6. Bind background jobs to immutable profile and role-definition state; reject
   superseded jobs on publication. The new snapshot enables this but application
   lifecycle guards are not implemented.
7. Implement historical provenance, attributes/ranges/missing snapshots, game date,
   calculation version and role rules, segmented views and comparable deltas.
   Never relabel old history with current profiles. Existing history remains intact.
8. Complete tests, real-squad comparisons, UI/theme/size/accessibility checks and
   representative performance measurements. Optional real FM27-save testing and
   individual skipped test cases must be reported separately before release.
9. Update English and affected German documentation, migration/rollback guide and
   manual acceptance guide. Return to 2.2.3 requires restoring the pre-migration
   schema snapshot; do not open the schema-v17 database with the stable application.
10. Assign 2.3.0-test.1 only when the agreed features are complete; recheck published
    versions first. Adapt the existing testbuild workflow, which currently accepts
    numeric-only versions and deletes/recreates a testbuild tag. Never overwrite
    published artifacts. Prepare installer numeric/full-version mapping, package,
    and publish only with the user's explicit release authorization.

Application version remains 2.2.3 in this unfinished branch; no packaged artifact
from it should be delivered as stable 2.2.3 or as complete 2.3.0-test.1.
