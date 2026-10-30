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

Ziel: kleine, fokussierte Exporte statt immer aller 250+ Spalten.

| Preset | Inhalt | Zweck im Analyzer |
| --- | --- | --- |
| **Analyzer** | Unique ID, Player, Position, Age, Club, Nation, Height, Füße, alle 40 bewerteten Attribute | täglicher Import |
| **Analyzer-Voll** | Analyzer + Wage, Transfer Value, Vertrag, Personality, Media Handling, Playing Time, Rating | Kaufberatung, Talente |
| **Statistik** | Unique ID, Player, Position, Minuten, Spiele, alle Statistik-Spalten je 90 min (xG/90, xA/90 …) | Statistik-Labor (Gewichte aus echten Daten) |
| **Alles** | alle verfügbaren Spalten | Fehlersuche, neuer Patch |

Die genaue Spaltenliste je Preset folgt, sobald der Spaltenkatalog aus einem
echten Voll-Export steht. Das Preset **Analyzer** entspricht der Ansicht aus
`docs/ANSICHT-ANALYZER-FM27.md` im Exporter.

## Offene Fragen

- **Presets technisch:** Exportiert der Exporter nur die sichtbaren Spalten der
  aktuellen Ansicht, oder kann er Spalten selbst wählen? Im ersten Fall sind
  Presets = gespeicherte Ansichten im Spiel (mit Anleitung je Preset).
- **Statistik:** Welche Statistik-Spalten bietet FM27 an, und in welchem Format
  (`0.42`, `0,42`, `-`)?
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
