# Changelog

## Unreleased

Test build 2.3.0-test.1:

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
- Overview: colour positional strength by level or by comparison with the rest of the squad.
- Switching the profile recalculates once and marks it in the history; development does not count it as progress.
- Player databases migrate to schema 18; 2.2.3 cannot open the upgraded database. A backup is made first.

## 2.2.3 – 2026-11-09

- Manual player swaps now exchange both IP and OOP places, keeping each phase's slot roles.
- IP and OOP show A/B teams side by side like Combined; Both retains one team's two phases.
- Combined role headers have independent, consistent role colours. Positional Strength uses the DWRS colour scale.
- Lineup explanations sit below their own setting. Appearance saves avoid analysis reloads and duplicate stylesheet work.
- FM27 save import reconstructs unambiguous personality names for the own club / fully scouted players (all imported players with masking off); ambiguous names preserve earlier reports.
- Personality categories are editable separately for FM24 and FM27 in Settings, with the existing FM24 defaults.
- Talent Score keeps its familiar weights, uses midpoint estimates for masked mental attributes and neutral values for missing ones, and explains its limits as a development proxy.
- FM27: keep the selected eleven and improve their assignments across both phases: manual central-side preferences first, then shorter total paths. The configurable allowance defaults to 0.1 DWRS points for the whole team, relative to the original assignment, and also protects the weighted selection score. Pinned positions stay fixed.
- Central side preferences now work across different roles when the assignments are equally suitable. Preferred foot is not used.
- FM27 saves: import natural positions for fully known players. Manual overrides, including an explicitly empty selection, take priority and survive imports. The existing natural-position bonus now checks the actual slot, including the correct wing; its default multiplier remains 1.00.
- Best XI Combined: show Starting XI and B-Team side by side when there is room, with the selected team on the left. Keep the view controls visible while scrolling, adapt pitch height and use smaller wheel steps. Player portraits now also appear in Combined.
- Player databases migrate to schema 16 to preserve imported positions separately from manual choices; older builds cannot open the upgraded database.
- Squad Registration (LaLiga): mark a player with "Minimum wage does not apply (e.g. loan)". He can then be registered although his wage is below the minimum.
- Squad Registration (LaLiga): EU players aged 18 or younger on 1 January need no slot ("U18 EU"), instead of the U21 rule that LaLiga does not have. The in-game date decides unclear ages, or you set them by hand. Players without a recognised EU nationality stay unclear until you decide.
- Squad Registration (LaLiga): the suggested list with a wage budget is calculated much faster for big squads.
- New save with copied settings: the live-tracked FM27 save, the club logo and the flag are no longer copied, so the new save never imports the old career.
- Player history: an older save imported later no longer replaces a newer state of the same month.
- FM27 saves: a damaged scouting block is ignored completely instead of partly.
- A database from a newer app version is no longer opened (it could lose data).
- Best XI and Overview (FM27): choose the tactical view: Combined, In Possession, Out of Possession or Both. Combined shows one pitch with each player halfway between his two places and both roles on the card.
- Best XI: the table for a missing starter now shows who comes on, who takes over the vacated places, other moves and the change for the whole XI. Equal plans prefer fewer moves. The non-EU starting limit is kept.
- Overview: the positional strength shows each starter's rating and how much the XI loses if he is out, instead of an average over starters, B-team and reserves. Hover a card for who would replace him.
- Best XI: the list of extra depth is now called "Reserve options per role".
- Switching saves is faster, especially for big databases: only the page you are looking at is rebuilt, and ratings load in one go.
- Scouting: changing age, value, DWRS range, nationality or free agents updates the list much faster. Sliders wait until you let go.
- Fixed a crash when switching saves while the Squad Matrix was open (big databases).

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
