# DWRS 2.3.0 development and acceptance plan

Target: complete `2.3.0-test.1`, followed by user acceptance before stable 2.3.0.
Base: stable `v2.2.3`, commit `9490f54f63b57a8b617d1eeb1acd5826613b6399`.
Branch: `feature/2.3.0-dwrs-profiles`. No recovery or review checkout is used.

Status labels describe verified completion only. No test build is ready yet.

- [x] A: inspect actual checkout, local instructions, stable ancestry, issue and both comments; preserve local `.claude/`.
- [x] A: track sanitized English shared AGENTS.md without losing existing rules.
- [ ] B: versioned full JSON format, immutable formula revisions, explicit role/position inheritance and origin labels.
- [ ] C: reproduce Classic group distribution, role factors, goalkeeper rules, rounding, missing values and ranges with reference tests.
- [x] D: extract all 33 measured attributes; retain original values, isotonic fits, 20-point curves and exclusion reasons. User approved cutoff <=4 pp on 2026-11-09; 18 visible attributes retained.
- [ ] E: universal evaluator and full visible attribute flow including Natural Fitness and unmeasured editable attributes.
- [ ] F: unrestricted personal profile management, editable built-ins, retained originals, revisions, atomic persistence, conflict-safe JSON import/export.
- [ ] F: backed-up transactional migration, customized weights preservation, defaults for new saves and one-time choice for existing FM27 saves.
- [ ] G: Settings / DWRS Rating with bilingual profile controls, searchable attribute groups, draggable 20-point curve, numeric inputs, weight, contribution/increment mode, linear/plateau/reset, undo/redo, keyboard access and draft cancellation.
- [ ] G: advanced base/position/role rules including separate FM27 phases, inheritance origins and reset-to-parent.
- [ ] G: side-effect-free same-snapshot squad preview including ratings, ranks and Best XI; affected-save warning and explicit activation.
- [ ] H: player views, scouting, comparisons, Best XI, depth, signing, training, talent and other derived consumers use one revision; nonlinear training gains simulated at current values.
- [ ] H: immutable background snapshots, revision-keyed caches, stale-result rejection and responsive large datasets.
- [ ] H: preserve old history; unknown legacy provenance; segmented changes and comparable deltas; future entries include attributes/ranges/missing values, save date, role, formula revision and reproduction rules.
- [ ] I: full regression, malformed imports, concurrency, realistic data/performance and bilingual/theme/window-size UI checks.
- [ ] I: English release notes/design/user documentation, affected German workflows, migration/rollback and manual acceptance guide.
- [ ] J: complete version-consistent installer/portable build with numeric installer version mapping, no artifact overwrite, explicitly prerelease download.

## Findings and release gates

- HEAD, origin/main and v2.2.3 match. No 2.3.0 tag was found at discovery.
- Fetch reported a differing existing `testbuild-2.2.3` tag; it was not overwritten.
- Current testbuild workflow deletes/recreates its release tag and accepts only numeric versions. It must be adapted before this prerelease is published.
- Classic currently uses midpoint ranges and numeric zero for missing attributes. Preserve this under a named legacy calculation policy; do not silently apply it to the new profile.
- No production save or historical rating has been changed.
- Optional source measurement overlay and CSV exchange are optional; all other requested editor and integration features remain required.

## Verified implementation checkpoint (2026-11-10)

See development-checkpoint-2026-11-10.md for the exact completed foundations, verification and next steps. Curve/profile/library cores, schema-v17 visible attribute flow, 4,608 Classic references, immutable compiled snapshots and nonlinear training overloads are implemented. Full application activation, built-in Classic/goalkeeper representation, UI, history and release gates above remain open. The final full build and all 45 suites passed, including the import-validation additions.

## Editor checkpoint

See development-checkpoint-editor.md. The DWRS settings page now contains the
profile library and 20-point curve editor while retaining all legacy controls.
All 47 visible attributes have English/German labels and category/search filters.
Draft persistence, conflict handling, reset/discard and theme/language rendering
have widget coverage. This is still an incomplete development build: activation,
advanced rules UI, preview, migration, history and the other release gates above
remain open. The built-in definitions are not yet seeded into the UI library.
Final editor verification: MSVC release build and 47/47 CTest suites passed (7.26 seconds), including the user-requested two-decimal display and preservation of underlying curve precision.
