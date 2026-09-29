# Backlog: Robustheit, Performance & Code-Qualität

Offene Verbesserungen aus dem Architektur-Review (2026-07-13). Die **Bugs 1–7**
(Datenverlust-/Korruptions-/Crash-Risiken) sind in **v1.2.5** behoben.

Format: `Datei:Zeile` — Problem — Vorschlag.

---

## ✅ Erledigt (v1.2.6 – v1.2.8)

Die drei Abarbeitungs-Schritte aus dem Review sind bis auf zwei Restpunkte
umgesetzt:

| Schritt | Version | Punkte |
|---------|---------|--------|
| 1 — Cleanup + contained Perf-Wins | v1.2.6 | 15, 16, 17, 18, 20, 21 |
| 2 — Perf-Hebel Reload/Ratings/Suche + Schema v2 | v1.2.7 | 11 *(teilweise)*, 12, 13, 14, 19 |
| 3 — Robustheit | v1.2.8 | 8, 9, 10 |

Kurzfassung der Umsetzung:

- **#8** DwrsEngine-Plan-Cache: alle `validRoles`-Pläne werden vorab auf dem
  UI-Thread in `reloadConfig()` gebaut → Worker lesen den Cache nur noch.
- **#9** `Player*`-Lebensdauer: `PageBase::releaseStoreRows()` leert bei
  `dataChanged` die rohen `Player*` versteckter Seiten (4 `PlayerTableModel`-
  Seiten + `SquadMatrix::m_scoutedPool`), bevor der Store neu gelesen wird.
- **#10** AssignRolesPage: `savePending`/`autoAssign` mit Revert-on-failure.
- **#12** `dwrs_latest`-Tabelle (materialisiertes „latest je player/role"),
  gepflegt in `appendDwrsRatings`/`mergePlayerInto`; `latestDwrsRatings()` ist
  jetzt ein Scan statt `MAX(ts)`-Self-Join.
- **#13** `rebuildSuggestions` iteriert nur bewertete Spieler je Rolle.
- **#14** Sidebar-Suche: schlankes `PlayerSearchModel` + `FoldingCompleter`
  (Umlaut-/Akzent-Faltung).
- **#15** GapAnalysis cached geparste Positionen je Spieler.
- **#16** In-Memory-Cache der DB-Settings.
- **#17** gemeinsames `widgets/NumericTableItem.h`.
- **#18** `parseDwrsTimestamp()` in `core/Utils`.
- **#19** Schema-Migration v1→v2: `dwrs_latest` + tote Spalten
  `registration`/`information` per `ALTER TABLE DROP COLUMN` entfernt.
- **#20** Milestone-Placeholder → bewusster „unknown pageId"-Guard.
- **#21** Widget-Aufräumen auf `deleteLater()` vereinheitlicht.
- **Nachtrag zu #14 (v1.2.9):** Profilseiten-Suche ebenfalls auf
  `PlayerSearchModel` umgestellt. Der alte `QStandardItemModel`-Aufbau feuerte
  pro Spieler ein `dataChanged`, worauf der Completer jedes Mal mit dem alten
  Suchtext neu filterte (O(n²) → 20–30+ s Freeze beim ersten Tippen nach
  jedem Reload/Scope-Wechsel). Jetzt ein einziger Modell-Reset (<0,5 s).

---

## ✅ Rest von #11 — Voll-Reload bei jedem kleinen Save — erledigt in v1.3.19

> **Umsetzung:** `Database::loadPlayers(ids)` (id-gechunkt, inkl. Rollen/Kader/
> Shortlist/Trainingsrolle), `PlayerStore::replace` (in-place, Indizes bleiben
> konsistent, `Player*` bleiben gültig), `RatingsUpdater::patchRoleRatings`
> (inkrementell = Voll-Neuaufbau, getestet). `AppContext::refreshPlayers(ids)` /
> `refreshRatings(ids)` / `setNationalSquad(ids)` ersetzen `reloadFromDatabase`
> in Transfers, Spieler bearbeiten, Abgängen, Nominierungs-Assistent,
> Kader-Auswahl, National-Import, manuellem Einzel-Update und nach jeder
> Teil-Neuberechnung (Rollen zuweisen). Fallback auf Voll-Reload, falls ein
> Spieler fehlt/neu ist. **Gemessen** (Snapshot, 35k Spieler): Voll-Reload
> 1.458 ms → gezielt 20 Spieler 15 ms. Voll-Reload bleibt nur beim DB-Wechsel.
> Tests: `loadPlayersSubsetMatchesFullLoad`, `patchEqualsFullRebuild`.

- **Datei:** [src/app/AppContext.cpp](src/app/AppContext.cpp) `reloadFromDatabase`;
  Aufrufer u. a. TransfersPage, EditPlayerPage, DashboardPage (Abgänge),
  NationalDashboardPage, NationalSquadSelectionPage.
- **Stand:** Der teuerste Teil (Ratings-Aggregat) ist mit **#12** weg, und der
  Recalc-Pfad nutzt jetzt `reloadRatings()` (nur Ratings, Spieler unangetastet).
  Die verbleibenden Save-Pfade laden weiterhin **alle** ~35k Spieler synchron
  neu für 1–20 geänderte Zeilen.
- **Vorschlag:** Geänderte Spieler gezielt im Store patchen (in-place per Row,
  hält `Player*` gültig — verwandt mit **#9**) und nur `dataChanged` emittieren,
  statt den kompletten Store zu ersetzen. Erfordert einen gezielten
  `Database::loadPlayers(ids)`-Pfad und Store-Patch je Aufrufer.

---

## ✅ #22 — `dwrsHistory` baut unbegrenzte `IN (…)`-Klausel — erledigt in v1.3.17

> `Database::forEachIdChunk` (500er-Blöcke, sortiert → global geordnet), genutzt von
> `dwrsHistory` und dem neuen `latestDwrsRatings(ids)`.

- **Datei:** [src/core/Database.cpp](src/core/Database.cpp) `dwrsHistory`.
- **Problem:** Bei sehr großen id-Listen theoretisch > Platzhalter-Limit
  (praktisch club-begrenzt).
- **Vorschlag:** Chunking (z. B. 500er-Blöcke) oder Temp-Table-Join.

---

# Code-Analyse 2026-09-28 (Stand v1.3.11)

Neue Funde aus einer vollständigen Durchsicht von Core + Save-/Reload-/Worker-
Pfaden der App. Nummerierung setzt die Review-Liste fort. Empfohlene
Reihenfolge: ~~#23~~ → ~~#28~~ → ~~#24/#25 (+#33)~~ → ~~#30~~ → ~~#26/#27~~ → ~~#31/#32~~ (+ ~~#22~~). ~~#38~~ → ~~#11~~ → ~~#34~~ → ~~#35~~. Offen: #29, #36, #37.
Feature-Ideen stehen getrennt in [IDEEN.md](IDEEN.md).

## 🔴 Funktionale Bugs

### ✅ #23 — Entfernte Rollen wirken weiter (stale Ratings + `primaryRole`) — erledigt in v1.3.12

> **Umsetzung:** `RatingsUpdater::roleRatingsForAssigned` baut den Rating-Cache
> nur noch aus zugewiesenen Rollen (Historie/`dwrs_latest` bleiben unangetastet,
> wirkt auch auf Altdaten ohne Migration). `SquadBuilder` ignoriert eine nicht
> mehr zugewiesene `primaryRole`; `AssignRolesPage` leert sie beim Speichern
> (`RoleAssignment::clearStalePrimaryRole`, Revert bei DB-Fehler).
> Regressionstest `test_roleratings`.

- **Dateien:** [src/core/RatingsUpdater.cpp](src/core/RatingsUpdater.cpp)
  (Rollen-Gruppierung), [src/core/SquadBuilder.cpp](src/core/SquadBuilder.cpp)
  `buildCandidates`, [src/app/pages/AssignRolesPage.cpp](src/app/pages/AssignRolesPage.cpp)
  `savePending`/`autoAssign`, [src/app/AppContext.cpp](src/app/AppContext.cpp)
  `rebuildRatingsCache`.
- **Problem:** Wird einem Spieler eine Rolle entnommen, bleibt ihre letzte Zeile
  in `dwrs_latest` stehen (Recalc rechnet nur zugewiesene Rollen, löscht nichts).
  `m_ratings` enthält sie weiter, und `SquadBuilder` prüft `assignedRoles` nicht
  → der Spieler kann in Best XI, Gap-Analyse, Taktik-Explorer,
  Nominierungs-Assistent und Transferzielen weiter auf der entfernten Rolle
  landen (mit eingefrorenem Wert). Zusätzlich bleibt eine `primaryRole` auf die
  entfernte Rolle gesetzt → der Spieler ist für **alle anderen** Slots gesperrt
  (`primaryRole != role → continue`). Legacy-Parität, aber falsch.
- **Vorschlag:** Beim Speichern von Rollenänderungen `dwrs_latest`-Zeilen
  entfernter Rollen löschen (Historie behalten) und eine nicht mehr zugewiesene
  `primaryRole` leeren; alternativ `rebuildRatingsCache` nur zugewiesene Rollen
  übernehmen lassen. Regressionstest.

### ✅ #24 — „Retired"/veraltete Spieler in Vorschlägen — erledigt in v1.3.14

> **Umsetzung:** Neues Core-Modul `core/PlayerStatus` (`isRetiredClub`,
> `isRetired` = Club „Retired" **oder** Freshness-Retired, `isNationalEligible`,
> `isAvailableForNation`). Ausgeschlossen werden Retired-Spieler jetzt aus den
> Dashboard-Transferzielen, den National-Transferzielen, der Kader-Auswahl
> (verfügbare Spieler) und dem Nominierungs-Assistenten; die „'Retired'
> ausblenden"-Checkbox beider Matrizen blendet auch Freshness-Retired aus.
> **Bewusst nicht** ausgeschlossen: nur *veraltete* (stale) Spieler — alte
> Scouting-Daten heißen nicht, dass der Spieler weg ist.

- **Dateien:** [src/app/pages/DashboardPage.cpp](src/app/pages/DashboardPage.cpp)
  `rebuildSuggestions`, NationalDashboardPage (Transferziele),
  NationalSquadSelectionPage.
- **Problem:** Die User-Konvention Club = „Retired" (und die live berechnete
  Freshness-Einstufung Retired/veraltet) wird nur im Nominierungs-Assistenten
  und in der Squad Matrix berücksichtigt. Die Dashboard-Transferziele schlagen
  solche Spieler vor.
- **Vorschlag:** Zentrale Verfügbarkeits-Prüfung im Core (siehe #33) und in
  allen Vorschlags-Pools nutzen.

### ✅ #25 — Abgang-Dialog: drei Lücken — erledigt in v1.3.14

> **Umsetzung:** (1) `isFreeAgent` erkennt „FrA" (case-insensitive).
> (2) `PlayerStatus::applyDeparture` leert Transfer-/Leihliste und `newClub`.
> (3) **Entfällt:** Die Transfers-Seite übernimmt einen eingetragenen
> „Neuen Verein" sofort als Club (Legacy-Verhalten) — ein Spieler mit
> geplantem Ziel steht nie mehr beim eigenen Verein und erscheint daher nicht
> im Abgang-Dialog.

- **Dateien:** [src/app/pages/DashboardPage.cpp](src/app/pages/DashboardPage.cpp)
  `resolveDepartures`, [src/core/Utils.cpp](src/core/Utils.cpp) `isFreeAgent`.
- **Probleme:**
  1. Ziel „FrA" wird von `isFreeAgent()` nicht als vereinslos erkannt → diese
     Spieler fehlen in der Free-Agent-Suche der Squad Matrix.
  2. Beim Abgang bleiben `transferStatus`/`loanStatus`/`newClub` gesetzt.
  3. Ein auf der Transfers-Seite geplanter `newClub` wird nicht als Ziel
     vorbelegt (immer „FrA").
- **Vorschlag:** „FrA" in `isFreeAgent` aufnehmen, Flags beim Abgang leeren,
  Ziel mit `newClub` vorbelegen, falls gesetzt.

### ✅ #26 — Ungestempelte Alt-Spieler werden nie veraltet — erledigt in v1.3.16

> **Umsetzung:** `Freshness::uploadsSinceSeen` wertet `lastSeenUpdate == 0` als
> „zuletzt bei Upload 0 gesehen"; solange der Zähler 0 ist, bleibt alles frisch.
> Keine Sofort-Flut: Der Zähler startet mit der Frische-Erfassung bei 0
> (bayern2026-2-0 am 2026-09-29: Zähler 0, alle 34.951 Spieler ungestempelt →
> erst nach 5 künftigen Imports ohne den Spieler greift „veraltet").
> Tests in `test_freshness`.

- **Datei:** [src/core/Freshness.cpp](src/core/Freshness.cpp) `uploadsSinceSeen`.
- **Problem:** `lastSeenUpdate == 0` (vor v1.3.5 importiert, seither nie
  gesehen) gilt dauerhaft als „aktuell" → Auto-Retired greift genau bei den
  ältesten Karteileichen nie.
- **Vorschlag:** Ungestempelte Zeilen als „zuletzt bei Upload 0 gesehen"
  behandeln (nach Y Uploads veraltet). Tests in `test_freshness` anpassen.

### ✅ #27 — HTML-Import ist nicht atomar — erledigt in v1.3.16

> **Umsetzung:** Verschachtelbare Transaktionen in `Database`
> (`beginTransaction`/`commitTransaction`/`rollbackTransaction`: äußere Ebene
> echte Transaktion, innere Ebenen SAVEPOINTs; alle Schreibmethoden nutzen sie)
> plus RAII `ScopedTransaction`. `importHtml` klammert ID-Unification, alle
> Batches und den Upload-Zähler in eine Transaktion; ein Rollback verwirft
> auch den Settings-Cache. Gegenprobe: alter Importer ließ nach Fehler im
> 2. Batch 2.000 Spieler zurück, neuer keinen. Tests: `test_htmlimporter`
> (`failedImportRollsBackEverything`), `test_database` (`nestedTransactions`).

- **Datei:** [src/core/HtmlImporter.cpp](src/core/HtmlImporter.cpp) `importHtml`
  (Merges/Renames + `flush` je 2000er-Batch).
- **Problem:** ID-Unification und jeder Batch committen einzeln; ein Abbruch
  mitten im Import hinterlässt einen Teil-Import (nur das Backup rettet).
- **Vorschlag:** Eine äußere Transaktion über den ganzen Import (die
  Database-Methoden dürfen dann keine eigene Transaktion öffnen → Savepoints
  oder `inTransaction`-Flag).

## 🟠 Robustheit / Threads

### ✅ #28 — Esc schließt Progress-Dialoge, Worker läuft weiter — erledigt in v1.3.13

> **Bestätigt** (Qt 6.8.3): ein `QProgressDialog` ohne Cancel-Knopf schließt
> bei Esc **und** blendet sich bei Erreichen des Maximums automatisch aus
> (`autoClose`/`autoReset`) — die Worker melden ihren letzten Schritt aber
> *vor* dem finalen DB-Schreiben. **Umsetzung:** `widgets/BusyProgressDialog`
> (ignoriert Esc/Schließen bis `finish()`, kein Auto-Close, kein X-Knopf) in
> ImportRunner, RecalcHelper und Voll-Recalc; laufende Jobs zentral via
> `AppContext::registerBackgroundTask`, gewartet wird vor Engine-/Definitions-
> Reload, DB-Wechsel, `closeEvent` und im `~AppContext`; eindeutige
> Worker-Verbindungsnamen (`Database::uniqueConnectionName`). Gleiches Muster im
> Migrations-Assistenten (Esc während der Migration) mit behoben. Tests
> `test_busydialog` (Widget-Test, offscreen) + `test_database`.

- **Dateien:** [src/app/ImportRunner.cpp](src/app/ImportRunner.cpp),
  [src/app/RecalcHelper.cpp](src/app/RecalcHelper.cpp),
  [src/app/MainWindow.cpp](src/app/MainWindow.cpp) `startDwrsRecalc`/`closeEvent`.
- **Problem (sehr wahrscheinlich, praktisch verifizieren):** Die
  `QProgressDialog`s haben keinen Cancel-Knopf, aber Esc löst `reject()` aus →
  Dialog weg, Fenster wieder bedienbar, Worker läuft weiter. Dann möglich:
  zweiter Import (beide Worker nutzen den Verbindungsnamen `"import_worker"`),
  Einstellungen speichern (`reloadConfig()` leert den Plan-Cache, während der
  Worker liest), App schließen (`closeEvent` wartet nur auf `m_recalcWatcher`,
  nicht auf Import/`recalcDwrsFor`) → Crash/Use-after-free.
- **Vorschlag:** Esc/Close im Dialog abfangen; laufende Futures zentral im
  `AppContext` registrieren; `closeEvent` wartet auf alle; eindeutige
  Worker-Verbindungsnamen.

### #29 — Vereinswechsel im Dashboard-Combo ohne Benachrichtigung

- **Datei:** [src/app/pages/DashboardPage.cpp](src/app/pages/DashboardPage.cpp)
  `m_clubCombo` activated.
- **Problem:** `user_club` wird gespeichert, aber Header und andere Seiten
  erfahren nichts davon.
- **Vorschlag:** `m_context.notifySettingsChanged()` aufrufen.

## 🟡 Performance

### ✅ #30 — `Definitions` baut Hashes bei jedem Zugriff aus dem JSON neu — erledigt in v1.3.15

> **Umsetzung:** `Definitions::rebuildCache()` baut alle abgeleiteten Tabellen
> einmal in `load()`/`setRoot()` (ein fehlgeschlagener `load()` lässt sie
> unangetastet); die Accessoren geben `const &` zurück, `personalityCategory`
> nutzt eine vorberechnete Kleinschreibungs-Tabelle statt eines linearen Scans.
> **Gemessen** (Release, echte definitions.json, identische Ergebnisse):
> 35k × `personalityCategory` 421 → 4 ms; 2k × `tacticRoles` +
> `positionToRoleMapping` 116 → 0 ms; 2k × `sortRolesNaturally` 313 → 1 ms.
> Tests in `test_definitions` (Cache folgt `setRoot`, Fehl-Load, Defaults).

- **Datei:** [src/core/Definitions.cpp](src/core/Definitions.cpp)
  (`personalities`, `personalityCategory`, `tacticRoles`,
  `positionToRoleMapping`, `roleDisplayMap`, `naturalRoleSorter`, …).
- **Problem:** `personalityCategory()` läuft in heißen Pfaden: Persönlichkeits-
  filter der Squad Matrix je Spieler (35k), Zell-Styles bei **jedem** Paint
  (Background + Foreground → doppelt), Talent-Score. Der Trainingsplan ruft pro
  Spieler `tacticRoles()` + `positionToRoleMapping()` auf.
- **Vorschlag:** Geparste Strukturen in `load()`/`setRoot()` einmal aufbauen,
  Accessoren geben `const &` zurück. Größter bisher unerkannter Perf-Hebel.

### ✅ #31 — Import schreibt/lädt unnötig viel — erledigt in v1.3.17

> **Umsetzung:** `upsertPlayers(…, RoleWrite::Keep)` im Import (Rollen nur bei
> Merge-Übernahme via neuem `replacePlayerRoles`); Auto-Rollen schreiben nur Rollen.
> Importer aktualisiert die Spielerliste in-place (`updatedPlayers`, gleiche
> Reihenfolge wie `loadPlayers()`, inkl. ID-Wiederverwendung nach Merge) → kein
> zweites Laden im Worker; die UI übernimmt sie per `AppContext::adoptPlayers`
> statt Voll-Reload. HTML-String wird nach dem Parsen freigegeben. Pipeline liegt
> jetzt UI-frei in `core/ImportPipeline` (App und `fmbench` nutzen denselben Code).
> Zusätzlich gefunden: das DWRS-Schreiben war der größte Posten → mehrzeilige
> INSERTs + `PRAGMA cache_size = 64 MB` (8,1 → 3,5 s allein durch den Cache).
> **Gemessen** mit `fmbench <db> --import` (neu: Import-Modus auf einer Dateikopie),
> Snapshot von bayern2026-2-0 (34.951 Spieler) + `Wrexham_Scouting.html` (11.121 Zeilen,
> 7.757 neu, 164.631 DWRS-Zeilen geschrieben):
>
> | Phase | vorher | nachher |
> |---|---|---|
> | Worker gesamt | 19,2 s | 8,3 s |
> | davon DWRS schreiben | 11,6 s | 3,7 s |
> | davon Import-DB + Nachladen | ~2,7 s | 0,9 s |
> | davon Auto-Rollen | 1,5 s | 0,8 s |
> | UI-Thread (Freeze) nach Import | 1,9 s | 1,1 s |
> Tests: `updatedPlayersMatchReload` (Feld-für-Feld + Reihenfolge vs. Reload),
> `bulkRatingsAndChunkedReads`.

- **Dateien:** [src/core/Database.cpp](src/core/Database.cpp) `upsertPlayers`,
  [src/app/ImportRunner.cpp](src/app/ImportRunner.cpp).
- **Probleme:** `upsertPlayers` löscht und schreibt die Rollen **jedes**
  Spielers neu, obwohl der Import Rollen nicht ändert (→ ganze
  `player_roles`-Tabelle pro Import). Pro Import 3× vollständiges `loadPlayers`
  (Worker vor/nach Import + UI-Reload). Ganze Datei als `QString` + komplette
  Zellmatrix im Speicher (77-MB-Export → mehrere hundert MB Peak).
- **Vorschlag:** Rollen nur bei Änderung schreiben (Flag/eigener Pfad),
  Worker-Reload auf betroffene Spieler begrenzen, Zeilen streamend verarbeiten.

### ✅ #32 — Recalc-Pfade laden/kopieren zu viel — erledigt in v1.3.17

> Voll-Recalc endet mit `reloadRatings()`; `recalcDwrsFor` kopiert nur die
> betroffenen Spieler; `RatingsUpdater` lädt bei einer Teilmenge nur deren
> `dwrs_latest`-Zeilen (`latestDwrsRatings(ids)`) statt aller ~400k.

- **Dateien:** [src/app/MainWindow.cpp](src/app/MainWindow.cpp) (Voll-Recalc-
  Finish), [src/app/RecalcHelper.cpp](src/app/RecalcHelper.cpp).
- **Problem:** Der Voll-Recalc endet mit `reloadFromDatabase()` statt
  `reloadRatings()`; `recalcDwrsFor` kopiert alle 35k Spieler für eine Handvoll.
- **Vorschlag:** `reloadRatings()` nutzen; nur betroffene Spieler kopieren.

## ⚪ Tech-Debt / Konsistenz

- ✅ **#33 (v1.3.14, via `PlayerStatus`) — National-Berechtigung 5× dupliziert, inkonsistent.** Callup,
  Kader-Auswahl, National-Squad-Matrix und National-Dashboard behandeln
  `age <= 0` und Age-Limit 0 unterschiedlich. → Eine Core-Funktion (inkl.
  Retired-Check, siehe #24).
- ✅ **#34 (v1.3.21) — Zwei Auto-Assign-Semantiken.** Umgesetzt: Knopf heißt jetzt „Fehlende Standardrollen ergänzen" und nutzt wie der Import `RoleAssignment::missingRoleAdditions` (additiv, schreibt nur Rollen); der Reset-Knopf nutzt `RoleAssignment::DefaultRoles`, Duplikat entfernt. Der Import nutzt das additive
  `RoleAssignment::autoAssignMissingRoles`; der Knopf „nur Unzugeordnete" in
  [AssignRolesPage.cpp](src/app/pages/AssignRolesPage.cpp) `autoAssign` hat
  duplizierte Legacy-Logik. → Auf `RoleAssignment` zusammenführen.
- ✅ **#35 (v1.3.22) — Drei GK-Erkennungen.** Umgesetzt: `Player::isGoalkeeper()` wird von SquadBuilder, TalentEngine, NationalCallup und Spieler bearbeiten genutzt; Test in `test_nationalcallup`. `SquadBuilder` (`contains("GK")`),
  `NationalCallup::isGoalkeeper` (Positionen), `TalentEngine::ageCapForPlayer`.
  → Eine Funktion.
- **#36 — Kleinkram:** Tabellen-Namensfilter (`PlayerFilterProxy`) ohne
  Umlaut-Faltung (anders als die Sidebar-Suche); CSV-Export mit Komma trennt in
  deutschem Excel nicht (`;` oder `sep=`-Zeile); `valueToFloat` kennt nur €
  (£/$ → Marktwert 0); `DwrsEngine::planFor` befüllt den Cache lazy für
  unbekannte Rollen (Race, falls parallel zu einem Worker).

## ✅ #38 — Verbleibende Import-Kosten — erledigt in v1.3.18

> **HTML-Parser:** `extractTable` sucht Tags jetzt per `<`-Zeichensuche +
> ASCII-Vergleich (statt case-insensitiver Teilstring-Suche), findet `<td`/`<th`
> in einem Durchgang und übernimmt reine Textzellen als eine Kopie. Belegt
> identisch: SHA1 über alle Zellen aller 15 echten Exporte unverändert
> (`fmbench --parse`). Tempo: `bayern_allplayers.html` (86 MB) 16,0 → 1,3 s,
> `Wrexham_Scouting.html` (11 MB) 2,0 → 0,15 s. Randfall-Test
> `extractTableEdgeCases`.
>
> **UI-Übernahme:** Store + beide Rating-Caches baut jetzt der Import-Worker
> (`ImportPipelineResult::store/latestRatings/ratings`, Stufe „Ergebnis wird
> vorbereitet…"); die UI tauscht nur noch per `AppContext::adoptState`.
> UI-Freeze nach dem Import 1,1 s → 0 ms; Worker gesamt 8,3 → 7,0 s.
>
> Hinweis: Andere Speichervorgänge bauen die Rating-Caches weiterhin auf dem
> UI-Thread neu (`reloadFromDatabase`/`reloadRatings`, je ~1 s bei 35k
> Spielern) — das bleibt der Rest von **#11**.

## ⚪ Testlücken

- **#37** Keine Unit-Tests für `SquadBuilder`, `RoleAssignment`,
  `RatingsUpdater`, `GapAnalysis`, `TacticExplorer`, `TalentEngine`; der
  Squad-Golden-Vergleich (`fmsquad`) läuft nicht in CI. Mindestens #23 und #25
  mit Regressionstests absichern.
