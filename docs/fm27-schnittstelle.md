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
| **Statistik** | Unique ID, Player, Position, `Minutes`, `Apps`, alle Statistik-Spalten je 90 min (xG/90, xA/90 …) | Statistik-Labor (Gewichte aus echten Daten) |
| **Alles** | alle verfügbaren Spalten | Fehlersuche, neuer Patch |

Die genaue Spaltenliste je Preset folgt, sobald der Spaltenkatalog aus einem
echten Voll-Export steht. Das Preset **Analyzer** entspricht der Ansicht aus
`docs/ANSICHT-ANALYZER-FM27.md` im Exporter.

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
  Grenze: FM27 befüllt nur etwa die ersten 255 Spalten einer Ansicht, auch im Spiel
  selbst. Eine Ansicht „Alles“ mit allen 375 Spalten liefert deshalb rund 120 leere
  Spalten. Presets müssen unter dieser Grenze bleiben.
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
- 30.10.2026: Antworten des Exporters zu den offenen Fragen, Metadaten-Vorschlag, Spaltennamen im Statistik-Preset (Exporter-Projekt).
