# FM Player Analyzer

The Analyzer is a C++ desktop application using Qt 6 for FM24 and FM27.
Desktop code lives in `desktop/`; `legacy/` is a reference and migration source.

## Working practices

- Verify the requested branch, tag or test-build commit and existing local changes. When reviewing a test build, its exact commit is authoritative.
- Start from the affected feature. Search with `rg`, inspect callers and relevant tests, and expand the investigation when evidence requires it. For version questions, read the relevant `CHANGELOG.md` section first.
- Keep changes within scope and reuse existing types and functions. Centralize domain decisions needed by multiple execution paths.
- In reviews, lead with reproducible findings, triggers, impact and source locations. Distinguish measured runtimes, confirmed defects and hypotheses.
- Preserve unrelated and uncommitted work. Never reset destructively or move published tags without explicit authorization.

## Entry points

Paths below are relative to `desktop/src/`; tests live in `tests/`.

| Area | Entry points | Relevant tests |
|---|---|---|
| Import and FM27 live source | `core/ImportPipeline.cpp`, `core/HtmlImporter.cpp`, `core/Fm27SaveImport.cpp`, `app/LiveSaveWatcher.cpp` | `test_htmlimporter`, `test_fm27save` |
| Database, save profiles, history | `core/Database.cpp`, `core/ProfileStore.cpp`, `core/ImportHistory.cpp`, `app/AppContext.cpp` | `test_database`, `test_profile`, `test_profileswitch`, `test_importhistory` |
| Lineups and tactics | `core/SquadBuilder.cpp`, `core/TacticLibrary.cpp`, `core/PitchGeometry.cpp` | `test_squadbuilder`, `test_twophase`, `test_tacticlibrary`, `test_pitchgeometry` |
| Registration | `core/Registration.cpp`, `app/pages/RegistrationPage.cpp` | `test_registration`, `test_database` |
| Settings and appearance | `app/pages/SettingsPage.cpp`, `app/theming/ThemeManager.cpp`, `core/AppConfig.cpp` | `test_profileswitch`, `test_themecolors`, `test_charts` |

## Project rules

- Domain logic belongs in `core/`; interaction and presentation belong in `app/`. Load only necessary data in frequent UI paths and measure repeated computation.
- Save-specific data belongs to the active save profile. Copy settings only for reusable options such as weights, colours, roles and tactics. Club name, club assignment, crest, flag and live source stay with the original save.
- For database changes, examine migrations, older data and import state together. Persisted and displayed values must agree after player merges. Copies and backups must include committed SQLite WAL changes.
- Check FM27 visibility, scouting selection and attribute masking changes against the requested scope. Handle unknowns and ranges consistently across import, rating and export.
- Check FM24 and FM27 separately when roles or lineups change. FM24 displays A/B teams side by side; FM27 distinguishes possession phases.
- English is the default language. Maintain complete English and German UI translations using the existing translation system and language preference. Personal names are not translated. Write new documentation, technical comments and release notes in English, and update affected German user documentation.
- For UI design work, use `.claude/skills/unslop/SKILL.md` when available. Club gradients and crests belong to club presentation. This optional local skill is not required to build the repository.

## Build and tests

- Windows build from the repository root: `.\desktop\scripts\build.ps1 -Preset msvc-release`. Requirements and other commands are in `desktop/README.md`.
- After building, run affected suites first, for example `ctest --test-dir desktop/build/msvc-release --output-on-failure -R '^(test_database|test_htmlimporter)$'`. Add meaningful regression tests for otherwise undetected defects.
- Before a test build or release, run the complete build and suite with `.\desktop\scripts\build.ps1 -Preset msvc-release -Test`. Tests using saves must use temporary files or copies. Report optional real-save FM27 tests and skipped tests separately.
- Never label incomplete feature work as a complete test build. Keep prerelease versions unique and do not overwrite published artifacts.
