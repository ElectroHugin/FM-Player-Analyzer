# Import aus Football Manager 2027

FM27 hat keinen eingebauten Spielerexport mehr. Die Daten kommen vom
[FM View Exporter](https://github.com/ElectroHugin/FM-View-Exporter), einem
BepInEx-Plugin, das die gerade angezeigte Spielerliste per **F9** als CSV und HTML
speichert. Beide Programme sind aufeinander abgestimmt; diese Datei beschreibt den
Vertrag zwischen ihnen.

## Ablauf

1. FM View Exporter installieren (siehe dessen README).
2. Im Spiel die Ansicht **Analyzer** anlegen
   ([Anleitung und Spaltenliste](https://github.com/ElectroHugin/FM-View-Exporter/blob/main/docs/ANSICHT-ANALYZER-FM27.md)).
   Andere Ansichten gehen auch, solange `Unique ID` und `Player` dabei sind.
3. Kader, Spielersuche oder Scouting öffnen, **F9** drücken. Die Datei landet in
   `Dokumente\Sports Interactive\FM View Exporter`.
4. Im Analyzer einen Spielstand mit der Version **Football Manager 2027** anlegen
   (Einstellungen → Datenbank → Neu; neue Spielstände sind standardmäßig FM27).
5. Dashboard → *Neue Spielerdaten importieren* → CSV- oder HTML-Datei wählen. Der
   Dateidialog öffnet direkt den Ordner des Exporters.

Die Version hängt am Spielstand. Ein FM24-Spielstand lehnt FM27-Dateien ab
(„Pflichtspalte UID fehlt“), weil FM27 die Spalten anders nennt.

## Spaltenzuordnung

FM24 exportierte Kürzel (`Acc`, `UID`, ...). Der Exporter schreibt dagegen genau die
Spaltenköpfe, die FM27 zeigt (englische Spieloberfläche). Die Reihenfolge der Spalten
ist egal, der Analyzer sucht sie über den Namen. Quelle im Code:
`desktop/src/core/ImportSchema.cpp` (`fm27AttributeMapping()`).

### Pflicht

| Spalte in FM27 | Feld im Analyzer | FM24-Kopf |
| --- | --- | --- |
| `Unique ID` | Unique ID | `UID` |
| `Player` (Spielerdatenbank: `Name`) | Name | `Name` |

### Stammdaten

| Spalte in FM27 | Feld im Analyzer | FM24-Kopf | Hinweis |
| --- | --- | --- | --- |
| `Position` | Position | `Position` | |
| `Age` | Age | `Age` | |
| `Club` | Club | `Club` | |
| `Nation` | Nationality | `Nat` | Kürzel (`GER`) oder voller Name (`Germany`), gespeichert wird das Kürzel; bei zwei `Nation`-Spalten zählt die erste |
| `2nd Nat` | Second Nationality | `2nd Nat` | voller Name, gespeichert wird das Kürzel |
| `Height` | Height | `Height` | `193 cm` |
| `Preferred Foot` | Preferred Foot | `Preferred Foot` | |
| `Left Foot` | Left Foot | `Left Foot` | |
| `Right Foot` | Right Foot | `Right Foot` | |
| `Personality` | Personality | `Personality` | |
| `Media Handling` | Media Handling | `Media Handling` | |
| `Playing Time` | Agreed Playing Time | `Agreed Playing Time` | |
| `Wage` | Wage | `Wage` | |
| `Transfer Value` | Transfer Value | `Transfer Value` | Spanne → untere Grenze |
| `Rating` | Average Rating | `Av Rat` | `-` = keine Spiele |
| `Inf` | Information | `Inf` | wird gelesen, aber nicht ausgewertet |

### Attribute (40)

| Spalte in FM27 | Feld im Analyzer | FM24-Kopf |
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

Gescoutete Attribute kommen als Spanne (`12-15`) und werden wie bei FM24 als
Unter- und Obergrenze gespeichert.

Beide CSV-Stile des Exporters gehen: *English* (Komma, `7.05`) und *German*
(Semikolon, `7,05`). Der Analyzer erkennt das Trennzeichen selbst und liest die
Note mit Punkt oder Komma.

### Nicht gelesen

Alle anderen Spalten, die FM27 anbietet, kennt der Analyzer und überspringt sie
still, darunter `Punching`, `Natural Fitness`,
`Free Kick Taking`, `Long Throws` und `Penalty Taking` (wie bei FM24 nicht unter
den bewerteten Attributen) sowie `Rec.` / `Recommendation` (Scout-Empfehlung von A+ bis F,
erste Spalte der Ansicht für die Spielerdatenbank). Als *unbekannte Spalte* meldet der Import nur Köpfe, die
in keiner der beiden Listen stehen, also z. B. Spalten, die ein Patch umbenannt hat.

## Was bei FM27 (noch) anders ist

- **Fehlende Spalten überschreiben nichts:** Fehlt einer Datei eine Spalte, behält
  der Analyzer den bisher gespeicherten Wert. Beispiel: Die Ansicht für
  *Recruitment > Player Database* bietet keine `Personality`; die Persönlichkeit
  aus einem Kader-Import bleibt erhalten, neue Spieler aus der Datenbank haben
  zunächst keine.

- **Keine Registrierung:** FM27 hat keine Spalte `Reg`. Der Registrierungs-Assistent
  ist für FM27-Spielstände deshalb aus.
- **Rollen und Taktiken:** die 79 FM27-Rollen (40 mit Ball, 39 ohne Ball) mit Key- und
  Preferable-Attributen aus dem Spiel, erzeugt aus `desktop/data/fm27/` durch
  `desktop/scripts/make_definitions_fm27.py` (`definitions_fm27.json`). Startaktiken sind
  die Formationen ohne Ball mit Standardrollen. Zwei Rollen pro Slot folgen nach
  [KONZEPT_ZWEI_ROLLEN.md](../desktop/KONZEPT_ZWEI_ROLLEN.md). Zusätzlich zu FM24 liest
  der Analyzer die Torwart-Attribute `Communication` und `Eccentricity` (Key bzw. Preferable
  der FM27-Torwartrollen); FM24-Spielstände bewerten sie weiterhin nicht.
- **Gewichte:** Attributkategorien und Gewichte sind vorläufig die von FM24
  (eigener Abschnitt `[Weights_fm27]` in `config.ini`).

## Beide Programme synchron halten

- Maßgeblich für die Spaltennamen ist das Spaltenverzeichnis des Exporters:
  [`docs/columns/fm27.csv`](https://github.com/ElectroHugin/FM-View-Exporter/blob/main/docs/columns/fm27.csv),
  Spalte `analyzer_field`. Jede Zeile mit Eintrag dort steht in
  `fm27AttributeMapping()`, alle übrigen in `fm27IgnoredExportColumns()`.
- Benennt ein FM27-Patch eine Spalte um, zeigt der Import sie als unbekannt an.
  Dann beide Stellen anpassen: `fm27.csv` im Exporter und `ImportSchema.cpp` hier.
- Der Test `test_csvimport` (`fm27ExporterCsv`, `fm27MappingComplete`) prüft eine
  Datei im Format des Exporters und dass alle 40 bewerteten Attribute erreichbar sind.
