# Ideenliste: Features & Erweiterungen (C++/Qt)

Übernommen aus [legacy/Future_Ideas.md](../legacy/Future_Ideas.md) und am
2026-09-28 gegen den C++-Stand **v1.3.11** abgeglichen. Technische Schulden,
Bugs und Performance-Themen stehen getrennt in [BACKLOG.md](BACKLOG.md).

Prioritäten: 🔴 hoch · 🟡 mittel · ⚪ niedrig

---

## 🆕 Neue Ideen

### ✅ Registrierungs-Assistent (Premier League + UEFA) — fertig mit v1.4.1

**Ziel:** Meldeliste so bilden, dass sie **registrierbar** ist und keine Plätze
verschenkt. Anlass: Mit zu wenigen HG-Spielern wurden oft nur 21 Spieler
gemeldet, obwohl talentierte Jugendspieler die freien Plätze hätten füllen
können. Ein registrierbarer Allrounder geht vor zwei Spezialisten, von denen
einer nicht registriert werden kann.

- **Regeln** (mit dem User abgestimmt, 2026-09-29; fest im Core pro FM-Version,
  `core/Registration`, nur FM24):
  - **Premier League:** höchstens 25 gemeldete Spieler, davon höchstens 17
    Nicht-Home-Grown. U21 (Stichtag: geboren ab 1.1. des Saisonstartjahres − 21)
    braucht keinen Platz. Home-Grown = drei Spielzeiten vor dem 21. Geburtstag
    bei Vereinen des Verbands; eine Club-trained-Quote gibt es in der PL nicht.
  - **UEFA (CL/EL/ECL, gleiche Regeln):** Liste A mit höchstens 25 Spielern,
    davon höchstens 17 nicht lokal ausgebildet. 4 der 8 reservierten Plätze nur
    für Club-trained, also höchstens 21 ohne Club-trained. Mindestens 2
    Torhüter. Liste B (unbegrenzt) = U21 und Club-trained (Näherung für „2 Jahre
    am Stück im Verein“, keine eigene Pflege).
  - **Bundesliga:** keine wirksame Beschränkung (99 Plätze). Als Liga wählbar,
    verhält sich aber wie „Standard (keine Beschränkung)": keine Liga-Meldeliste,
    alle Spieler spielberechtigt. „Standard" gilt für jede Liga, deren Regeln
    noch nicht hinterlegt sind.
  - Mindestzahl Torhüter in der Meldeliste als Einstellung (Default 2). Die PL
    schreibt keine vor, ohne zwei ist es aber albern.
- **Daten:** FM24 exportiert keinen HG-Status, also ist alles Handpflege.
  Tabelle `player_registration` (Schema v5): `home_grown`, `club_trained`
  (schließt HG ein), `u21` (auto/ja/nein), `league_listed`, `uefa_listed`.
  U21 automatisch: bis 20 ja, ab 22 nein, mit 21 unklar (zählt vorsichtshalber
  als nicht U21, Hinweis). Pflege auf „Spieler bearbeiten“ und per Rechtsklick →
  Registrierung.
- **Algorithmus:** Pool = Erste Mannschaft + Zweitteam ohne Retired.
  `SquadBuilder` über den ganzen Pool und **alle Lieblingstaktiken** (max. 2)
  ergibt die Rangfolge Stamm-XI > B-Team > Tiefe > DWRS. Dann die Quoten in
  dieser Rangfolge füllen. Freie HG-/Club-trained-Plätze werden **immer**
  aufgefüllt, auch mit Jugend und Zweitteam. Anzeige der DWRS-Kosten der Quote
  (Best XI mit gegen ohne Regeln).
- **Umsetzung in Schritten:**
  1. ✅ v1.3.24: Schema v5, Markierungen (Bearbeiten + Rechtsklick),
     Einstellung „Registrierungsregeln“ (Liga, UEFA, Mindest-Torhüter).
  2. ✅ v1.3.25: Auswahl in `core/Registration` (`rankPool`, `propose`,
     `quotaCosts`) mit Tests + Seite „Registrierung“ (nur sichtbar bei
     aktiven Regeln): Vorschlag live, Kosten der Quote, Warnungen (freie
     Plätze, U21 unklar), „Als Meldeliste übernehmen“ mit Abgleich.
  3. ✅ v1.3.25 (mit Schritt 2): UEFA-Liste A/B im Assistenten.
  4. ✅ v1.4.1: Best XI hat eine Wettbewerbs-Auswahl — Liga, Champions /
     Europa / Conference League und Pokal (DFB-Pokal bzw. FA Cup / League Cup,
     ohne Registrierung) — und rechnet die erste Mannschaft nur mit den
     Spielberechtigten (gespeicherte Meldeliste + U21 bzw. Liste B; gemeldete
     Zweitteam-Spieler zählen mit). Die UEFA-Liste ist unabhängig von der Liga,
     gilt also auch bei Bundesliga. Jugend/Zweitteam
     und Leih-/Verkaufslisten bleiben beim ganzen Verein. Auf der Seite
     „Registrierung" hakt man an, wen man im Spiel wirklich gemeldet hat
     (vorbelegt: Empfehlung, nach dem Speichern die eigene Liste); Torhüter und
     Feldspieler getrennt, Standardsortierung nach Position
     (`positionSortKey`: Tor → Sturm, rechts vor links), per Spaltenkopf
     umsortierbar.
- **Später:** La Liga, Serie A, Ligue 1 als weitere Regelwerke (Regeln erst
  recherchieren).

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

### 🟡 Linux-Version

**Ziel:** Die App läuft auch unter Linux. macOS ist nicht geplant.

- **Schritte:**
  1. ✅ 2026-10-02 (ohne Versionsänderung): Linux-Build in CI. CMake ist
     portabel (`WIN32`/`app.rc` nur unter Windows), Presets `linux-debug` /
     `linux-release` (Qt-Pfad aus `QT_ROOT_DIR`), Linux-Job in `ci.yml` und als
     unabhängiger Job in `release.yml`. Baut mit GCC 13 ohne Code-Änderung,
     alle 16 Test-Suiten grün.
  2. Offen: AppImage im Release-Workflow neben dem Windows-Installer (z. B.
     `linuxdeploy` + Qt-Plugin); braucht ein PNG-Icon und eine `.desktop`-Datei.
  3. Offen: Feinschliff nach dem ersten echten Test unter Linux — Schrift
     („Segoe UI" ist fest gesetzt), Standard-Importordner (FM24 läuft über
     Steam/Proton, der Dokumente-Ordner liegt im Proton-Präfix), Emojis in
     Überschriften, Knopf „Im Explorer öffnen", README („Windows 10 / 11 only").

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
(ctest, 16 Suiten), Persönlichkeits-System, Vereins-Header/Theme/Logo,
Taktik-Explorer.

## ❌ Verworfen / Grundsätze

- **Preferred Foot ist kein Seiten-Signal.** Nur das manuelle
  `preferredSide` zählt, für jede seitenbezogene Logik.
