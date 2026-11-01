# Ideenliste: Features & Erweiterungen (C++/Qt)

Übernommen aus [legacy/Future_Ideas.md](../legacy/Future_Ideas.md) und am
2026-09-28 gegen den C++-Stand **v1.3.11** abgeglichen. Technische Schulden,
Bugs und Performance-Themen stehen getrennt in [BACKLOG.md](BACKLOG.md).

Prioritäten: 🔴 hoch · 🟡 mittel · ⚪ niedrig

---

## 🆕 Neue Ideen

### 🔴 Vorbereitung auf Football Manager 2027

**Ziel:** Wenn FM27 erscheint, soll die App schnell einsatzfähig sein. FM25
gab es nie, FM26 hat der User ausgelassen; der Sprung geht also von FM24 auf
FM27. Abgestimmt am 2026-10-10.

- **Annahmen:** Die 40 Attribute bleiben gleich. Sind die Spieler einmal
  importiert, passt der Rest der App. Offen bleiben neue Rollen, Positionen und
  Taktiken.
- **Export:** Laut offizieller Bestätigung hat FM27 keinen Spielerexport im
  Basisspiel. Für FM26 gibt es den BepInEx-Mod „FM26 Player Export“ (CSV +
  HTML per F9). Da FM27 vermutlich weiter auf Unity läuft, ist eine schnelle
  Portierung realistisch. Ein eigener Mod ist Plan B, falls nach dem Release
  ein paar Wochen nichts kommt.
- **Rollensystem (größtes Risiko):** FM26 hat Defend/Support/Attack abgeschafft.
  Jeder Feldspieler hat eine Rolle **mit Ball** und eine **ohne Ball**, dazu
  getrennte Formationen für beide Phasen; laut Drittquellen über 70 Rollen
  (weg: Mezzala, Trequartista, Enganche; neu u. a. Channel Midfielder,
  Overlapping Centre-Back, Screener, Presser). Unser Modell kennt **eine**
  Rolle pro Slot. Für FM27 braucht ein Slot zwei Rollen.
  - **Bewertung eines Slots:** gewichteter Mittelwert aus DWRS mit Ball und
    DWRS ohne Ball. Anteil **einstellbar, Standard 50 %** (User, 2026-10-10:
    beide Phasen gleich wichtig, im Sinne von „der Stürmer ist der erste
    Verteidiger, der Innenverteidiger der erste Angreifer“).
  - **Position bei Doppel-Formation:** Maßgeblich ist die Position **ohne
    Ball** (Grundordnung); sie entscheidet, ob ein Spieler den Slot spielen
    kann.
  - Betroffen: DWRS, Best XI, Trainingsberater, Kaufberatung,
    Taktik-Explorer, Rollen-Editor. Erst bauen, wenn die FM27-Rollenliste
    feststeht.
- **Attributgewichte ohne Testdaten:** FM27 startet mit den FM24-Kategorien
  als Annahme, in der App als „vorläufig“ gekennzeichnet. Idee: aus vielen
  exportierten Spielern pro Liga und Rolle prüfen, welche Attribute mit guten
  Durchschnittsnoten (`Av Rat`) zusammenhängen. Nur Anhaltspunkte (gute
  Spieler spielen oft in guten Teams), bis Engine-Tests der Community
  vorliegen.
- **Umsetzung in Schritten:**
  1. ✅ **Versionsprofile** (v1.4.12): Jede FM-Version hat ihr eigenes Profil.
     Dazu gehören die Rollen-Datei (Rollen, Rollengewichte, Position → Rolle,
     Taktiken; FM24 `definitions.json`, später `definitions_<id>.json`), die
     Attributkategorien, die Kategorie-Gewichte in config.ini (FM24
     `[Weights]`/`[GKWeights]`, später `[Weights_<id>]`) und der Spielordner
     für den Import. Der Spielstand bestimmt das Profil (`fm_version`; ohne
     Eintrag = FM24). Neue Spielstände werden mit der neuesten Version
     markiert; wählbar beim Anlegen, sobald es mehrere gibt. Fest, sobald der
     Spielstand Spieler enthält. FM24 verhält sich unverändert. Neue Version
     einbauen: siehe Kommentar an `fmDataVersions()` in `core/Constants.h`.
  2. ✅ **Flexibler Import** (v1.6.0): CSV-Import mit Erkennung des
     Trennzeichens, Spaltenschema pro Version (`core/ImportSchema`) und ein
     Hinweis auf fehlende Spalten neben den unbekannten.
  3. **Konzept zwei Rollen pro Slot:** Das Konzept steht in
     [KONZEPT_ZWEI_ROLLEN.md](KONZEPT_ZWEI_ROLLEN.md) (Datenformat, Bewertung,
     Auswirkungen, Reihenfolge). Gebaut wird, sobald die FM27-Rollenliste
     feststeht.
  4. **Zum Release:** ✅ Import (Profil `fm27`, Spaltenschema für die Dateien
     des FM View Exporters, siehe [docs/IMPORT-FM27.md](../docs/IMPORT-FM27.md));
     offen: echten Export einlesen und Zuordnung prüfen, Rollen mit
     Schlüsselattributen aus dem Spiel übernehmen, Positionen prüfen. Die
     Spielfeld-Geometrie (Slots, Slot → Spielposition, Rollen im
     Taktik-Editor) liegt pro Version in `core/PitchGeometry` (eine Quelle);
     für FM27 dort ergänzen. Seit v1.6.0 bietet der Taktik-Editor auf DML/DMR
     nur noch Wingback-Rollen an, wie im Spiel. DL/DR verlangen jetzt die
     Position D (L)/D (R); reine Wingbacks spielen nur auf WBL/WBR (DML/DMR).
  5. **Danach:** Gewichte nachziehen, sobald Engine-Tests vorliegen.

### 🟡 Statistik-Labor: Attributgewichte aus Spielstatistiken prüfen

**Ziel:** Mit echten Spielstatistiken aus einem FM-Export prüfen, welche
Attribute wirklich zu guter Leistung führen, und die DWRS-Gewichte daraus
ableiten oder bestätigen. Für FM27 wichtig, weil es dort anfangs keine
Engine-Tests gibt (FM27-Plan, Schritt 5).

- **Vorbild Torhüter (2026-10-10):** Export „Goalkeeper_Analysis.html“ mit
  8070 Torhütern (Spieljahr 2043), 335 davon mit mindestens 10 Einsätzen und
  Detailstatistik. Maß: xGP/90 (erwartete verhinderte Tore). Die Note eignet
  sich nicht, sie hängt mit −0,91 an den Gegentoren, misst also die Abwehr.
  Ergebnis: Der alte Torwart-DWRS rechnete nur mit 6 Attributen (r = 0,43);
  das neue Schema aus der Analyse erreicht r = 0,58 (v1.4.13).
- **Verfahren:** Spannen als Mitte rechnen; Korrelation je Attribut, auch
  bereinigt um das Gesamtniveau (sonst korreliert alles mit allem); Prüfung
  immer an ungesehenen Daten (wiederholte Halbierung), damit keine
  Scheingenauigkeit entsteht.
- **Feldspieler:** schwieriger, weil fast jede Statistik von Mannschaft und
  Taktik abhängt. Brauchbar sind Kennzahlen, die gegen einen Erwartungswert
  rechnen (z. B. Tore − xG für den Abschluss). Je Positionsgruppe eine
  passende Kennzahl wählen.
- **Mögliche Umsetzung:** eigene Seite: Statistik-Export laden, Kennzahl
  wählen, Attribut-Zusammenhänge und Treffsicherheit des aktuellen DWRS
  anzeigen, Gewichtsvorschlag als Option übernehmen.
- **Hinweis:** Communication, Eccentricity und Punching importiert die App
  nicht (nicht unter den 40 Attributen). Für eine Auswertung müssten sie dazu.

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
  2. ✅ 2026-10-17: AppImage im Release-Workflow neben dem Windows-Installer
     (`linuxdeploy` + Qt-Plugin, PNG-Icon, `.desktop`-Datei, Starttest in CI);
     auf Linux Mint getestet.
  3. Offen: Feinschliff nach dem ersten echten Test unter Linux — Schrift
     („Segoe UI" ist fest gesetzt), Standard-Importordner (FM24 läuft über
     Steam/Proton, der Dokumente-Ordner liegt im Proton-Präfix), Emojis in
     Überschriften, Knopf „Im Explorer öffnen", README („Windows 10 / 11 only").

---

## 🟢 Offen aus der Legacy-Liste (weiterhin relevant)

- **✅ Gap-Analyse → Transfer-Shortlist-Brücke** — erledigt mit v1.4.11: Tab
  „Kaufberatung“ in Best XI (`core/SigningAdvisor`). Statt Median-Vergleich
  werden die besten Spieler des gefilterten Markts (Budget, Höchstalter, nur
  Shortlist) testweise in den Kader geholt und Startelf + B-Team exakt neu
  gelöst; Rangfolge der Positionen nach gemessenem Startelf-Gewinn, inkl.
  Folgeeffekten. Kandidaten per Rechtsklick auf die Shortlist. Die alte
  Gap-Analyse steht eingeklappt darunter, ihr Sidebar-Eintrag ist weg.
- **🟡 Kaufberatung über mehrere Taktiken.** Die Kaufberatung über die
  Lieblingstaktiken gleichzeitig; ein Kauf, der in *allen* die Startelf hebt,
  ist das sicherste Transferziel.
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
