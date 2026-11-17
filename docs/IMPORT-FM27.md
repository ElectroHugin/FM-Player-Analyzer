# Import from Football Manager 2027

FM27 no longer has a built-in player export. The data comes from the
[FM View Exporter](https://github.com/ElectroHugin/FM-View-Exporter), a BepInEx
plugin that saves the player list currently on screen as CSV and HTML with
**F9**. Both programs are matched to each other; this file describes the
contract between them.

## Steps

1. Install the FM View Exporter (see its README).
2. Create the **Analyzer** view in the game
   ([guide and column list](https://github.com/ElectroHugin/FM-View-Exporter/blob/main/docs/ANSICHT-ANALYZER-FM27.md)).
   Other views work too, as long as `Unique ID` and `Player` are included.
3. Open the squad, player search or scouting, press **F9**. The file lands in
   `Documents\Sports Interactive\FM View Exporter`.
4. In the Analyzer, create a save with the version **Football Manager 2027**
   (Settings → Database → New; new saves are FM27 by default).
5. Dashboard → *Import new player data* → choose the CSV or HTML file. The file
   dialog opens the Exporter's folder directly.

The version belongs to the save. An FM24 save rejects FM27 files ("required
column UID missing") because FM27 names the columns differently.

The Exporter writes the column headers the game shows. The Analyzer currently
reads the headers and values of the **English** game interface only; for other
game languages, read the save directly (below).

## Reading the save directly (from 2.1.0)

Instead of an export, the save itself can be chosen
(`Documents\Sports Interactive\Football Manager 27\games\*.fm`). The Analyzer
only reads the file and never writes to it. Preferably use a freshly saved file,
not one the game is still writing. This works for every game language.

What is taken over is controlled by the setting **Attribute masking as in the
game** (Settings → Import, per save, on by default):

| Players | Masking on | Masking off |
| --- | --- | --- |
| own squad (including the second team) | exact values | exact values |
| world reputation 8,000 or more | exact values | exact values |
| own scouting, knowledge 78 % or more | exact values | exact values |
| own scouting, 56 to 77 % | partly ranges (made by the Analyzer) | exact values |
| prior knowledge, below 56 % | all attributes as ranges (2 to 4 points) | exact values |
| not scouted, with a club | all attributes as ranges (4 to 7 points) | exact values |
| not scouted, without a club | not taken over | not taken over |

- The knowledge level is in the save (section `fog_of_war`). The game's ranges
  are not; the Analyzer rolls its own, fixed once per player and attribute. The
  true value lies somewhere inside, not by rule in the middle. More knowledge
  means fewer and narrower ranges. Rather more players than too few: even with
  masking, nobody with a club is missing, only the precision drops.
- Hidden values (potential, ability, consistency, injury proneness, important
  matches, dirtiness, versatility) are never read into the database.
- Personality: unambiguous names are derived from the personality values read.
  With masking this applies to the own club and fully scouted players (knowledge
  100 %); without masking to every player taken over. Unknown or ambiguous names
  stay empty and do not overwrite an earlier export value. The raw values are not
  stored in the database.
- Media handling is not taken from the save yet. Earlier export values are kept.
  Nations are known for the 152 countries that appeared in exports.
- Affiliated clubs and opponents the game shows without a scout report are still
  missing.
- A FM patch can change the file format. The import then stops with a message
  instead of taking over wrong values.
- The format follows [fmsave](https://github.com/rhiever/fmsave) (MIT), see
  `THIRD_PARTY_NOTICES.md`. Tool for comparison: `fm27savedump`.

## Column mapping

FM24 exported abbreviations (`Acc`, `UID`, ...). The Exporter instead writes
exactly the column headers FM27 shows (English game interface). The column
order does not matter; the Analyzer finds columns by name. Source in the code:
`desktop/src/core/ImportSchema.cpp` (`fm27AttributeMapping()`).

### Required

| Column in FM27 | Field in the Analyzer | FM24 header |
| --- | --- | --- |
| `Unique ID` | Unique ID | `UID` |
| `Player` (player database: `Name`) | Name | `Name` |

### Basic data

| Column in FM27 | Field in the Analyzer | FM24 header | Note |
| --- | --- | --- | --- |
| `Position` | Position | `Position` | |
| `Age` | Age | `Age` | |
| `Club` | Club | `Club` | |
| `Nation` | Nationality | `Nat` | code (`GER`) or full name (`Germany`), the code is stored; with two `Nation` columns the first counts |
| `2nd Nat` | Second Nationality | `2nd Nat` | full name, the code is stored |
| `Height` | Height | `Height` | `193 cm` |
| `Preferred Foot` | Preferred Foot | `Preferred Foot` | |
| `Left Foot` | Left Foot | `Left Foot` | |
| `Right Foot` | Right Foot | `Right Foot` | |
| `Personality` | Personality | `Personality` | |
| `Media Handling` | Media Handling | `Media Handling` | |
| `Playing Time` | Agreed Playing Time | `Agreed Playing Time` | |
| `Wage` | Wage | `Wage` | |
| `Transfer Value` | Transfer Value | `Transfer Value` | range → lower bound |
| `Rating` | Average Rating | `Av Rat` | `-` = no matches |
| `Inf` | Information | `Inf` | read but not evaluated |

### Attributes (40)

| Column in FM27 | Field in the Analyzer | FM24 header |
| --- | --- | --- |
| `Aerial Reach` | Aerial Reach | `Aer` |
| `Command Of Area` | Command of Area | `Cmd` |
| `Handling` | Handling | `Han` |
| `Kicking` | Kicking | `Kic` |
| `One On Ones` | One vs One | `1v1` |
| `Reflexes` | Reflexes | `Ref` |
| `Rushing Out (Tendency)` | Rushing Out (Tendency) | `TRO` |
| `Throwing` | Throwing | `Thr` |
| `Aggression` | Aggression | `Agg` |
| `Anticipation` | Anticipation | `Ant` |
| `Bravery` | Bravery | `Bra` |
| `Composure` | Composure | `Cmp` |
| `Concentration` | Concentration | `Cnt` |
| `Decisions` | Decisions | `Dec` |
| `Determination` | Determination | `Det` |
| `Flair` | Flair | `Fla` |
| `Leadership` | Leadership | `Ldr` |
| `Off The Ball` | Off the Ball | `OtB` |
| `Positioning` | Positioning | `Pos` |
| `Team Work` | Teamwork | `Tea` |
| `Vision` | Vision | `Vis` |
| `Work Rate` | Work Rate | `Wor` |
| `Acceleration` | Acceleration | `Acc` |
| `Agility` | Agility | `Agi` |
| `Balance` | Balance | `Bal` |
| `Jumping Reach` | Jumping Reach | `Jum` |
| `Pace` | Pace | `Pac` |
| `Stamina` | Stamina | `Sta` |
| `Strength` | Strength | `Str` |
| `Corners` | Corners | `Cor` |
| `Crossing` | Crossing | `Cro` |
| `Dribbling` | Dribbling | `Dri` |
| `Finishing` | Finishing | `Fin` |
| `First Touch` | First Touch | `Fir` |
| `Heading` | Heading | `Hea` |
| `Long Shots` | Long Shots | `Lon` |
| `Marking` | Marking | `Mar` |
| `Passing` | Passing | `Pas` |
| `Tackling` | Tackling | `Tck` |
| `Technique` | Technique | `Tec` |

Scouted attributes come as a range (`12-15`) and are stored as lower and upper
bound, as with FM24.

Both CSV styles of the Exporter work: *English* (comma, `7.05`) and *German*
(semicolon, `7,05`). The Analyzer detects the separator itself and reads the
rating with a dot or a comma.

### Not read

The Analyzer knows every other column FM27 offers and skips it silently, among
them `Punching`, `Natural Fitness`, `Free Kick Taking`, `Long Throws` and
`Penalty Taking` (not among the rated attributes, as with FM24) as well as
`Rec.` / `Recommendation` (scout recommendation from A+ to F, first column of
the player database view). The import only reports headers as *unknown column*
when they are in neither list, e.g. columns a patch renamed.

## What is (still) different in FM27

- **Missing columns overwrite nothing:** if a file lacks a column, the Analyzer
  keeps the value stored so far. Example: the view for *Recruitment > Player
  Database* offers no `Personality`; the personality from a squad import is
  kept, new players from the database have none at first.
- **No registration:** FM27 has no `Reg` column. The registration assistant is
  therefore off for FM27 saves.
- **Roles and tactics:** the 79 FM27 roles (40 in possession, 39 out of
  possession) with key and preferable attributes from the game, generated from
  `desktop/data/fm27/` by `desktop/scripts/make_definitions_fm27.py`
  (`definitions_fm27.json`). A FM27 tactic has two independent line-ups (in
  possession, out of possession) without fixed pairs; the Best XI picks the
  players and a place for each in both line-ups via min-cost flow
  (`SquadBuilder::calculateTwoPhaseSquad`). In addition to FM24, the Analyzer
  reads the goalkeeping attributes `Communication` and `Eccentricity`; FM24
  saves do not rate them.
- **Weights:** attribute categories and weights are provisionally those of FM24
  (own section `[Weights_fm27]` in `config.ini`).

## Keeping both programs in sync

- The Exporter's column catalogue is authoritative for column names:
  [`docs/columns/fm27.csv`](https://github.com/ElectroHugin/FM-View-Exporter/blob/main/docs/columns/fm27.csv),
  column `analyzer_field`. Every row with an entry there is in
  `fm27AttributeMapping()`, all others in `fm27IgnoredExportColumns()`.
- If a FM27 patch renames a column, the import shows it as unknown. Then adjust
  both places: `fm27.csv` in the Exporter and `ImportSchema.cpp` here.
- The test `test_csvimport` (`fm27ExporterCsv`, `fm27MappingComplete`) checks a
  file in the Exporter's format and that all 40 rated attributes are reachable.

## Personality: limits and classification (from 2.2.3)

The reconstruction uses the published thresholds and priorities of the
[FM reference analysis](https://github.com/Napas/fm-mcp/blob/main/internal/fm/personality.go)
for FM26.3.2. A complete independent confirmation of every name in FM27 is still
pending. The own parser knows neither the extra flag for negative personality
names nor the club attachment for "Devoted". As soon as one of them would be
needed for the choice, no name is guessed. Invalid values give no name either.
CSV/HTML can still provide the name the game shows directly.

Under Settings → Personalities, good/neutral/bad can be changed separately for
FM24 and FM27. The classification lives in the save's version-specific
definitions document; unchanged, the previous FM24 defaults apply. Talent score,
filters and analysis read the same classification.
