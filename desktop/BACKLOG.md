# Backlog: robustness, performance & code quality

Open improvements from the architecture review (2026-07-13). **Bugs 1–7**
(data loss/corruption/crash risks) are fixed in **v1.2.5**.

Format: `file:line` — problem — proposal.

---

## ✅ Done (v1.2.6 – v1.2.8)

The three work steps from the review are done except for two remaining points:

| Step | Version | Points |
|---------|---------|--------|
| 1 — cleanup + contained perf wins | v1.2.6 | 15, 16, 17, 18, 20, 21 |
| 2 — perf levers reload/ratings/search + schema v2 | v1.2.7 | 11 *(partly)*, 12, 13, 14, 19 |
| 3 — robustness | v1.2.8 | 8, 9, 10 |

Summary of the implementation:

- **#8** DwrsEngine plan cache: all `validRoles` plans are built up front on
  the UI thread in `reloadConfig()` → workers only read the cache.
- **#9** `Player*` lifetime: `PageBase::releaseStoreRows()` clears the raw
  `Player*` of hidden pages on `dataChanged` (4 `PlayerTableModel` pages +
  `SquadMatrix::m_scoutedPool`) before the store is re-read.
- **#10** AssignRolesPage: `savePending`/`autoAssign` with revert on failure.
- **#12** `dwrs_latest` table (materialised "latest per player/role"),
  maintained in `appendDwrsRatings`/`mergePlayerInto`; `latestDwrsRatings()`
  is now a scan instead of a `MAX(ts)` self-join.
- **#13** `rebuildSuggestions` iterates only over rated players per role.
- **#14** Sidebar search: slim `PlayerSearchModel` + `FoldingCompleter`
  (umlaut/accent folding).
- **#15** GapAnalysis caches parsed positions per player.
- **#16** In-memory cache of the DB settings.
- **#17** Shared `widgets/NumericTableItem.h`.
- **#18** `parseDwrsTimestamp()` in `core/Utils`.
- **#19** Schema migration v1→v2: `dwrs_latest` + dead columns
  `registration`/`information` removed via `ALTER TABLE DROP COLUMN`.
- **#20** Milestone placeholder → deliberate "unknown pageId" guard.
- **#21** Widget clean-up unified on `deleteLater()`.
- **Addendum to #14 (v1.2.9):** the profile page search also moved to
  `PlayerSearchModel`. The old `QStandardItemModel` build fired one
  `dataChanged` per player, and the completer re-filtered with the old search
  text every time (O(n²) → 20–30+ s freeze on the first keystroke after every
  reload/scope switch). Now a single model reset (<0.5 s).

---

## ✅ Rest of #11 — full reload on every small save — done in v1.3.19

> **Implementation:** `Database::loadPlayers(ids)` (chunked by id, including
> roles/squad/shortlist/training role), `PlayerStore::replace` (in place,
> indexes stay consistent, `Player*` stay valid),
> `RatingsUpdater::patchRoleRatings` (incremental = full rebuild, tested).
> `AppContext::refreshPlayers(ids)` / `refreshRatings(ids)` /
> `setNationalSquad(ids)` replace `reloadFromDatabase` in transfers, edit
> player, departures, call-up assistant, squad selection, national import,
> manual single update and after every partial recalculation (assign roles).
> Falls back to a full reload if a player is missing/new. **Measured**
> (snapshot, 35k players): full reload 1,458 ms → targeted 20 players 15 ms.
> A full reload only remains on a DB switch. Tests:
> `loadPlayersSubsetMatchesFullLoad`, `patchEqualsFullRebuild`.

- **File:** [src/app/AppContext.cpp](src/app/AppContext.cpp) `reloadFromDatabase`;
  callers include TransfersPage, EditPlayerPage, DashboardPage (departures),
  NationalDashboardPage, NationalSquadSelectionPage.
- **State:** the most expensive part (ratings aggregate) is gone with **#12**,
  and the recalc path now uses `reloadRatings()` (ratings only, players
  untouched). The remaining save paths still reload **all** ~35k players
  synchronously for 1–20 changed rows.
- **Proposal:** patch changed players in the store in place (per row, keeps
  `Player*` valid; related to **#9**) and emit only `dataChanged` instead of
  replacing the whole store. Needs a targeted `Database::loadPlayers(ids)` path
  and a store patch per caller.

---

## ✅ #22 — `dwrsHistory` builds an unbounded `IN (…)` clause — done in v1.3.17

> `Database::forEachIdChunk` (blocks of 500, sorted → globally ordered), used by
> `dwrsHistory` and the new `latestDwrsRatings(ids)`.

- **File:** [src/core/Database.cpp](src/core/Database.cpp) `dwrsHistory`.
- **Problem:** with very large id lists, theoretically above the placeholder
  limit (in practice limited by club).
- **Proposal:** chunking (e.g. blocks of 500) or a temp table join.

---

# Code analysis 2026-09-28 (state v1.3.11)

New findings from a full review of core + the app's save/reload/worker paths.
Numbering continues the review list. Recommended order:
~~#23~~ → ~~#28~~ → ~~#24/#25 (+#33)~~ → ~~#30~~ → ~~#26/#27~~ → ~~#31/#32~~ (+ ~~#22~~). ~~#38~~ → ~~#11~~ → ~~#34~~ → ~~#35~~ → ~~#36~~ → ~~#37~~.
Feature ideas are listed separately in [IDEAS.md](IDEAS.md).

## 🔴 Functional bugs

### ✅ #23 — Removed roles keep working (stale ratings + `primaryRole`) — done in v1.3.12

> **Implementation:** `RatingsUpdater::roleRatingsForAssigned` builds the
> rating cache only from assigned roles (history/`dwrs_latest` stay untouched,
> works on old data without migration). `SquadBuilder` ignores a `primaryRole`
> that is no longer assigned; `AssignRolesPage` clears it on save
> (`RoleAssignment::clearStalePrimaryRole`, revert on DB error). Regression test
> `test_roleratings`.

- **Files:** [src/core/RatingsUpdater.cpp](src/core/RatingsUpdater.cpp)
  (role grouping), [src/core/SquadBuilder.cpp](src/core/SquadBuilder.cpp)
  `buildCandidates`, [src/app/pages/AssignRolesPage.cpp](src/app/pages/AssignRolesPage.cpp)
  `savePending`/`autoAssign`, [src/app/AppContext.cpp](src/app/AppContext.cpp)
  `rebuildRatingsCache`.
- **Problem:** when a role is taken from a player, its last row stays in
  `dwrs_latest` (recalc only calculates assigned roles and deletes nothing).
  `m_ratings` still contains it, and `SquadBuilder` does not check
  `assignedRoles` → the player can still end up on the removed role in Best XI,
  gap analysis, tactic explorer, call-up assistant and transfer targets (with a
  frozen value). In addition, a `primaryRole` stays set to the removed role →
  the player is locked out of **every other** slot
  (`primaryRole != role → continue`). Legacy parity, but wrong.
- **Proposal:** when saving role changes, delete the `dwrs_latest` rows of
  removed roles (keep history) and clear a `primaryRole` that is no longer
  assigned; alternatively let `rebuildRatingsCache` take over assigned roles
  only. Regression test.

### ✅ #24 — "Retired"/stale players in suggestions — done in v1.3.14

> **Implementation:** new core module `core/PlayerStatus` (`isRetiredClub`,
> `isRetired` = club "Retired" **or** freshness retired, `isNationalEligible`,
> `isAvailableForNation`). Retired players are now excluded from the dashboard
> transfer targets, the national transfer targets, squad selection (available
> players) and the call-up assistant; the "hide 'Retired'" checkbox of both
> matrices also hides freshness-retired players. **Deliberately not** excluded:
> merely *stale* players; old scouting data does not mean the player is gone.

- **Files:** [src/app/pages/DashboardPage.cpp](src/app/pages/DashboardPage.cpp)
  `rebuildSuggestions`, NationalDashboardPage (transfer targets),
  NationalSquadSelectionPage.
- **Problem:** the user convention club = "Retired" (and the live freshness
  classification retired/stale) is only respected in the call-up assistant and
  in the squad matrix. The dashboard transfer targets suggest such players.
- **Proposal:** a central availability check in core (see #33), used in every
  suggestion pool.

### ✅ #25 — Departure dialog: three gaps — done in v1.3.14

> **Implementation:** (1) `isFreeAgent` recognises "FrA" (case-insensitive).
> (2) `PlayerStatus::applyDeparture` clears transfer/loan list and `newClub`.
> (3) **Dropped:** the transfers page takes over an entered "new club" as the
> club immediately (legacy behaviour); a player with a planned destination is
> never at the own club any more and therefore does not appear in the
> departure dialog.

- **Files:** [src/app/pages/DashboardPage.cpp](src/app/pages/DashboardPage.cpp)
  `resolveDepartures`, [src/core/Utils.cpp](src/core/Utils.cpp) `isFreeAgent`.
- **Problems:**
  1. Destination "FrA" is not recognised as clubless by `isFreeAgent()` →
     these players are missing from the squad matrix free agent search.
  2. On departure, `transferStatus`/`loanStatus`/`newClub` stay set.
  3. A `newClub` planned on the transfers page is not prefilled as the
     destination (always "FrA").
- **Proposal:** add "FrA" to `isFreeAgent`, clear the flags on departure,
  prefill the destination with `newClub` if set.

### ✅ #26 — Unstamped old players never become stale — done in v1.3.16

> **Implementation:** `Freshness::uploadsSinceSeen` treats `lastSeenUpdate == 0`
> as "last seen at upload 0"; as long as the counter is 0, everything stays
> fresh. No sudden flood: the counter starts at 0 with freshness tracking
> (bayern2026-2-0 on 2026-09-29: counter 0, all 34,951 players unstamped →
> "stale" only applies after 5 future imports without the player). Tests in
> `test_freshness`.

- **File:** [src/core/Freshness.cpp](src/core/Freshness.cpp) `uploadsSinceSeen`.
- **Problem:** `lastSeenUpdate == 0` (imported before v1.3.5, never seen since)
  counts as "current" forever → auto-retired never applies to exactly the
  oldest dead entries.
- **Proposal:** treat unstamped rows as "last seen at upload 0" (stale after Y
  uploads). Adjust the tests in `test_freshness`.

### ✅ #27 — HTML import is not atomic — done in v1.3.16

> **Implementation:** nestable transactions in `Database`
> (`beginTransaction`/`commitTransaction`/`rollbackTransaction`: the outer level
> is a real transaction, inner levels are SAVEPOINTs; every write method uses
> them) plus RAII `ScopedTransaction`. `importHtml` wraps id unification, all
> batches and the upload counter in one transaction; a rollback also drops the
> settings cache. Counter-check: the old importer left 2,000 players behind
> after an error in the 2nd batch, the new one none. Tests: `test_htmlimporter`
> (`failedImportRollsBackEverything`), `test_database` (`nestedTransactions`).

- **File:** [src/core/HtmlImporter.cpp](src/core/HtmlImporter.cpp) `importHtml`
  (merges/renames + `flush` per batch of 2,000).
- **Problem:** id unification and every batch commit separately; an abort in
  the middle of an import leaves a partial import (only the backup helps).
- **Proposal:** one outer transaction over the whole import (the database
  methods must then not open their own transaction → savepoints or an
  `inTransaction` flag).

## 🟠 Robustness / threads

### ✅ #28 — Esc closes progress dialogs, worker keeps running — done in v1.3.13

> **Confirmed** (Qt 6.8.3): a `QProgressDialog` without a cancel button closes
> on Esc **and** hides itself automatically when reaching the maximum
> (`autoClose`/`autoReset`), but the workers report their last step *before*
> the final DB write. **Implementation:** `widgets/BusyProgressDialog`
> (ignores Esc/close until `finish()`, no auto close, no X button) in
> ImportRunner, RecalcHelper and full recalc; running jobs registered centrally
> via `AppContext::registerBackgroundTask`, waited for before engine/definitions
> reload, DB switch, `closeEvent` and in `~AppContext`; unique worker
> connection names (`Database::uniqueConnectionName`). The same pattern fixed
> in the migration assistant (Esc during migration). Tests `test_busydialog`
> (widget test, offscreen) + `test_database`.

- **Files:** [src/app/ImportRunner.cpp](src/app/ImportRunner.cpp),
  [src/app/RecalcHelper.cpp](src/app/RecalcHelper.cpp),
  [src/app/MainWindow.cpp](src/app/MainWindow.cpp) `startDwrsRecalc`/`closeEvent`.
- **Problem (very likely, verify in practice):** the `QProgressDialog`s have no
  cancel button, but Esc triggers `reject()` → dialog gone, window usable
  again, worker keeps running. Then possible: a second import (both workers use
  the connection name `"import_worker"`), saving settings (`reloadConfig()`
  clears the plan cache while the worker reads), closing the app (`closeEvent`
  only waits for `m_recalcWatcher`, not for import/`recalcDwrsFor`) →
  crash/use-after-free.
- **Proposal:** catch Esc/close in the dialog; register running futures
  centrally in `AppContext`; `closeEvent` waits for all; unique worker
  connection names.

### ✅ #29 — Club change in the dashboard combo without notification — done in v1.6.0

> **Implementation:** after saving `user_club`, the dashboard calls
> `m_context.notifySettingsChanged()`; the header and other pages follow.

- **File:** [src/app/pages/DashboardPage.cpp](src/app/pages/DashboardPage.cpp)
  `m_clubCombo` activated.
- **Problem:** `user_club` is saved, but the header and other pages do not
  learn about it.
- **Proposal:** call `m_context.notifySettingsChanged()`.

## 🟡 Performance

### ✅ #30 — `Definitions` rebuilds hashes from the JSON on every access — done in v1.3.15

> **Implementation:** `Definitions::rebuildCache()` builds all derived tables
> once in `load()`/`setRoot()` (a failed `load()` leaves them untouched); the
> accessors return `const &`, `personalityCategory` uses a precomputed
> lower-case table instead of a linear scan. **Measured** (release, real
> definitions.json, identical results): 35k × `personalityCategory` 421 → 4 ms;
> 2k × `tacticRoles` + `positionToRoleMapping` 116 → 0 ms; 2k ×
> `sortRolesNaturally` 313 → 1 ms. Tests in `test_definitions` (cache follows
> `setRoot`, failed load, defaults).

- **File:** [src/core/Definitions.cpp](src/core/Definitions.cpp)
  (`personalities`, `personalityCategory`, `tacticRoles`,
  `positionToRoleMapping`, `roleDisplayMap`, `naturalRoleSorter`, …).
- **Problem:** `personalityCategory()` runs in hot paths: personality filter of
  the squad matrix per player (35k), cell styles on **every** paint
  (background + foreground → twice), talent score. The training plan calls
  `tacticRoles()` + `positionToRoleMapping()` per player.
- **Proposal:** build parsed structures once in `load()`/`setRoot()`, accessors
  return `const &`. Biggest perf lever not recognised so far.

### ✅ #31 — Import writes/loads too much — done in v1.3.17

> **Implementation:** `upsertPlayers(…, RoleWrite::Keep)` in the import (roles
> only when taken over by a merge, via the new `replacePlayerRoles`); auto
> roles write only roles. The importer updates the player list in place
> (`updatedPlayers`, same order as `loadPlayers()`, including id reuse after a
> merge) → no second load in the worker; the UI takes it over via
> `AppContext::adoptPlayers` instead of a full reload. The HTML string is freed
> after parsing. The pipeline now lives UI-free in `core/ImportPipeline` (app
> and `fmbench` use the same code). Also found: writing DWRS was the biggest
> item → multi-row INSERTs + `PRAGMA cache_size = 64 MB` (8.1 → 3.5 s from the
> cache alone). **Measured** with `fmbench <db> --import` (new: import mode on a
> file copy), snapshot of bayern2026-2-0 (34,951 players) +
> `Wrexham_Scouting.html` (11,121 rows, 7,757 new, 164,631 DWRS rows written):
>
> | Phase | before | after |
> |---|---|---|
> | Worker total | 19.2 s | 8.3 s |
> | of which writing DWRS | 11.6 s | 3.7 s |
> | of which import DB + reload | ~2.7 s | 0.9 s |
> | of which auto roles | 1.5 s | 0.8 s |
> | UI thread (freeze) after import | 1.9 s | 1.1 s |
> Tests: `updatedPlayersMatchReload` (field by field + order vs. reload),
> `bulkRatingsAndChunkedReads`.

- **Files:** [src/core/Database.cpp](src/core/Database.cpp) `upsertPlayers`,
  [src/app/ImportRunner.cpp](src/app/ImportRunner.cpp).
- **Problems:** `upsertPlayers` deletes and rewrites the roles of **every**
  player although the import does not change roles (→ whole `player_roles`
  table per import). 3 full `loadPlayers` per import (worker before/after
  import + UI reload). Whole file as `QString` + complete cell matrix in memory
  (77 MB export → several hundred MB peak).
- **Proposal:** write roles only on change (flag/own path), limit the worker
  reload to affected players, process rows as a stream.

### ✅ #32 — Recalc paths load/copy too much — done in v1.3.17

> The full recalc ends with `reloadRatings()`; `recalcDwrsFor` copies only the
> affected players; `RatingsUpdater` loads only their `dwrs_latest` rows for a
> subset (`latestDwrsRatings(ids)`) instead of all ~400k.

- **Files:** [src/app/MainWindow.cpp](src/app/MainWindow.cpp) (full recalc
  finish), [src/app/RecalcHelper.cpp](src/app/RecalcHelper.cpp).
- **Problem:** the full recalc ends with `reloadFromDatabase()` instead of
  `reloadRatings()`; `recalcDwrsFor` copies all 35k players for a handful.
- **Proposal:** use `reloadRatings()`; copy only affected players.

## ⚪ Tech debt / consistency

- ✅ **#33 (v1.3.14, via `PlayerStatus`) — national eligibility duplicated 5×,
  inconsistent.** Call-up, squad selection, national squad matrix and national
  dashboard treat `age <= 0` and age limit 0 differently. → One core function
  (including the retired check, see #24).
- ✅ **#34 (v1.3.21) — Two auto-assign semantics.** Done: the button is now
  called "Add missing default roles" and, like the import, uses
  `RoleAssignment::missingRoleAdditions` (additive, writes only roles); the
  reset button uses `RoleAssignment::DefaultRoles`, duplicate removed.
  Originally: the import used the additive
  `RoleAssignment::autoAssignMissingRoles`; the button "unassigned only" in
  [AssignRolesPage.cpp](src/app/pages/AssignRolesPage.cpp) `autoAssign` had
  duplicated legacy logic. → Merge into `RoleAssignment`.
- ✅ **#35 (v1.3.22) — Three GK detections.** Done: `Player::isGoalkeeper()` is
  used by SquadBuilder, TalentEngine, NationalCallup and edit player; test in
  `test_nationalcallup`. Originally `SquadBuilder` (`contains("GK")`),
  `NationalCallup::isGoalkeeper` (positions), `TalentEngine::ageCapForPlayer`.
  → One function.
- ✅ **#36 (v1.3.23) — Small things:** done: `containsFolded` (table filter,
  edit player, squad selection, injury list), CSV separator by locale
  (`csvSeparator`/`csvField`), £/$ in `valueToFloat`, `DwrsEngine::planFor`
  without lazy insert. Originally: the table name filter (`PlayerFilterProxy`)
  had no umlaut folding (unlike the sidebar search); a CSV export with commas
  does not split in German Excel (`;` or a `sep=` line); `valueToFloat` only
  knew € (£/$ → market value 0); `DwrsEngine::planFor` filled the cache lazily
  for unknown roles (race if parallel to a worker).

## ✅ #38 — Remaining import costs — done in v1.3.18

> **HTML parser:** `extractTable` now finds tags via a `<` character search +
> ASCII comparison (instead of a case-insensitive substring search), finds
> `<td`/`<th` in one pass and takes plain text cells over as one copy. Proven
> identical: SHA1 over all cells of all 15 real exports unchanged
> (`fmbench --parse`). Speed: `bayern_allplayers.html` (86 MB) 16.0 → 1.3 s,
> `Wrexham_Scouting.html` (11 MB) 2.0 → 0.15 s. Edge case test
> `extractTableEdgeCases`.
>
> **UI takeover:** the import worker now builds the store + both rating caches
> (`ImportPipelineResult::store/latestRatings/ratings`, stage "Preparing
> result…"); the UI only swaps via `AppContext::adoptState`. UI freeze after
> the import 1.1 s → 0 ms; worker total 8.3 → 7.0 s.
>
> Note: other save operations still rebuild the rating caches on the UI thread
> (`reloadFromDatabase`/`reloadRatings`, ~1 s each with 35k players); that
> remains the rest of **#11**.

## ⚪ Test gaps

- ✅ **#37** (P7, 31 Oct 2026: tests for RoleAssignment, RatingsUpdater,
  GapAnalysis, TacticExplorer, TalentEngine; golden guide in TESTS.md) No unit
  tests for `SquadBuilder`, `RoleAssignment`, `RatingsUpdater`, `GapAnalysis`,
  `TacticExplorer`, `TalentEngine`; the squad golden comparison (`fmsquad`)
  does not run in CI. Cover at least #23 and #25 with regression tests.

---

# Performance review 2026-11-08 (state 2.2.3)

Done in 2.2.3: rating caches in one pass (`RatingsUpdater::loadRatingCaches`,
read errors are reported), a save switch rebuilds only the visible page once,
scouting filters change only the rows (columns/widths stay, sliders debounced).
Fixed on the way: crash when switching from the squad matrix with a large save
(pages released the old store's pointers too late). Synthetic measurement
(80,000 players, 960,000 ratings): switch ~8 s → ~4.8 s, filter change
~800 → ~50 ms.

## ⚪ #39 — Load the save in the background (deferred)

- `AppContext::openDatabaseImpl` loads players + caches synchronously on the UI
  thread (still ~3.5 s with 80,000 players, ~1.5 s with 35,000).
- **Proposal:** a worker with its own read-only connection loads the store +
  both caches in one read transaction, the UI takes over via `adoptState`.
  Serialise switching with import/recalculation, activate profile/version only
  on success, error case in `test_profileswitch`.
- **Why deferred:** reaches deep into profile activation and error paths; the
  waiting time is roughly halved after the changes above.

## ⚪ #40 — Fast finish of small partial imports (deferred)

- `ImportPipeline` (finalize stage) re-reads the whole store and all ratings
  after every import, even with 25 changed players.
- **Proposal:** for partial imports without UID change/merge, return only the
  changed players (including auto roles) and their ratings and apply them via
  `patchRoleRatings`.
- **Why deferred:** already runs in the worker (no UI freeze), gain ~2–3 s
  waiting time with very large saves, but high risk of diverging caches.

## ⚪ #41 — More precise `fmbench --import` (deferred)

- Measure the backup stage too, the first load of the store separately from
  parsing, and the real UI completion. Before changing the backup method
  (`VACUUM INTO` vs. online backup API), measure today's way first.
