# Changelog

## Unreleased

Test build 2.3.0-test.2:

- DWRS now uses rating profiles. Each save calculates with exactly one profile, shown in Settings → DWRS Rating.
- New FM27 profile "DWRS FM27 · harvestgreen22", based on harvestgreen22's attribute measurements (thank you!). Provisional: values can change after their re-test with the full game.
- New FM27 saves use it automatically. Existing saves keep their numbers and ask once whether to switch.
- FM24 keeps the familiar calculation ("DWRS Classic"). Your own changed weights become a personal profile.
- Optional role share: part of the rating comes from the role's key and preferable attributes. On by default (33 %) for the FM27 profile, explained in the editor.
- Missing attributes no longer count as zero: they are left out. Below 70 % known weight there is no rating.
- Profiles can be edited, saved as new, exported and imported.
- DWRS shows one decimal for curve profiles; Classic stays whole numbers.
- FM27 goalkeepers get their own weights from our own analysis of FM27 match statistics (not from harvestgreen22), plus a 20 % goalkeeper role share for the attributes that set a goalkeeper role apart.
- New settings page "DWRS colouring": the original rainbow stays the default; more colour schemes (e.g. Rarity), own colours, thresholds and soft borders, saved as your own schemes.
- No web links inside the app; sources are linked in the README.
- Saving ratings after a recalculation is faster and the progress dialog shows the saving step.
- DWRS development chart: the hover panel stays open, and the in-game date replaces "−1/−2" on the axis and in the panel.
- Overview: colour positional strength by level or by comparison with the rest of the squad.
- Switching the profile recalculates once and marks it in the history; development does not count it as progress.
- Player databases migrate to schema 18; 2.2.3 cannot open the upgraded database. A backup is made first.

Added for 2.3.0-test.4:

- FM27 save import with attribute masking: players you have not scouted (enough) are no longer left out. Everyone with a club is read in; prior knowledge shows every attribute as a range of 2-4, no scouting as a range of 4-7. Thanks to the feedback in issue #26.
- English UI: the default colour preset is called "Club-neutral (default)".

Added for 2.3.0-test.3:

- FM27 save import: when the game renames clubs (e.g. after a data update), your club and second team follow automatically instead of showing an empty squad.
- Training: your own training profile per save. Choose which attributes belong to which band, the age windows, a weight per band, add or remove bands, and import/export the profile as JSON. Today's values are the standard.
- Personalities: a save only shows and edits the classification of its own FM version.
- Overview, squad comparison: grey for the squad median instead of dark cards, a colour legend and the distance in the tooltip.
- Settings: opened explanations are no longer cut off.
- Leagues without a squad list (e.g. Bundesliga): mark a player as "Not registered" in the context menu. Best XI and the Overview then leave him out. Squad Registration lists the squad with a "Registered" tick for these leagues.
- FM27: reading the save (.fm) is now the main way in. New save asks for the save file first (exports stay optional) and can follow it live; the welcome assistant offers following the save; the Overview shows one line on whether the save is followed, and a large export suggests the save once (it only returns if you tick "Show again next time").
- Large saves: the basis of each rating (profile, game date, attributes) is stored once per player instead of once per role. Saving ratings after an import or recalculation is about twice as fast, the database about two thirds smaller, and the progress dialog shows the saving step instead of sitting at 98 %. Saves from earlier 2.3.0 test builds are converted once when opened.

## 2.2.3 – 2026-11-09

**Best XI and tactics (FM27)**
- New tactical views: Combined, In Possession, Out of Possession or Both, on Best XI and Overview.
- Starting XI and B-Team side by side when there is room. Player portraits also in Combined.
- Smarter position assignment across both phases. Your central-side preferences come first, pinned players stay put.
- Manual swaps now move a player in both phases.
- Missing starter: see who comes in and who moves.
- Overview: positional strength shows each starter's rating and what the XI loses without him.

**FM27 save import**
- Natural positions and personality are imported for fully known players. Your manual choices win.
- A damaged scouting block is ignored completely.
- An older save imported later no longer overwrites newer player history.

**Squad Registration (LaLiga)**
- Mark players where the minimum wage does not apply (e.g. loans).
- EU players aged 18 or younger need no slot (U18 EU) instead of the U21 rule.
- Suggested list with a wage budget is much faster.

**Settings and other**
- Personality categories can be edited separately for FM24 and FM27.
- Talent Score handles hidden attributes better.
- New save with copied settings no longer takes over the old live save, logo or flag.
- Faster save switching and scouting filters.
- Fixed a crash when switching saves with the Squad Matrix open.
- Databases are upgraded. Back them up before going back to an older version.

## 2.2.2 – 2026-11-07

- Squad Registration: wage budget and minimum wage now only appear for LaLiga. Other leagues have no such rule.
- Squad Registration: mark players as non-EU. Only shown where the league limits them (LaLiga 3, Swiss Super League 10). The suggested list respects the limit.
- Squad Registration: mark a player as "cannot be registered", separately for league and European competitions. He is never suggested.
- Best XI (Swiss Super League): at most 5 non-EU players in the starting XI, with a warning if there are more.
- Squad Registration: for leagues without a squad list (Bundesliga, Ligue 1, Austria) the page explains why there is nothing to suggest. Ligue 1 also mentions its non-EU limit.
- Release notes are now in English.
- Settings redesigned: a section list on the left, short pages with clear cards, explanations folded away. All options are still there.
- In English the Squad List page is now called Squad Registration.

## 2.2.1 – 2026-11-06

- Squad Registration (LaLiga): set a wage budget and a minimum wage (per week, month or year).
- The suggested list never picks players below the minimum wage and stays within the budget.
- With a budget, players with promised playing time come first. Your starting XI is never weakened.
- Counters, warnings and the wage column show where your selected list stands.

## 2.2.0 – 2026-11-06

- In-game date is saved with every import.
- Player history: monthly snapshots from FM27 saves, shown in the player profile.
- By default only your squad, shortlist and scouted players are tracked. Switch on "all players" if you want more.
- Season start can be set per save (default 1 July). Find it under Settings → Club.
- Overview: new tile with the in-game date and season.
- New player profile header: portrait (or club crest), club, market value, wage and average rating with change since last month.
- Player profile shows left and right foot strength.
- Comparison: no longer stays empty when a player's first role isn't in the tactic.

## 2.1.0 – 2026-11-06

- Read FM27 save files directly (read-only), with attributes hidden just like in the game.
- Follow a running save live.
- Save profiles: settings, tactics and colours are kept per save.
- Welcome assistant after your first import.
- Tactic editor, plus two example tactics: Control Press and Counter Surge.
- Squad depth: see who replaces whom when a player is out.
- Bridge search: find players who cover two positions.
- Club header with crest, colour presets and an optional graphics folder for portraits and crests.
- Reworked start screen.
- Role lists: switch between in possession and out of possession.
- Scouting: choose which columns to show.
- Squad menu in the player profile, right-click menu on transfer targets.
- Fixed: the app no longer freezes after importing.
- Many smaller fixes.

## 2.0.4 – 2026-11-02

- FM27 roles are now complete everywhere: in and out of possession (scouting, matrix, comparison, overview, training).
- Roles are labelled the same way everywhere: -IP (in possession) and -OOP (out of possession).
- Player profile: all roles as tiles, with a filter for in / out of possession.
- Comparison: shows each player's best four roles, the rest in a tooltip.
- Transfer advice and gap analysis respect your manual line-up changes.

## 2.0.3 – 2026-11-01

- Default tactic: DLF instead of CFD up front. New FM27 tactic file included.

## 2.0.2 – 2026-11-01

- Default tactic: DLP instead of BBP on the left of defensive midfield. New FM27 tactic file included.

## 2.0.1 – 2026-11-01

- New default tactic: 4-2-3-1 Bavarian Blitz.
- The default tactic is included as an FM27 tactic file. Install it from the installer or from Settings.

## 2.0.0 – 2026-11-01

- Football Manager 2027 support.
- Import FM27 data via FM View Exporter.
- Two line-ups per tactic: in possession and out of possession.
- New pitch view and tactic editor.
- Squad registration rules for 11 FM27 leagues.
- FM27 views included.
- New look, new logo.

## 1.6.2 – 2026-10-21

- New About dialog (Help → About) with version, licence and support info.
- "Copy system info" button for bug reports.
- Version shown at the bottom of the sidebar.

## 1.6.1 – 2026-10-21

- Player tables: long text columns (club, position, personality) are shortened so role columns stay visible. Full text in the tooltip.

## 1.6.0 – 2026-10-18

- Import CSV files, not just HTML.
- Clear message when columns are missing in an import.
- Linux version (AppImage).
- Best XI: DL/DR now need position D (L)/D (R), like in the game. Pure wing-backs only play WBL/WBR.

## 1.5.0 – 2026-10-11

- New sidebar: pages grouped, with tabs on top. Fewer entries, easier to find.

## 1.4.x – 2026-09-29 to 2026-10-11

- Squad registration assistant: builds a full, rule-compliant squad list for the Premier League and UEFA (List A and B), Bundesliga selectable.
- Tick who you actually registered. Best XI then uses that list per competition.
- U21 status ends at 22. Changes since your last save are shown.
- Best XI and B team are now provably optimal (Hungarian method).
- Long-term injured players can be taken out of the Best XI. The squad rebalances.
- Transfer advice: gap analysis shows where buying really pays off.
- Squad matrix: tabs for Scouting and My squad, attribute thresholds, difference to your best own player.
- DWRS development: based on imports instead of dates, ranking with history, role taken from your favourite tactic's Best XI.
- Goalkeeper DWRS fixed and re-weighted from real match statistics.
- Only full exports age players. Outdated players are hidden.
- Name columns sort by surname, like in the game.
- Charts readable in night mode.
- Tables no longer cut off the longest text in a column.
- Save profiles per FM version (preparation for FM27).

## 1.3.x – 2026-08-03 to 2026-09-29

- English translation for the free agent and nationality filters.
- National team mode: upload national flags.
- Nomination assistant for your national squad.
- Training advice per player and a training plan page, based on training methods.
- Data freshness tracking. Retired players are detected and left out of all suggestions.
- Back, forward and home navigation on every page.
- Much faster import and DWRS recalculation. The app freezes less.
- Search in tables ignores umlauts. CSV export works with German Excel. £ and $ market values supported.
- Home-grown, club-grown and U21 status can be edited, with a setting for the rule set.
