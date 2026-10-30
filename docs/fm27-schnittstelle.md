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

## Offene Fragen

- **Presets technisch:** Exportiert der Exporter nur die sichtbaren Spalten der
  aktuellen Ansicht, oder kann er Spalten selbst wählen? Im ersten Fall sind
  Presets = gespeicherte Ansichten im Spiel (mit Anleitung je Preset).
- **Leere Spalten ab Nr. 256:** Ursache und Abhilfe (siehe Befund).
- **Rollen-Eignung:** Kommen „Role“ / „Role Ability“ aus dem Taktik-Bildschirm
  oder Key/Preferable-Markierungen an die Liste heran?
- **Mit/ohne Ball:** Wie wird der Umschalter „In Possession / Out of Possession“
  in der Kaderliste exportiert?

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
- 30.10.2026: Presets mit Spaltenlisten und Befund aus dem ersten echten Export ergänzt.
