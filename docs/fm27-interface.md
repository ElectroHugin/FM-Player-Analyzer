# Interface FM View Exporter ↔ FM Player Analyzer (FM27)

Shared agreement between both projects. As of 31 Oct 2026 (Exporter 1.0.0/1.0.1).
On `main` since Analyzer version 2.0.0.

## Who is authoritative for what

| Topic | Authoritative source |
| --- | --- |
| Column headers, meaning, Analyzer field | Exporter: `docs/columns/fm27.csv` |
| Menu entries for building views | Exporter: `docs/columns/fm27-menu.csv` |
| Ready-made views (`.fmf`) and guide | Exporter: `views/fm27/`, `docs/VIEW-ANALYZER-FM27.md` |
| How the Analyzer reads the columns (required, formats, aliases) | Analyzer: `docs/IMPORT-FM27.md`, `desktop/src/core/ImportSchema.cpp` |
| Presets, ground rules, open questions | **this file** |

If something changes in one place, the other is updated in the same step and
noted here under "Changes".

## Ground rules for the export

1. **Column headers exactly as in the game** (English interface). No own
   abbreviations. Headers can differ per screen (`Player`/`Name`,
   `Rec.`/`Recommendation`); the Analyzer keeps aliases for them.
2. **`Unique ID` and player name in every export.** `Unique ID` is the key.
3. **Values as displayed**, not converted: ranges `12-15`, unknown `-`, units
   (`193 cm`, `€71M - €95M`, `10.4km`, `81%`), ratings `7.12`.
4. **CSV:** UTF-8 with BOM, style `English` (comma separates, dot is the decimal
   separator). `Export.CsvStyle = German` (decimal comma) is not recommended for
   the Analyzer.
5. **HTML:** one `<table>` with a `<th>` header row, screen in the title.
6. **Duplicate column headers** (`Nation`, `Expires`, `Attacking - Tactical`,
   `Defending - Technical`): the Analyzer uses the first column.
7. **At most about 250 columns per view.** FM27 only fills the first 255
   columns, in the game itself too. Put important columns first.
8. **Stars** (`Ability`, `Potential`, `Role Ability`): the count as it looks
   filled in the game, yellow and grey together (from Exporter 1.0.1).
   `Unknown` if the game shows none.
9. **File names:** `squad_…`, `player-database_…`, `shortlist_…` plus a time
   stamp; prefix, time stamp and format (CSV/HTML/both) are configurable. The
   screen is always in the name. The FM version is still missing (proposal:
   `fm27_` as prefix).

## Presets

A preset is a **saved view in the game**: the Exporter only reads the columns
the current view shows. The Analyzer defines which columns a preset needs,
Edmund builds the view from it, and the Exporter ships it as `.fmf`. FM27 does
not share views between screens, so there is one file per screen.

### Done (31 Oct 2026)

| Screen | View | Export file |
| --- | --- | --- |
| Squad | `FM27-PlayerAnalyzer-Squad-Standard.fmf` | `squad_…` |
| Recruitment > Player Database | `FM27-PlayerAnalyzer-PlayerDatabase-Standard.fmf` | `player-database_…` |
| Recruitment > Shortlists | `FM27-PlayerAnalyzer-Shortlist-Standard.fmf` | `shortlist_…` |

All three contain the "Analyzer" preset.

### Analyzer (daily import)

`Unique ID`, `Player`, `Position`, `Best Pos`, `Age`, `Club`, `Nation`, `2nd Nat`, `Height`,
`Preferred Foot`, `Left Foot`, `Right Foot`, `Personality`, `Media Handling`, `Playing Time`,
`Wage`, `Transfer Value`, `Rating`, `Inf`, `Best Role`, `Ability`, `Potential`, plus all 47 attributes:

- Goalkeeping: `Aerial Reach`, `Command Of Area`, `Communication`, `Eccentricity`, `Handling`,
  `Kicking`, `One On Ones`, `Punching`, `Reflexes`, `Rushing Out (Tendency)`, `Throwing`
- Mental: `Aggression`, `Anticipation`, `Bravery`, `Composure`, `Concentration`, `Decisions`,
  `Determination`, `Flair`, `Leadership`, `Off The Ball`, `Positioning`, `Team Work`, `Vision`, `Work Rate`
- Physical: `Acceleration`, `Agility`, `Balance`, `Jumping Reach`, `Natural Fitness`, `Pace`, `Stamina`, `Strength`
- Technical: `Crossing`, `Dribbling`, `Finishing`, `First Touch`, `Heading`, `Long Shots`, `Marking`,
  `Passing`, `Tackling`, `Technique`
- Set pieces: `Corners`, `Free Kick Taking`, `Long Throws`, `Penalty Taking`

(69 columns. Player Database and Shortlist have no `Personality` but `Rec.` or
`Recommendation` and `Player's Interest`; the player column is called `Name`
there.)

### Analyzer full (signing advice, talents)

Analyzer + `Expires`, `Contract Start Date`, `Minimum Fee Release Clause`, `Asking Price`,
`Transfer Status`, `Loan Status`, `Home-Grown Status`, `DoB`, `Pros`, `Cons`, `Style`,
`Media Description`, `Int Apps`, `Int Gls`.

### Statistics (statistics lab)

`Unique ID`, `Player`, `Position`, `Best Pos`, `Age`, `Minutes`, `Apps`, `Starts`,
`Average Rating Club`, `Goals per 90 minutes`, `Asts/90`, `xG/90`, `NP-xG/90`, `xA/90`,
`Shot/90`, `ShT/90`, `KP/90`, `OP-KP/90`, `Ch C/90`, `Pr passes/90`, `Ps A/90`, `Ps C/90`, `Pas %`,
`Drb/90`, `Cr C/90`, `OP-Crs C/90`, `Aer A/90`, `Hdrs W/90`, `Hdr %`, `K Hdrs/90`, `Tck/90`,
`K Tck/90`, `Tck R`, `Int/90`, `Clr/90`, `Blk/90`, `Shts Blckd/90`, `Poss Won/90`, `Poss Lost/90`,
`Pres A/90`, `Pres C/90`, `Sprints/90`, `Dist/90`, `Fouls`, `Yel`, `Red`,
goalkeepers additionally: `Con/90`, `Cln/90`, `Saves/90`, `Sv %`, `xSv %`, `xGP/90`.

### Everything

All available columns, split across two views because of the 255-column limit
(troubleshooting, new patch).

## Test results

| Date | Exports | Result with PR #4 |
| --- | --- | --- |
| 30 Oct | Squad, full export (375 columns) | no errors; columns from no. 256 empty (game limit) |
| 30 Oct | old player search view | position with appended role → fixed in 1.0.0 |
| 31 Oct | 1.0.0: Squad (43), Player Database (235), Shortlist (6), each CSV + HTML | all without errors, no unknown columns, ranges and `-` correct |
| 31 Oct | Chris Afanou: Potential 1 yellow + 1 grey star | exported as `1.0` → fixed in 1.0.1 (`2.0`) |

Statistics columns have only been seen at the start of a season (all 0) so far.
An export after a few match days will follow.

## Open questions

- **Stars, certain share:** the Analyzer does not need it for now; DWRS does the
  rating. If it does later, a range `2.0-3.0` as with attributes would be
  possible.
- **Role suitability:** `Role Ability` and `Role Ability Out Of Possession` were
  empty (probably no role selected), `Player Role Suitability` and
  `Position/Role` were beyond column 255. Next step: a slim view with these
  columns and a selected role.
- **In/out of possession:** the export shows the state of the toggle when F9 is
  pressed. Whether headers or only values change is open. Next step: one export
  in each position.
- **Statistics export** with real values (see above).
- **Game language:** whether the exporter writes headers in the game's
  language is open; the Analyzer currently reads English headers only.

## How changes work

- Changes to this file: pull request **against the branch
  `claude/project-thread-1hrepb`**, not against `main`. Edmund decides on the
  merge.
- Changes to column names: first `docs/columns/fm27.csv` in the Exporter, then
  `ImportSchema.cpp` and `docs/IMPORT-FM27.md` here, with an entry under
  "Changes".
- Large example exports and screenshots do not belong in the repository but in
  the project folder.

## Changes

- 30 Oct 2026: first version, presets with column lists, findings from the full export and player search (Analyzer project).
- 30 Oct 2026: answers to the open questions, 255-column limit, views (.fmf) as presets, menu catalogue (Exporter project, PR #5).
- 31 Oct 2026: ready-made views per screen, shortlist columns, file names (Exporter project, PR #5).
- 31 Oct 2026: test with Exporter 1.0.0, star counting (1.0.1), note tidied up and brought up to date (Analyzer project).
- 17 Nov 2026: translated to English, open question on the game language (Analyzer project).
