# Schnittstelle FM View Exporter ↔ FM Player Analyzer (FM27)

Gemeinsame Absprache beider Projekte. Stand: 31.10.2026 (Exporter 1.0.0/1.0.1).
Liegt seit Version 2.0.0 des Analyzers auf `main`.

## Wer ist wofür maßgeblich

| Thema | Maßgebliche Quelle |
| --- | --- |
| Spaltenköpfe, Bedeutung, Analyzer-Feld | Exporter: `docs/columns/fm27.csv` |
| Menüeinträge zum Zusammenstellen von Ansichten | Exporter: `docs/columns/fm27-menu.csv` |
| Fertige Ansichten (`.fmf`) und Anleitung | Exporter: `views/fm27/`, `docs/VIEW-ANALYZER-FM27.md` |
| Wie der Analyzer die Spalten liest (Pflicht, Formate, Aliasse) | Analyzer: `docs/IMPORT-FM27.md`, `desktop/src/core/ImportSchema.cpp` |
| Presets, Grundregeln, offene Fragen | **diese Datei** |

Ändert sich etwas an einer Stelle, wird die andere im selben Zug angepasst und
hier unter „Änderungen“ vermerkt.

## Grundregeln für den Export

1. **Spaltenköpfe genau wie im Spiel** (englische Oberfläche). Keine eigenen Kürzel.
   Je Bildschirm können Köpfe abweichen (`Player`/`Name`, `Rec.`/`Recommendation`);
   der Analyzer führt dafür Aliasse.
2. **`Unique ID` und Spielername in jedem Export.** `Unique ID` ist der Schlüssel.
3. **Werte wie angezeigt**, nicht umgerechnet: Spannen `12-15`, unbekannt `-`,
   Einheiten (`193 cm`, `€71M - €95M`, `10.4km`, `81%`), Noten `7.12`.
4. **CSV:** UTF-8 mit BOM, Stil `English` (Komma trennt, Punkt ist Dezimalzeichen).
   `Export.CsvStyle = German` (Dezimalkomma) ist für den Analyzer nicht empfohlen.
5. **HTML:** eine `<table>` mit `<th>`-Kopfzeile, Bildschirm im Titel.
6. **Doppelte Spaltenköpfe** (`Nation`, `Expires`, `Attacking - Tactical`,
   `Defending - Technical`): beim Analyzer zählt die erste Spalte.
7. **Höchstens etwa 250 Spalten je Ansicht.** FM27 befüllt nur die ersten 255 Spalten,
   auch im Spiel selbst. Wichtige Spalten nach vorn.
8. **Sterne** (`Ability`, `Potential`, `Role Ability`): Anzahl so, wie sie im Spiel gefüllt
   aussieht, gelb und grau zusammen (ab Exporter 1.0.1). `Unknown`, wenn das Spiel keine zeigt.
9. **Dateinamen:** `squad_…`, `player-database_…`, `shortlist_…` plus Zeitstempel; Präfix,
   Zeitstempel und Format (CSV/HTML/beides) sind einstellbar. Der Bildschirm steht immer im
   Namen. Die FM-Version fehlt noch (Vorschlag: `fm27_` als Präfix).

## Presets

Ein Preset ist eine **gespeicherte Ansicht im Spiel**: Der Exporter liest nur die Spalten,
die die aktuelle Ansicht zeigt. Der Analyzer legt fest, welche Spalten ein Preset braucht,
Edmund baut daraus die Ansicht, der Exporter liefert sie als `.fmf` mit. FM27 teilt
Ansichten nicht zwischen Bildschirmen, deshalb gibt es je Bildschirm eine Datei.

### Fertig (31.10.2026)

| Bildschirm | Ansicht | Exportdatei |
| --- | --- | --- |
| Kader | `FM27-PlayerAnalyzer-Squad-Standard.fmf` | `squad_…` |
| Recruitment > Player Database | `FM27-PlayerAnalyzer-PlayerDatabase-Standard.fmf` | `player-database_…` |
| Recruitment > Shortlists | `FM27-PlayerAnalyzer-Shortlist-Standard.fmf` | `shortlist_…` |

Alle drei enthalten das Preset „Analyzer“.

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

(69 Spalten. In Player Database und Shortlist ohne `Personality`, dafür `Rec.` bzw.
`Recommendation` und `Player's Interest`; die Spielerspalte heißt dort `Name`.)

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

Alle verfügbaren Spalten, wegen der Grenze von 255 Spalten aufgeteilt auf zwei Ansichten
(Fehlersuche, neuer Patch).

## Testergebnisse

| Datum | Exporte | Ergebnis mit PR #4 |
| --- | --- | --- |
| 30.10. | Kader, Voll-Export (375 Spalten) | fehlerfrei; Spalten ab Nr. 256 leer (Grenze des Spiels) |
| 30.10. | alte Spielersuche-Ansicht | Position mit angehängter Rolle → behoben in 1.0.0 |
| 31.10. | 1.0.0: Kader (43), Player Database (235), Shortlist (6), je CSV + HTML | alle fehlerfrei, keine unbekannten Spalten, Spannen und `-` korrekt |
| 31.10. | Chris Afanou: Potential 1 gelber + 1 grauer Stern | als `1.0` exportiert → behoben in 1.0.1 (`2.0`) |

Statistik-Spalten sind bisher nur zu Saisonbeginn (alle 0) gesehen. Ein Export nach einigen
Spieltagen folgt.

## Offene Fragen

- **Sterne, sicherer Anteil:** Der Analyzer braucht ihn vorerst nicht; die Bewertung macht
  DWRS. Falls später doch, wäre eine Spanne `2.0-3.0` wie bei Attributen denkbar.
- **Rollen-Eignung:** `Role Ability` und `Role Ability Out Of Possession` waren leer
  (vermutlich keine Rolle gewählt), `Player Role Suitability` und `Position/Role` lagen hinter
  Spalte 255. Nächster Schritt: schlanke Ansicht mit diesen Spalten und gewählter Rolle.
- **Mit/ohne Ball:** Der Export zeigt den Zustand des Umschalters beim Druck auf F9. Ob sich
  Köpfe oder nur Werte ändern, ist offen. Nächster Schritt: je ein Export in beiden Stellungen.
- **Statistik-Export** mit echten Werten (siehe oben).

## Wie Änderungen laufen

- Änderungen an dieser Datei: Pull Request **gegen den Branch
  `claude/project-thread-1hrepb`**, nicht gegen `main`. Edmund entscheidet über den Merge.
- Änderungen an Spaltennamen: zuerst `docs/columns/fm27.csv` im Exporter, dann
  `ImportSchema.cpp` und `docs/IMPORT-FM27.md` hier, Eintrag unter „Änderungen“.
- Große Beispiel-Exporte und Screenshots gehören nicht ins Repo, sondern in den Projektordner.

## Änderungen

- 30.10.2026: erste Fassung, Presets mit Spaltenlisten, Befunde Voll-Export und Spielersuche (Analyzer-Projekt).
- 30.10.2026: Antworten zu den offenen Fragen, Grenze 255 Spalten, Ansichten (.fmf) als Presets, Menükatalog (Exporter-Projekt, PR #5).
- 31.10.2026: Fertige Ansichten je Bildschirm, Shortlist-Spalten, Dateinamen (Exporter-Projekt, PR #5).
- 31.10.2026: Test mit Exporter 1.0.0, Sterne-Zählung (1.0.1), Notiz aufgeräumt und auf Stand gebracht (Analyzer-Projekt).
