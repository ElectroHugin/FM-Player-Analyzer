# Schnittstelle FM View Exporter ↔ FM Player Analyzer (FM27)

Gemeinsame Absprache beider Projekte. Stand: 30.10.2026.
Liegt im Analyzer-Repo auf dem Branch `claude/project-thread-1hrepb`, bis das
FM27-Super-Update auf `main` kommt.

## Wer ist wofür maßgeblich

| Thema | Maßgebliche Quelle |
| --- | --- |
| Spaltennamen und welche Spalte welches Analyzer-Feld füllt | Exporter: `docs/columns/fm27.csv`, Spalte `analyzer_field` |
| Wie der Analyzer die Spalten liest (Pflicht, Formate, ignoriert) | Analyzer: `docs/IMPORT-FM27.md` und `desktop/src/core/ImportSchema.cpp` (`fm27AttributeMapping()`) |
| Filter-Presets, Wertformate, Metadaten, offene Fragen | **diese Datei** |

Ändert sich etwas an einer Stelle, wird die andere im selben Zug angepasst und
hier unter „Änderungen“ vermerkt.

## Grundregeln für den Export

1. **Spaltenköpfe genau wie im Spiel** (englische Oberfläche, volle Namen wie
   `Acceleration`, `Off The Ball`, `Unique ID`). Keine eigenen Kürzel.
2. **`Unique ID` und `Player` in jedem Export**, auch in kleinen Presets.
   `Unique ID` ist der Schlüssel, über den der Analyzer Dateien zusammenführt.
3. **Werte wie angezeigt**, nicht umgerechnet: Spannen `12-15`, maskiert `-`,
   Einheiten (`193 cm`, `€71M - €95M`) bleiben drin, der Analyzer liest sie.
4. **CSV:** UTF-8 (BOM erlaubt), Trennzeichen fest pro Datei, Felder mit
   Trennzeichen oder Zeilenumbruch in `"…"`. Der Analyzer erkennt `;`, `,` und Tab.
5. **HTML:** eine `<table>` mit `<th>`-Kopfzeile.
6. **Doppelte Spaltenköpfe** (z. B. `Nation`): erste Spalte zählt beim Analyzer.
   Neue Doppelungen bitte hier melden.
7. **Wünschenswert:** Metadaten im Dateinamen oder als Kommentarzeile, wenn ohne
   Aufwand möglich: FM-Version, Ansicht (Kader/Suche/Scouting), Preset, Datum.

## Filter-Presets (Entwurf)

Ziel: kleine, fokussierte Exporte statt immer aller 375 Spalten. Abgeleitet aus Edmunds
Voll-Export vom 30.10.2026. `Unique ID` und `Player` stehen immer vorn.

### Analyzer (täglicher Import)

`Unique ID`, `Player`, `Position`, `Best Pos`, `Age`, `Club`, `Nation`, `2nd Nat`, `Height`,
`Preferred Foot`, `Left Foot`, `Right Foot`, `Personality`, `Media Handling`, `Playing Time`,
`Wage`, `Transfer Value`, `Rating`, `Inf`, `Best Role`, `Ability`, `Potential`, dazu alle 47 Attribute:

- Torwart: `Aerial Reach`, `Command Of Area`, `Communication`, `Eccentricity`, `Handling`,
  `Kicking`, `One On Ones`, `Punching`, `Reflexes`, `Rushing Out (Tendency)`, `Throwing`
- Mental: `Aggression`, `Anticipation`, `Bravery`, `Composure`, `Concentration`, `Decisions`,
  `Determination`, `Flair`, `Leadership`, `Off The Ball`, `Positioning`, `Team Work`, `Vision`, `Work Rate`
- Physisch: `Acceleration`, `Agility`, `Balance`, `Jumping Reach`, `Natural Fitness`, `Pace`, `Stamina`, `Strength`
- Technik: `Crossing`, `Dribbling`, `Finishing`, `First Touch`, `Heading`, `Long Shots`, `Marking`,
  `Passing`, `Tackling`, `Technique`
- Standards: `Corners`, `Free Kick Taking`, `Long Throws`, `Penalty Taking`

(69 Spalten. Die Fuß-Spalten bitte weit vorn, siehe Befund unten.)

### Analyzer-Voll (Kaufberatung, Talente)

Analyzer + `Expires`, `Contract Start Date`, `Minimum Fee Release Clause`, `Asking Price`,
`Transfer Status`, `Loan Status`, `Home-Grown Status`, `DoB`, `Pros`, `Cons`, `Style`,
`Media Description`, `Int Apps`, `Int Gls`.

### Statistik (Statistik-Labor)

`Unique ID`, `Player`, `Position`, `Best Pos`, `Age`, `Minutes`, `Apps`, `Starts`,
`Average Rating Club`, `Goals per 90 minutes`, `Asts/90`, `xG/90`, `NP-xG/90`, `xA/90`,
`Shot/90`, `ShT/90`, `KP/90`, `OP-KP/90`, `Ch C/90`, `Pr passes/90`, `Ps A/90`, `Ps C/90`, `Pas %`,
`Drb/90`, `Cr C/90`, `OP-Crs C/90`, `Aer A/90`, `Hdrs W/90`, `Hdr %`, `K Hdrs/90`, `Tck/90`,
`K Tck/90`, `Tck R`, `Int/90`, `Clr/90`, `Blk/90`, `Shts Blckd/90`, `Poss Won/90`, `Poss Lost/90`,
`Pres A/90`, `Pres C/90`, `Sprints/90`, `Dist/90`, `Fouls`, `Yel`, `Red`,
Torwart zusätzlich: `Con/90`, `Cln/90`, `Saves/90`, `Sv %`, `xSv %`, `xGP/90`.

### Alles

Alle verfügbaren Spalten (Fehlersuche, neuer Patch).

## Befund aus dem ersten echten Export (30.10.2026)

Datei `squad_20261030_192739` (Kader FC Bayern, 24 Spieler, 375 Spalten), getestet mit PR #4:

- CSV und HTML werden fehlerfrei eingelesen, keine unbekannten Spalten, alle 40 bewerteten
  Attribute gefüllt. CSV und HTML haben identische Köpfe.
- **Spalten 256–375 kommen leer an** (nur `World Reputation` hat „Unknown“). Letzte gefüllte
  Spalte: Nr. 255 `Yearly Wage Rise`. Betroffen u. a. `Preferred Foot`, `Left Foot`,
  `Right Foot`, `Inf`, `DoB`, `CON`, `SHP`, `Morale`, `Sec. Position`. Der Analyzer braucht die
  Fuß-Spalten für Seitenpräferenz in Best XI und Kaderlücken. **Bitte im Exporter prüfen**
  (Grenze bei 255 Spalten? Zellen außerhalb des sichtbaren Bereichs nicht gerendert?).
- Statistik-Spalten sind alle 0/`-` (Saisonbeginn). Format sonst: Punkt als Dezimaltrenner,
  `0%`, `0.0km`, Datum `30/6/2027`, große Beträge `"€65,000,000"`.

### Zweiter Export: Spielersuche (30.10.2026)

Datei `players_20261030_201715` (Spielersuche, eigene Attribut-Ansicht, 24 Spieler, 61 Spalten),
getestet mit PR #4:

- Import läuft durch, alle 40 Attribute gefüllt. CSV und HTML identisch.
- **Spalte `Position` enthält in der Spielersuche zusätzlich die Rolle**, z. B.
  `ST (C) Target Forward`, `DM, M (C) Deep-Lying Playmaker`, `GK Goalkeeper`. Der Analyzer
  erkennt dann die letzte Position nicht mehr: 15 von 24 Spielern haben gar keine Position,
  bei den übrigen fehlt die letzte (z. B. `ST (C)`). In der Kaderansicht stand nur die Position.
  **Wunsch an den Exporter:** Position und Rolle trennen, also `Position` = nur Positionstext,
  die Rolle in eine eigene Spalte (Vorschlag `Position Role`). Der Analyzer bekommt zusätzlich
  eine Absicherung, die einen angehängten Rollennamen abschneidet.
- **Keine Attribut-Spannen**, auch nicht bei `Knowledge` = `None`. Offen: Zeigt das Spiel für
  diese Spieler wirklich alle Werte, oder liest der Exporter mehr als die Oberfläche anzeigt?
- Neue Spalten `Recommendation` (`B- Very Good`) und `Knowledge` (`Extensive Knowledge`/`None`)
  kennt der Analyzer noch nicht (als unbekannt gemeldet). `Effectiveness` ist leer.
  `Wage` kommt hier als Spanne (`€190K - €215K p/w`).
- Für Kaufberatung fehlen in dieser Ansicht `Height`, `Personality`, die Fuß-Spalten u. a.
  → Preset „Analyzer“ auch für die Spielersuche verwenden.

## Ansichten (.fmf) als Presets

Vorschlag Exporter, 30.10.2026, mit Edmund abgestimmt:

1. **Der Analyzer bestimmt den Inhalt.** Er kennt alle Spalten, die FM27 anbietet, und
   legt je Fokus fest, welche Spalten ein Preset braucht. Die Spaltenliste je Preset steht
   in dieser Datei unter „Filter-Presets“.
2. **Edmund baut die Ansicht im Spiel.** Dazu dient das Menü *Insert Column*,
   *Replace this column* und *Remove this column* im Tabellenkopf. Anschließend speichert
   er sie mit *Export Current View…* als `.fmf`-Datei. Die Datei ist verschlüsselt, das
   spielt keine Rolle: Sie wird nur weitergereicht.
3. **Der Exporter liefert die `.fmf`-Dateien mit**, im Repo unter `views/fm27/` und im
   Release-Paket. Nutzer holen sie sich über *Import View…* in der Ansichtsauswahl.
4. **Regeln für jedes Preset:**
   - `Unique ID` und `Player` sind immer dabei.
   - Höchstens etwa 250 Spalten, weil FM27 dahinter nichts mehr anzeigt.
   - Der Dateiname der `.fmf` entspricht dem Preset-Namen.

**Spaltenkatalog für die Auswahl:** Im Menü heißen viele Spalten anders als im
Tabellenkopf, etwa *Player Of The Match* statt `PoM`. Der Exporter führt deshalb zwei
Dateien:
- `docs/columns/fm27.csv`: Spaltenköpfe, Bedeutung und Analyzer-Feld
- `docs/columns/fm27-menu.csv`: Menüpfad, Menüeintrag und zugehöriger Spaltenkopf

Für die Presets gilt: In der Anleitung stehen die Menüeinträge, die Edmund anklickt. Der
Analyzer arbeitet mit den Spaltenköpfen.
Stand des Menükatalogs: vollständig, 377 Menüeinträge in allen Gruppen. Bis auf `Player`
und `Apps` hat jede Spalte des Voll-Exports einen Menüeintrag. Drei Einträge kamen im
Voll-Export nicht vor: *Based Nation*, *Position Selected* und *Minimum Fee Release
Clause (Domestic Club Release Fee Date Change)*.
Im Spiel sind zwei Spaltenköpfe falsch beschriftet. Das Menü kennt *Attacking/Defending -
Tactical* und *- Technical*, die Tabelle zeigt aber zweimal `Attacking - Tactical` und
zweimal `Defending - Technical`.
Wichtig: Das Menü blendet Spalten aus, die schon in der Ansicht stehen. Der Katalog
entsteht deshalb am besten aus einer fast leeren Ansicht. Die Screenshots dazu liegen im Projektordner unter `fm27-spaltenmenue/`.

### Fertige Ansichten je Bildschirm (31.10.2026)

FM27 teilt Ansichten nicht zwischen Bildschirmen. Der Exporter liefert deshalb drei Dateien
unter `views/fm27/` (Anleitung: `docs/VIEW-ANALYZER-FM27.md` im Exporter):

| Bildschirm | Datei | Exportdatei |
| --- | --- | --- |
| Kader | `FM27-PlayerAnalyzer-Squad-Standard.fmf` | `squad_…` |
| Recruitment > Player Database | `FM27-PlayerAnalyzer-PlayerDatabase-Standard.fmf` | `player-database_…` |
| Recruitment > Shortlists | `FM27-PlayerAnalyzer-Shortlist-Standard.fmf` | `shortlist_…` |

Inhalt jeweils Preset „Analyzer“. Unterschiede in Datenbank und Shortlist: kein `Personality`
(bietet FM27 dort nicht an). Die Datenbank hat zusätzlich `Rec.` (Scout-Note `A+` bis `F`),
die Shortlist stattdessen `Recommendation` (dieselbe Note mit Wort, z. B. `B+ Superb`) und
`Player's Interest` (oft leer); bestätigt mit dem ersten Shortlist-Export am 31.10.2026. Die Spielerspalte heißt dort `Name` statt `Player`.
`Nation` kommt im Kader als voller Name, in der Datenbank als Kürzel. In der Datenbank gibt es
Attribut-Spannen (`8-13`) sowie `Unknown` bei Ability/Potential. Der Analyzer verarbeitet all
das seit PR #4 (`Name` und `Rec.` als Alias, `nationCode()`).
Dateinamen sind einstellbar: `Export.FilePrefix` (z. B. `fc-bayern-squad`), `Export.Timestamp`
und `Export.Formats` (CSV, HTML oder beides).

## Offene Fragen

Antworten vom Exporter (Stand 30.10.2026, getestet mit einem Kader-Export, 24 Spieler,
375 Spalten).

- **Presets technisch:** Der Exporter liest die Tabelle, die das Spiel gerade zeichnet.
  Er kann deshalb **nur Spalten exportieren, die in der aktuellen Ansicht im Spiel
  stehen**; fehlende Spalten kann er nicht selbst hinzufügen. Ein Preset ist damit in
  erster Linie eine **gespeicherte Ansicht im Spiel**, mit Anleitung je Preset (wie
  `docs/ANSICHT-ANALYZER-FM27.md` im Exporter).
  Zusätzlich möglich und geplant: Presets im Exporter als Spaltenliste, die aus einer
  größeren Ansicht nur die genannten Spalten übernimmt (Reihenfolge, `Unique ID` und
  `Player` immer dabei). Das spart Ansichten, ersetzt sie aber nicht.
- **Leere Spalten ab Nr. 256:** Die Ursache liegt im Spiel, nicht im Exporter. FM27
  befüllt nur die ersten 255 Spalten einer Ansicht. Edmund hat geprüft, dass die Spalten
  auch im Spiel selbst leer sind. Der Exporter hat zudem testweise seitlich durch alle
  Spalten gescrollt (`Export.ScanColumns = true`), das Ergebnis war byte-genau gleich.
  Abhilfe: Presets mit höchstens etwa 250 Spalten, wichtige Spalten wie Füße, `Inf` und
  `DoB` weit vorn. Das Preset „Analyzer“ mit 69 Spalten ist davon nicht betroffen. „Alles“
  geht nur aufgeteilt auf zwei Ansichten.
- **Statistik:** Rund 130 Statistik-Spalten, vollständig mit Bedeutung in
  `docs/columns/fm27.csv` im Exporter (Gruppen „Statistik …“). Formate wie im Spiel mit
  englischer Oberfläche:
  - je 90 Minuten und Dezimalwerte: `0.42` (Punkt, zwei Nachkommastellen)
  - Quoten: `81%`
  - Zählwerte: `12`
  - Distanz: `10.4km`
  - Noten: `7.12`, ohne Einsatz `-`
  Mit `Export.CsvStyle = German` schreibt der Exporter reine Dezimalzahlen mit Komma
  (`0,42`, `81,5%`), alles andere bleibt unverändert. Für den Analyzer empfohlen:
  `English`, also Komma als Trennzeichen und Punkt als Dezimalzeichen.
- **Rollen-Eignung:** In der Kaderliste gibt es `Best Role` (Rollenkürzel als Text,
  z. B. `BGK`), dazu `Role Ability` und `Role Ability Out Of Possession` (Sterne, vom
  Exporter als Zahl `0.0`–`5.0` geschrieben). Im Test waren die beiden Role-Ability-
  Spalten leer, vermutlich weil keine Rolle ausgewählt war. `Player Role Suitability`
  und `Position/Role` lagen hinter der Grenze von etwa 255 Spalten und sind noch
  ungeprüft. Key- oder Preferable-Markierungen aus dem Taktik-Bildschirm sind noch
  nicht untersucht.
  Nächster Schritt: Test mit einer schlanken Ansicht mit diesen Spalten und gewählter
  Rolle.
- **Mit/ohne Ball:** Der Exporter schreibt, was die Tabelle beim Druck auf F9 anzeigt.
  Steht der Umschalter auf „Out of Possession“, kommen die Werte für diesen Zustand.
  Ob der Umschalter Spaltenköpfe oder nur Werte ändert, ist noch nicht getestet.
  Getrennt davon gibt es die feste Spalte `Role Ability Out Of Possession`.
  Nächster Schritt: je ein Export in beiden Stellungen.

**Metadaten (Grundregel 7):** Der Dateiname enthält heute Ansicht und Zeitpunkt, z. B.
`squad_20261030_192739.csv`; die Ansicht ergibt sich aus dem Bildschirm im Spiel.
FM-Version und Preset im Dateinamen oder als Kommentarzeile sind noch offen. Eine
Kommentarzeile vor dem Kopf würde den CSV-Leser des Analyzers stören. Vorschlag deshalb:
Metadaten nur im Dateinamen, etwa `fm27_squad_analyzer_20261030_192739.csv`.

## Wie Änderungen laufen

- Änderungen an dieser Datei: Pull Request **gegen den Branch
  `claude/project-thread-1hrepb`**, nicht gegen `main`. Edmund entscheidet über
  den Merge.
- Änderungen an Spaltennamen: zuerst `docs/columns/fm27.csv` im Exporter, dann
  `ImportSchema.cpp` und `docs/IMPORT-FM27.md` hier, Eintrag unter „Änderungen“.
- Große Beispiel-Exporte und Screenshots gehören nicht ins Repo, sondern in
  Edmunds Google Drive bzw. den Projektordner.

## Änderungen

- 30.10.2026: erste Fassung (Analyzer-Projekt).
- 30.10.2026: Antworten des Exporters zu den offenen Fragen, Metadaten-Vorschlag (Exporter-Projekt).
- 30.10.2026: Ansichten (.fmf) als Presets, Menükatalog `fm27-menu.csv` (Exporter-Projekt). Keine Spaltennamen geändert.
- 30.10.2026: Befund Spielersuche (Position mit Rolle, keine Spannen).
- 31.10.2026: Fertige Ansichten je Bildschirm (Kader, Spieler-Datenbank, Shortlist), Befunde und Dateinamen (Exporter-Projekt). Keine Spaltennamen im Exporter geändert; `Name` und `Rec.` sind Spielköpfe.
- 31.10.2026: Shortlist-Spalten bestätigt: `Recommendation` (statt `Rec.`) und `Player's Interest`, beide neu in `docs/columns/fm27.csv` des Exporters (Exporter-Projekt). Der Analyzer ignoriert beide bisher; als Alias für `Rec.` wäre `Recommendation` später möglich.
- 30.10.2026: Presets mit Spaltenlisten und Befund aus dem ersten echten Export ergänzt.
- 30.10.2026: Antwort zu den leeren Spalten ab Nr. 256: Grenze des Spiels (Exporter-Projekt).
