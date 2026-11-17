# Ideas: features and extensions (C++/Qt)

Taken over from [legacy/Future_Ideas.md](../legacy/Future_Ideas.md) and checked
against the C++ state **v1.3.11** on 2026-09-28. Technical debt, bugs and
performance topics are listed separately in [BACKLOG.md](BACKLOG.md).

Priorities: 🔴 high · 🟡 medium · ⚪ low

---

## 🆕 New ideas

### 🔴 Preparing for Football Manager 2027

**Goal:** when FM27 comes out, the app should be usable quickly. FM25 never
existed and the user skipped FM26, so the jump goes from FM24 to FM27. Agreed
on 2026-10-10.

- **Assumptions:** the 40 attributes stay the same. Once players are imported,
  the rest of the app fits. New roles, positions and tactics remain open.
- **Export:** according to official confirmation, FM27 has no player export in
  the base game. For FM26 there is the BepInEx mod "FM26 Player Export" (CSV +
  HTML via F9). Since FM27 probably still runs on Unity, a quick port is
  realistic. An own mod is plan B if nothing appears a few weeks after release.
- **Role system (biggest risk):** FM26 abolished Defend/Support/Attack. Every
  outfield player has one role **in possession** and one **out of possession**,
  plus separate formations for both phases; according to third-party sources
  more than 70 roles (gone: Mezzala, Trequartista, Enganche; new among others
  Channel Midfielder, Overlapping Centre-Back, Screener, Presser). Our model
  knows **one** role per slot. For FM27 a slot needs two roles.
  - **Rating a slot:** weighted mean of the in-possession DWRS and the
    out-of-possession DWRS. Share **adjustable, default 50 %** (user,
    2026-10-10: both phases equally important, in the sense of "the striker is
    the first defender, the centre-back the first attacker").
  - **Position with a double formation:** the **out-of-possession** position
    (base shape) decides whether a player can play the slot.
  - Affected: DWRS, Best XI, training advisor, signing advice, tactic
    explorer, role editor. Build only once the FM27 role list is settled.
- **Attribute weights without test data:** FM27 starts with the FM24
  categories as an assumption, marked "provisional" in the app. Idea: check
  with many exported players per league and role which attributes go together
  with good average ratings (`Av Rat`). Only indications (good players often
  play in good teams) until community engine tests exist.
- **Implementation in steps:**
  1. ✅ **Version profiles** (v1.4.12): every FM version has its own profile.
     It includes the role file (roles, role weights, position → role, tactics;
     FM24 `definitions.json`, later `definitions_<id>.json`), the attribute
     categories, the category weights in config.ini (FM24
     `[Weights]`/`[GKWeights]`, later `[Weights_<id>]`) and the game folder
     for the import. The save decides the profile (`fm_version`; no entry =
     FM24). New saves are marked with the newest version; selectable on
     creation once there are several. Fixed as soon as the save contains
     players. FM24 behaves unchanged. Adding a new version: see the comment on
     `fmDataVersions()` in `core/Constants.h`.
  2. ✅ **Flexible import** (v1.6.0): CSV import with separator detection,
     column schema per version (`core/ImportSchema`) and a hint on missing
     columns next to the unknown ones.
  3. **Concept two roles per slot:** the concept is in
     [CONCEPT_TWO_ROLES.md](CONCEPT_TWO_ROLES.md) (data format, rating,
     impact, order). It gets built once the FM27 role list is settled.
  4. **For the release:** ✅ import (profile `fm27`, column schema for the
     FM View Exporter's files, see [docs/IMPORT-FM27.md](../docs/IMPORT-FM27.md));
     open: read a real export and check the mapping, take over roles with key
     attributes from the game, check positions. The pitch geometry (slots,
     slot → game position, roles in the tactic editor) lives per version in
     `core/PitchGeometry` (one source); add FM27 there. Since v1.6.0 the
     tactic editor offers only wing-back roles on DML/DMR, as in the game.
     DL/DR now require the position D (L)/D (R); pure wing-backs only play on
     WBL/WBR (DML/DMR).
  5. **Afterwards:** adjust the weights once engine tests exist.

### 🟡 Statistics lab: check attribute weights against match statistics

**Goal:** use real match statistics from a FM export to check which
attributes really lead to good performance, and derive or confirm the DWRS
weights from them. Important for FM27 because there are no engine tests at
first (FM27 plan, step 5).

- **Model case goalkeepers (2026-10-10):** export "Goalkeeper_Analysis.html"
  with 8070 goalkeepers (game year 2043), 335 of them with at least 10
  appearances and detailed statistics. Measure: xGP/90 (expected goals
  prevented). The rating is unsuitable: it correlates −0.91 with goals
  conceded, so it measures the defence. Result: the old goalkeeper DWRS used
  only 6 attributes (r = 0.43); the new scheme from the analysis reaches
  r = 0.58 (v1.4.13).
- **Method:** count ranges as their middle; correlation per attribute, also
  adjusted for the overall level (otherwise everything correlates with
  everything); always test on unseen data (repeated halving) to avoid false
  precision.
- **Outfield players:** harder, because almost every statistic depends on the
  team and tactic. Useful are figures that count against an expected value
  (e.g. goals − xG for finishing). Pick a suitable figure per position group.
- **Possible implementation:** own page: load a statistics export, choose a
  figure, show attribute relationships and the accuracy of the current DWRS,
  apply the weight suggestion as an option.
- **Note:** the app does not import Communication, Eccentricity and Punching
  (not among the 40 attributes). An evaluation would need them.

### ✅ Registration assistant (Premier League + UEFA) — done with v1.4.1

**Goal:** build the registration list so that it is **registrable** and wastes
no places. Reason: with too few HG players, often only 21 players were
registered although talented youth players could have filled the free places.
A registrable all-rounder beats two specialists of whom one cannot be
registered.

- **Rules** (agreed with the user, 2026-09-29; fixed in core per FM version,
  `core/Registration`, FM24 only):
  - **Premier League:** at most 25 registered players, at most 17 of them
    non-home-grown. U21 (cut-off: born on or after 1 Jan of the season start
    year − 21) needs no place. Home-grown = three seasons before the 21st
    birthday at clubs of the association; the PL has no club-trained quota.
  - **UEFA (CL/EL/ECL, same rules):** list A with at most 25 players, at most
    17 of them not locally trained. 4 of the 8 reserved places only for
    club-trained players, so at most 21 without club-trained. At least 2
    goalkeepers. List B (unlimited) = U21 and club-trained (approximation for
    "2 consecutive years at the club", not maintained separately).
  - **Bundesliga:** no effective limit (99 places). Selectable as a league,
    but behaves like "Standard (no restriction)": no league registration list,
    every player eligible. "Standard" applies to every league whose rules are
    not stored yet.
  - Minimum number of goalkeepers in the registration list as a setting
    (default 2). The PL does not require any, but without two it is silly.
- **Data:** FM24 does not export the HG status, so everything is maintained by
  hand. Table `player_registration` (schema v5): `home_grown`, `club_trained`
  (includes HG), `u21` (auto/yes/no), `league_listed`, `uefa_listed`. U21
  automatic: up to 20 yes, from 22 no, at 21 unclear (counts as not U21 to be
  safe, with a hint). Maintained on "Edit player" and via right-click →
  Registration.
- **Algorithm:** pool = first team + second team without Retired.
  `SquadBuilder` over the whole pool and **all favourite tactics** (max. 2)
  gives the ranking starting XI > B team > depth > DWRS. Then fill the quotas
  in this order. Free HG/club-trained places are **always** filled, also with
  youth and second team players. Shows the DWRS cost of the quota (Best XI
  with versus without rules).
- **Implementation in steps:**
  1. ✅ v1.3.24: schema v5, markers (edit + right-click), setting
     "Registration rules" (league, UEFA, minimum goalkeepers).
  2. ✅ v1.3.25: selection in `core/Registration` (`rankPool`, `propose`,
     `quotaCosts`) with tests + page "Registration" (only visible with active
     rules): live proposal, cost of the quota, warnings (free places, U21
     unclear), "Apply as registration list" with comparison.
  3. ✅ v1.3.25 (with step 2): UEFA list A/B in the assistant.
  4. ✅ v1.4.1: Best XI has a competition selection (league, Champions /
     Europa / Conference League and cup: DFB-Pokal or FA Cup / League Cup,
     without registration) and calculates the first team only with the
     eligible players (stored registration list + U21 or list B; registered
     second team players count too). The UEFA list is independent of the
     league, so it also applies with the Bundesliga. Youth/second team and
     loan/sale lists stay with the whole club. On the "Registration" page you
     tick whom you really registered in the game (prefilled: recommendation,
     after saving your own list); goalkeepers and outfield players separately,
     default sorting by position (`positionSortKey`: goal → attack, right
     before left), re-sortable by column header.
- **Later:** La Liga, Serie A, Ligue 1 as further rule sets (research the
  rules first).

### ⚪ Screenshot → instant DWRS (image recognition)

**Goal:** take a screenshot of a FM24 player profile, press a button in the
app → attributes are recognised → instant DWRS calculation for every matching
role, without an HTML export.

- **Trigger:** paste a screenshot from the clipboard (Win+Shift+S →
  button/Ctrl+V). Simpler than a file dialog. Alternatively a file via drag &
  drop.
- **Recognition, options:**
  - Windows' own OCR (`Windows.Media.Ocr`, no extra dependency)
  - Tesseract (heavy dependency)
  - own digit template matching on the fixed FM attribute grid (1–20 on a
    coloured background). Possibly the most robust.
- **Sticking points:**
  - different resolutions, skins and UI zoom
  - masked range values ("12-15") for unscouted players
  - mapping number → attribute name via the label
  - goalkeeper layout
- **Result:** temporary `Player` object → `DwrsEngine::calculate` per role,
  showing the top roles plus the pitch diagram. Optionally "apply to player"
  (matched by name, confirmed as with the manual single update in the
  profile).
- Independent of the rest of the code; can become its own module at any time.

### 🟡 Linux version

**Goal:** the app also runs on Linux. macOS is not planned.

- **Steps:**
  1. ✅ 2026-10-02 (no version change): Linux build in CI. CMake is portable
     (`WIN32`/`app.rc` only on Windows), presets `linux-debug` /
     `linux-release` (Qt path from `QT_ROOT_DIR`), Linux job in `ci.yml` and
     as an independent job in `release.yml`. Builds with GCC 13 without code
     changes, all 16 test suites green.
  2. ✅ 2026-10-17: AppImage in the release workflow next to the Windows
     installer (`linuxdeploy` + Qt plugin, PNG icon, `.desktop` file, start
     test in CI); tested on Linux Mint.
  3. Open: polish after the first real test on Linux: font ("Segoe UI" is
     hard-coded), default import folder (FM24 runs via Steam/Proton, the
     Documents folder lives in the Proton prefix), emojis in headings, button
     "Open in Explorer", README ("Windows 10 / 11 only").

---

## 🟢 Open from the legacy list (still relevant)

- **✅ Gap analysis → transfer shortlist bridge**: done with v1.4.11: tab
  "Signing advice" in Best XI (`core/SigningAdvisor`). Instead of a median
  comparison, the best players of the filtered market (budget, maximum age,
  shortlist only) are added to the squad on trial, and starting XI + B team
  are solved again exactly; positions are ranked by the measured starting XI
  gain, including knock-on effects. Candidates go to the shortlist via
  right-click. The old gap analysis sits collapsed below; its sidebar entry is
  gone.
- **🟡 Signing advice across several tactics.** Signing advice over the
  favourite tactics at once; a signing that improves the starting XI in *all*
  of them is the safest transfer target.
- **🟡 Edit/delete roles and tactics.** Today there are only "New Role" and
  "New Tactic". Caution: deleting/changing a role creates the same stale DWRS
  values as removing a role assignment (backlog **#23**) and needs a
  recalculation.
- **⚪ Export/import settings** (config.ini + definitions.json + logos/flags as
  ZIP) for backup and sharing.
- **⚪ Shortlist management.** The shortlist flag exists (right-click menu,
  squad matrix). Open: an own view with notes and scout rating, or bundle
  everything in the gap → shortlist bridge.
- **⚪ Finance overview.** Wage total, market value vs. age (scatter). The data
  (`wageRaw`, `transferValue`) is there.
- **⚪ Team versus team comparison.** Two squads side by side via
  `SquadBuilder`.
- **⚪ "Manager summary".** A one-sentence summary of Best XI / gap analysis.
- **⚪ CSV export on further tables** (role analysis, gap analysis, transfers).
  Today only the squad matrix.
- **⚪ Move stat categories into `definitions.json`**
  (`globalStatCategories`/`gkStatCategories` are hard-wired in
  `Constants.cpp`). Only together with the definitions cache (backlog **#30**)
  and careful DWRS regression tests.
- **⚪ Icons/emojis** more consistent in headings and buttons (cosmetic).

## ✅ Already done in the C++ port

- **Browser navigation (v1.3.20):** back / forward / home in the header of
  every page, plus Alt+← / Alt+→ / Alt+Home and the mouse side buttons; the
  history also remembers the mode (club/national) and the player of the
  profile page.

Global player search (with umlaut folding), click/double-click/right-click on
player names → profile/comparison/edit (in-context editing), pros & cons,
profile page, youth/second team, tabs, conditional formatting, extended search
(squad matrix filters: nationality, minimum age, free agents, talent,
personality), CSV export (squad matrix), separation of logic and UI (`fmcore`
without widgets, transfer logic in `SquadBuilder`), test harness (ctest, 16
suites), personality system, club header/theme/logo, tactic explorer.

## ❌ Rejected / principles

- **Preferred foot is no side signal.** Only the manual `preferredSide`
  counts, for any side-related logic.
