# Save profiles (from 2.1.0)

Every save has its own complete profile. Two saves no longer share any setting,
and everything that makes up a save lives in its `.db` file. Duplicating,
renaming, backups and importing another database therefore take the profile
along automatically.

## What goes where

| Content | Location | Access in code |
|---|---|---|
| Rating weights (all FM versions), role multipliers, APT weights | profile, `profile_settings` | `context.config()` |
| Analysis options: age limits, selection bonus, in-possession share, squad planning, gap analysis, freshness, training window | profile, `profile_settings` | `context.config()` |
| Whole colour scheme (7 roles per mode), light/dark mode, colour template | profile, `profile_settings` (`ThemeSettings/…`) | `config().themeSettings()`, `ThemeManager` |
| Folder for FM exports | profile, `profile_settings` (`Import/…`) | `config().htmlExportDir()` |
| Own roles, tactics (per FM version) | profile, `profile_documents` `definitions_<version>` | `context.definitions()`, saving with `context.saveDefinitions()` |
| Club logo, national flag | profile, `profile_documents` `club_logo` / `national_flag` (PNG) | `context.profileImage()` / `setProfileImage()` |
| Copy of the crest from the graphics folder | profile, `profile_documents` `graphics_crest` (image file as in the folder) + setting `graphics_crest_id` (club id) | `context.ownClubCrestData()`; renewed by `refreshCachedCrest()` after opening, import and index loading. An own logo takes precedence; on a club change the copy is dropped or replaced |
| Training profile (bands, age windows, weights, attributes) | profile, `profile_documents` `training_profile` (JSON, format `fmpa-training-profile`); if missing, the default from `[Training]` applies | `trainingProfile(context)` in `PageHelpers.h`; Settings → Training stores only differences from the default |
| Club, second team, country, stadium, favourite tactics, national team, registration list rules, bridges, page state (filters, budget …) | save, table `settings` (unchanged since 1.x) | `context.database().setting()` |
| Club header: style (`header_style` = `gradient`/`solid`), crest silhouette (`header_crest_watermark`) | profile, `profile_settings` (`ThemeSettings/…`) | `ThemeManager::headerStyle()`, `crestWatermark()` |
| Own colour templates | app-wide, `<data folder>/color_templates.json` | `ColorTemplateStore` (core) |
| Language, window size, data folder | app-wide, `bootstrap.ini` | `context.paths()` |
| Last opened save, "always open the last one", list of recently opened | app-wide, `config.ini` | `config().dbName()` etc. |
| Look of the start screen | app-wide, `config.ini` `[StartTheme]` (copy of the last opened profile) | automatic |

Without an open save (start screen, command-line tools, tests) all profile keys
read and write `config.ini` as up to 2.0.4.

## Storage format

Two tables in the save file, created on first opening with 2.1.0
(`CREATE TABLE IF NOT EXISTS`, no version step of the database schema):

```sql
profile_settings  (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID
profile_documents (name TEXT PRIMARY KEY, encoding INTEGER, size INTEGER,
                   data BLOB, updated_at TEXT)
```

* `profile_settings` holds flat `section/key` pairs with the same names as
  `config.ini`. Values move 1:1, and the getters/setters of `AppConfig` are
  unchanged. Markers: `Profile/format` (currently 1), `Profile/seed`
  (`config.ini`, `defaults` or `copy:<save>`), `Profile/created_at`.
* `profile_documents` holds larger content that is read only when needed. JSON
  is compressed with zlib (`encoding` 1), images stay as they are (0).

### Why this way (after measuring)

* **Rows instead of one JSON block for settings:** the whole profile (77
  values, 2.3 KB) loads in one query in under 3 ms. Rows allow targeted
  writing: a light/dark switch writes exactly one row, unchanged values are not
  written at all (test `unchangedValuesAreNotWritten`). A block would have to be
  rewritten completely on every change.
* **Roles/tactics compressed:** 54 KB of JSON become 4 KB.
* **In the save instead of a side file:** one file stays one unit (backup, copy,
  import), and a SQLite transaction makes saving atomic. The profile costs
  16 KB per save, under 0.01 % of a large 280 MB save.

## Measurements

Tool: `fmprofilebench <definitions.json> [--repeat N]` (creates artificial
saves in a temp folder, touches nothing real). Linux, release build, median of
5 runs, 4 Nov 2026.

| Measurement | small (500 players) | large (35,000 players) |
|---|---|---|
| Save file | 4.0 MB | 277 MB |
| Extra size from the profile | 16 KB | 16 KB |
| Migration (create profile once) | 5 ms | 2 ms |
| Profile ready (colours, weights, roles) | 3 ms | 3 ms |
| Full open, 2.0.4 | 29 ms | 2.9 s |
| Full open, 2.1.0 | 20 ms | 2.1 s |
| Switch large → small, 2.0.4 / 2.1.0 | – | 2.8 s / 2.7 s |
| Save button (all settings), 2.0.4 | 91 ms | 91 ms |
| Save button, 2.1.0 (one transaction) | 3 ms | 2.5 ms |
| Toggle light/dark, 2.0.4 / 2.1.0 | 1.5 ms / 1.7 ms | 1.5 ms / 1.9 ms |
| Repeated full import of all players, without / with profile | – | 10.0 s / 9.6 s |

Result: the profile does not make opening, switching or imports slower (the
differences are within measurement noise). The save button is about 30 times
faster because it no longer syncs `config.ini` after every single value but
writes one transaction. When opening large saves, almost all the time goes into
loading players and ratings (35,000 players, 420,000 ratings). The profile is
therefore loaded first and announced with `AppContext::profileChanged` before
the players arrive. Speeding up the opening of large saves further means
working on player loading (separate package).

## Migration from 2.0.4

* **Existing save** (file exists, no profile yet): on first opening it takes
  over the settings all saves shared up to 2.0.4: every profile section from
  `config.ini`, the shared `definitions*.json` of the FM version (own roles and
  tactics) and the logo/flag from `assets/` (`club_logo_path`,
  `national_flag_path`). After that it is independent.
* **New save:** starts with the defaults, optionally as a copy of another
  save's profile ("New save" assistant, field "Settings"). Weights, colours,
  options, roles, tactics and logo are copied, not club, players and favourites.
  A source save without a profile (never opened with 2.1.0) is not changed.
* `config.ini` keeps its 2.0.x sections unchanged as the source for saves not
  opened yet, as do the old `definitions*.json` and logo files. Going back to
  2.0.4 therefore stays possible (2.0.4 ignores the new tables).
* Newer shipped role data (`data_revision`) is applied per save on opening
  (`Definitions::applyShippedUpdate`, own tactics stay). The previous state is
  kept as `backups/<save>_definitions_<version>_before_update_<time>.json`.

## Saving and loading

* **Save button of the settings:** `AppConfig::Transaction` opens a transaction
  on the save. Club settings (`database().setSetting`) and profile values run
  inside it together and are written together with `commit()`. If any step
  fails, everything is rolled back, in memory too. The page then shows an error
  and reloads the stored state. A half-saved profile does not exist (test
  `failedSaveWritesNothing`).
* **Single setters** (e.g. light/dark in the header) write immediately and only
  on a real change. They return `false`, details in `config().lastError()`.
* **Loading:** `AppContext::openDatabase()` attaches the save's profile, loads
  roles/tactics and emits `profileChanged()` (colours and mode are reapplied via
  `ThemeManager::reload()`), then the players. If the profile fails, the
  previous save stays active with its profile.
* **Acceptance:** `test_profileswitch` creates a data folder like 2.0.4 (tuned
  `config.ini`, own tactic, logo file), opens it, creates a second save with
  other weights, colours and tactics, switches A → B → A → B, restarts and
  checks the same again.

## For later packages

* **New setting that belongs to the save:** add the section to
  `isProfileSection()` (AppConfig.cpp) and the default to `profileDefaults()`.
  App-wide settings stay outside the list.
* **Tactics per save (done in 2.1.0):** `core/TacticLibrary` edits the
  document `definitions_<version>`. Next to the tactics are `edited_tactics`
  (saved by the user, an update keeps their version), `deleted_tactics`
  (deleted or renamed, an update does not bring them back) and `custom_roles`
  (own or imported roles, survive updates). Shipped data can rename a template
  (`renamed_tactics` {old: new}); unchanged copies are then renamed, and
  favourites (`favorite_tactic_1/2`, `national_fav_tactic_1/2`) as well as
  `bridges/<tactic>` follow (`TacticLibrary::renameTacticReferences`).
* **Tactic exchange format `.fmpatactic`:** JSON with `format`
  ("fm-player-analyzer-tactic"), `format_version` (currently 1), `app_version`,
  `fm_version`, `tactic` (`name`, `ip`/`oop` or `lineup`, `layout`) and `roles`
  (name, category, phase, positions, key/preferable of every role used). Roles
  missing on import are created from the file; another FM version, a newer
  format, a broken file or undescribed roles are rejected
  (`TacticLibrary::checkImport`). New fields: increase `format_version` and keep
  reading older files.
* **Own colour templates (done):** app-wide in
  `<data folder>/color_templates.json` (`ColorTemplateStore`, written
  atomically via `QSaveFile`; installer and updates do not touch the data
  folder). Each template holds all 14 colours, a name and optionally a league
  (empty = "Custom"). The id `user:<uuid>` is stored as
  `ThemeSettings/theme_preset` in the profile. Applying sets the colours on the
  appearance page; the save button writes them to the profile via the
  `AppConfig::Transaction`. Deleting a template leaves saves that use it
  unchanged.
* **Club header (done):** `ClubHeaderBar` draws the block in the club colours;
  colours and contrast come from `clubHeaderPalette()` (`core/ThemeColors`). The
  header gets the crest exclusively via `AppContext::headerCrest()`. Further
  sources (e.g. the FM graphics folder) plug in there without changing the
  header.
* **Format changes:** increase `Profile/format` and add the migration to
  `AppConfig::attachProfile`.
