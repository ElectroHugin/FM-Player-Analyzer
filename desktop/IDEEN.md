# Ideenliste: Features & Erweiterungen (C++/Qt)

Übernommen aus [legacy/Future_Ideas.md](../legacy/Future_Ideas.md) und am
2026-09-28 gegen den C++-Stand **v1.3.11** abgeglichen. Technische Schulden,
Bugs und Performance-Themen stehen getrennt in [BACKLOG.md](BACKLOG.md).

Prioritäten: 🔴 hoch · 🟡 mittel · ⚪ niedrig

---

## 🆕 Neue Ideen

### 🟡 Registrierungs-Assistent für restriktive Ligen/Wettbewerbe

**Ziel:** Best XI und Kader so bilden, dass sie **registrierbar** sind. Ein
registrierbarer Allrounder soll zwei Spezialisten vorgezogen werden, von denen
einer nicht registriert werden kann.

- **Beispiele:**
  - Premier League: 25er-Kader, begrenzte Zahl Nicht-Home-Grown über 21,
    U21 frei.
  - Champions League: Liste A mit Pflichtplätzen für „Home Grown“, davon ein
    Teil vereinsausgebildet (HGC). Die übrigen HG-Plätze dürfen
    verbandsausgebildet (HGN) sein.
  - Bundesliga-Regeln: relevant für den Bayern-Spielstand.
- **Daten:**
  - pro Spieler Flags *HG-Club* und *HG-Nation*, pflegbar auf „Spieler
    bearbeiten“. Dafür ist eine Schema-Migration v4→v5 nötig.
  - Zu prüfen: Ob FM24 den Home-Grown-Status als Spalte exportieren kann
    (dann automatisch importieren). Die heute ignorierten Export-Spalten
    `Reg`/`Inf` darauf ansehen.
  - Alter nur als ganze Jahre vorhanden: U21-Grenzen sind eine Annäherung,
    da das Geburtsdatum fehlt.
- **Regeln:** als konfigurierbare Presets (z. B. Abschnitt
  `registration_rules` in `definitions.json`: Kadergröße, HG-Mindestzahl,
  davon HGC, Altersausnahmen). Die konkreten Zahlen vor der Umsetzung gegen die
  FM24-Regeln prüfen, nicht aus dem Gedächtnis übernehmen.
- **Algorithmus:**
  1. Normale Auswahl (`SquadBuilder`, „weakest link first“).
  2. Quoten prüfen.
  3. Bei Verstoß den Tausch mit dem **geringsten DWRS-Verlust** wählen
     (Nicht-HG raus, bester HG-Kandidat rein).
  4. Wiederholen, bis alle Quoten erfüllt sind.

  Das ergibt genau den gewünschten „Kompromiss statt Spezialist“. Der
  Nominierungs-Assistent (`core/NationalCallup`) löst schon „bester Kader aus
  N Spielern mit G Torhütern“; die Quoten sind dort eine natürliche
  Erweiterung.
- **UI:** Wettbewerb wählen → registrierter Kader, verletzte Quoten, Liste
  „nicht registrierbar“ und die DWRS-Kosten jeder Quote.
- **Voraussetzung:** Backlog **#23** („entfernte Rollen wirken weiter“) — seit
  v1.3.12 erledigt.

### ⚪ Screenshot → Spontan-DWRS (Bilderkennung)

**Ziel:** Screenshot eines Spielerprofils aus FM24 aufnehmen, in der App einen
Knopf drücken → Attribute werden erkannt → sofortige DWRS-Rechnung für alle
passenden Rollen, ohne HTML-Export.

- **Auslöser:** Screenshot aus der Zwischenablage einfügen (Win+Shift+S →
  Knopf/Strg+V). Das ist einfacher als ein Dateidialog. Alternativ Datei
  per Drag & Drop.
- **Erkennung, Optionen:**
  - Windows-eigenes OCR (`Windows.Media.Ocr`, ohne Zusatzabhängigkeit)
  - Tesseract (schwere Abhängigkeit)
  - eigenes Ziffern-Template-Matching auf dem festen FM-Attributraster
    (1–20 auf farbigem Grund). Evtl. am robustesten.
- **Knackpunkte:**
  - verschiedene Auflösungen, Skins und UI-Zoom
  - maskierte Bereichswerte („12-15“) bei ungescouteten Spielern
  - Zuordnung Zahl → Attributname über die Beschriftung
  - Torwart-Layout
- **Ergebnis:** temporäres `Player`-Objekt → `DwrsEngine::calculate` je Rolle,
  Anzeige der Top-Rollen, dazu das Spielfeld-Diagramm. Optional „in Spieler
  übernehmen“ (Abgleich über den Namen, Bestätigung wie beim manuellen
  Einzel-Update im Profil).
- Unabhängig vom restlichen Code; kann jederzeit als eigenes Modul entstehen.

---

## 🟢 Offen aus der Legacy-Liste (weiterhin relevant)

- **🟡 Gap-Analyse → Transfer-Shortlist-Brücke.** Aus einer erkannten Lücke
  direkt die besten gescouteten Kandidaten für die Rolle zeigen und per Klick
  auf die Shortlist setzen. Im C++-Port noch nicht vorhanden. Teilt sich die
  Kandidatenlogik mit den Dashboard-Transferzielen (Backlog **#24**: dort fehlt
  der Retired-/Frische-Ausschluss).
- **🟡 Gap-Übersicht über mehrere Taktiken.** Gap-Analyse über die
  Lieblingstaktiken gleichzeitig; Positionen, die in *allen* schwach sind, sind
  die sichersten Transferziele.
- **🟡 Rollen & Taktiken bearbeiten/löschen.** Heute gibt es nur „Neue Rolle“
  und „Neue Taktik“. Achtung: Löschen/Ändern einer Rolle erzeugt dieselben
  veralteten DWRS-Werte wie das Entfernen einer Rollenzuweisung (Backlog **#23**) und
  braucht eine Neuberechnung.
- **⚪ Einstellungen exportieren/importieren** (config.ini + definitions.json +
  Logos/Flaggen als ZIP) für Backup und Weitergabe.
- **⚪ Shortlist-Verwaltung.** Shortlist-Flag existiert (Rechtsklick-Menü,
  Squad Matrix). Offen ist eine eigene Ansicht mit Notizen und Scout-Bewertung,
  oder alles in der Gap→Shortlist-Brücke bündeln.
- **⚪ Finanzübersicht.** Gehaltssumme, Marktwert vs. Alter (Scatter). Daten
  (`wageRaw`, `transferValue`) sind vorhanden.
- **⚪ Team-gegen-Team-Vergleich.** Zwei Kader per `SquadBuilder` nebeneinander.
- **⚪ „Manager-Fazit“.** Eine Satz-Zusammenfassung über Best XI / Gap-Analyse.
- **⚪ CSV-Export auf weiteren Tabellen** (Rollen-Analyse, Gap-Analyse,
  Transfers). Heute nur Squad Matrix.
- **⚪ Stat-Kategorien in `definitions.json` verlagern**
  (`globalStatCategories`/`gkStatCategories` sind in `Constants.cpp` fest
  verdrahtet). Nur zusammen mit dem Definitions-Cache (Backlog **#30**) und sorgfältigen
  DWRS-Regressionstests.
- **⚪ Icons/Emojis** konsistenter in Überschriften und Knöpfen (kosmetisch).

## ✅ Im C++-Port bereits erledigt

- **Browser-Navigation (v1.3.20):** Zurück / Vorwärts / Startseite in der
  Kopfzeile jeder Seite, dazu Alt+← / Alt+→ / Alt+Pos1 und die Maus-Seitentasten;
  der Verlauf merkt sich auch den Modus (Verein/National) und den Spieler der
  Profilseite.

Globale Spielersuche (mit Umlaut-Faltung), Klick/Doppelklick/Rechtsklick auf
Spielernamen → Profil/Vergleich/Bearbeiten (In-Context-Editing), Pros & Cons,
Profilseite, Jugend-/Zweitteam, Tabs, bedingte Formatierung, erweiterte Suche
(Squad-Matrix-Filter: Nationalität, Mindestalter, Free Agents, Talent,
Persönlichkeit), CSV-Export (Squad Matrix), Trennung von Logik und UI
(`fmcore` ohne Widgets, Transfer-Logik in `SquadBuilder`), Test-Harness
(ctest, 12 Suiten), Persönlichkeits-System, Vereins-Header/Theme/Logo,
Taktik-Explorer.

## ❌ Verworfen / Grundsätze

- **Preferred Foot ist kein Seiten-Signal.** Nur das manuelle
  `preferredSide` zählt, für jede seitenbezogene Logik.
