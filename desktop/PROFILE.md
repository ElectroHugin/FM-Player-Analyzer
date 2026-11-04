# Spielstandprofile (ab 2.1.0)

Jeder Spielstand hat ein eigenes, vollständiges Profil. Zwei Spielstände
teilen keine Einstellung mehr, und alles, was einen Spielstand ausmacht, steckt
in seiner `.db`-Datei. Duplizieren, Umbenennen, Sicherungen und der Import
einer fremden Datenbank nehmen das Profil deshalb automatisch mit.

## Was gehört wohin

| Inhalt | Ort | Zugriff im Code |
|---|---|---|
| Bewertungsgewichte (alle FM-Versionen), Rollen-Multiplikatoren, APT-Gewichte | Profil, `profile_settings` | `context.config()` |
| Analyse-Optionen: Altersgrenzen, Auswahlbonus, Anteil mit Ball, Kaderplanung, Lückenanalyse, Aktualität, Trainingsfenster | Profil, `profile_settings` | `context.config()` |
| Gesamtes Farbschema (7 Rollen je Modus), Hell-/Dunkelmodus, Farbvorlage | Profil, `profile_settings` (`ThemeSettings/…`) | `config().themeSettings()`, `ThemeManager` |
| Ordner für FM-Exporte | Profil, `profile_settings` (`Import/…`) | `config().htmlExportDir()` |
| Eigene Rollen, Taktiken (pro FM-Version) | Profil, `profile_documents` `definitions_<version>` | `context.definitions()`, Speichern mit `context.saveDefinitions()` |
| Vereinslogo, National-Flagge | Profil, `profile_documents` `club_logo` / `national_flag` (PNG) | `context.profileImage()` / `setProfileImage()` |
| Verein, zweite Mannschaft, Land, Stadion, Favoriten-Taktiken, Nationalteam, Meldelisten-Regeln, Bridges, Seitenzustände (Filter, Budget …) | Spielstand, Tabelle `settings` (unverändert seit 1.x) | `context.database().setting()` |
| Vereinskopf: Darstellung (`header_style` = `gradient`/`solid`), Wappen-Silhouette (`header_crest_watermark`) | Profil, `profile_settings` (`ThemeSettings/…`) | `ThemeManager::headerStyle()`, `crestWatermark()` |
| Eigene Farbvorlagen | App-weit, `<Datenordner>/color_templates.json` | `ColorTemplateStore` (core) |
| Sprache, Fenstergröße, Datenordner | App-weit, `bootstrap.ini` | `context.paths()` |
| Zuletzt geöffneter Spielstand, „immer letzten öffnen“, Liste der zuletzt geöffneten | App-weit, `config.ini` | `config().dbName()` usw. |
| Aussehen des Startbildschirms | App-weit, `config.ini` `[StartTheme]` (Kopie des zuletzt geöffneten Profils) | automatisch |

Ohne geöffneten Spielstand (Startbildschirm, Kommandozeilen-Werkzeuge, Tests)
lesen und schreiben alle Profil-Schlüssel wie bis 2.0.4 `config.ini`.

## Speicherformat

Zwei Tabellen in der Spielstand-Datei, angelegt beim ersten Öffnen mit 2.1.0
(`CREATE TABLE IF NOT EXISTS`, kein Versionssprung des Datenbankschemas):

```sql
profile_settings  (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID
profile_documents (name TEXT PRIMARY KEY, encoding INTEGER, size INTEGER,
                   data BLOB, updated_at TEXT)
```

* `profile_settings` hält flache Paare `Abschnitt/schlüssel` mit denselben
  Namen wie `config.ini`. Damit wandern Werte 1:1, und die Getter/Setter von
  `AppConfig` sind unverändert. Marker: `Profile/format` (aktuell 1),
  `Profile/seed` (`config.ini`, `defaults` oder `copy:<Spielstand>`),
  `Profile/created_at`.
* `profile_documents` hält größere Inhalte, die nur bei Bedarf gelesen werden.
  JSON wird mit zlib gepackt (`encoding` 1), Bilder bleiben wie sie sind (0).

### Warum so (nach Messung)

* **Zeilen statt eines JSON-Blocks für Einstellungen:** Das ganze Profil
  (77 Werte, 2,3 KB) lädt in einer Abfrage in unter 3 ms. Zeilen erlauben
  gezieltes Schreiben: Ein Hell/Dunkel-Wechsel schreibt genau eine Zeile,
  unveränderte Werte werden gar nicht geschrieben (Test
  `unchangedValuesAreNotWritten`). Ein Block müsste bei jeder Änderung ganz
  neu geschrieben werden.
* **Rollen/Taktiken gepackt:** 54 KB JSON werden zu 4 KB.
* **Im Spielstand statt in einer Nebendatei:** Eine Datei bleibt eine Einheit
  (Sicherung, Kopie, Import), und eine SQLite-Transaktion macht das Speichern
  atomar. Das Profil kostet je Spielstand 16 KB, bei einem großen Spielstand
  von 280 MB also unter 0,01 %.

## Messungen

Werkzeug: `fmprofilebench <definitions.json> [--repeat N]` (erzeugt
künstliche Spielstände in einem Temp-Ordner, fasst nichts Echtes an).
Linux, Release-Build, Median aus 5 Läufen, 4.11.2026.

| Messung | klein (500 Spieler) | groß (35.000 Spieler) |
|---|---|---|
| Spielstand-Datei | 4,0 MB | 277 MB |
| Mehrgröße durch das Profil | 16 KB | 16 KB |
| Umstellung (Profil einmalig anlegen) | 5 ms | 2 ms |
| Profil bereit (Farben, Gewichte, Rollen) | 3 ms | 3 ms |
| Öffnen komplett, 2.0.4 | 29 ms | 2,9 s |
| Öffnen komplett, 2.1.0 | 20 ms | 2,1 s |
| Wechsel groß → klein, 2.0.4 / 2.1.0 | – | 2,8 s / 2,7 s |
| Speichern-Knopf (alle Einstellungen), 2.0.4 | 91 ms | 91 ms |
| Speichern-Knopf, 2.1.0 (eine Transaktion) | 3 ms | 2,5 ms |
| Hell/Dunkel umschalten, 2.0.4 / 2.1.0 | 1,5 ms / 1,7 ms | 1,5 ms / 1,9 ms |
| Erneuter Voll-Import aller Spieler, ohne / mit Profil | – | 10,0 s / 9,6 s |

Ergebnis: Das Profil macht Öffnen, Wechsel und Importe nicht langsamer (die
Unterschiede liegen in der Messschwankung). Der Speichern-Knopf ist rund 30-mal
schneller, weil er nicht mehr nach jedem einzelnen Wert `config.ini`
synchronisiert, sondern eine Transaktion schreibt. Beim Öffnen großer Spielstände kostet fast die ganze
Zeit das Laden der Spieler und Bewertungen (35.000 Spieler, 420.000
Bewertungen). Das Profil wird deshalb zuerst geladen und mit
`AppContext::profileChanged` gemeldet, bevor die Spieler kommen. Wer das
Öffnen großer Spielstände weiter beschleunigen will, muss beim Laden der
Spieler ansetzen (eigenes Paket).

## Umstellung von 2.0.4

* **Bestehender Spielstand** (Datei existiert, noch kein Profil): Beim ersten
  Öffnen übernimmt er die Einstellungen, die bis 2.0.4 alle Spielstände
  geteilt haben: alle Profil-Abschnitte aus `config.ini`, die gemeinsame
  `definitions*.json` der FM-Version (eigene Rollen und Taktiken) und das
  Logo/die Flagge aus `assets/` (`club_logo_path`, `national_flag_path`).
  Danach ist er unabhängig.
* **Neuer Spielstand:** startet mit den Standardwerten, wahlweise als Kopie
  des Profils eines anderen Spielstands (Assistent „Neuer Spielstand“,
  Feld „Einstellungen“). Kopiert werden Gewichte, Farben, Optionen, Rollen,
  Taktiken und Logo, nicht Verein, Spieler und Favoriten. Ein Quell-Spielstand
  ohne Profil (noch nie mit 2.1.0 geöffnet) wird dabei nicht verändert.
* `config.ini` behält ihre Abschnitte aus 2.0.x unverändert als Quelle für
  noch nicht geöffnete Spielstände, ebenso die alten `definitions*.json`
  und Logo-Dateien. Ein Zurück auf 2.0.4 bleibt damit möglich (2.0.4 ignoriert
  die neuen Tabellen).
* Neuere mitgelieferte Rollen-Daten (`data_revision`) werden je Spielstand
  beim Öffnen eingespielt (`Definitions::applyShippedUpdate`, eigene Taktiken
  bleiben). Der Stand davor landet als
  `backups/<Spielstand>_definitions_<version>_vor_update_<Zeit>.json`.

## Speichern und Laden

* **Speichern-Knopf der Einstellungen:** `AppConfig::Transaction` öffnet eine
  Transaktion auf dem Spielstand. Vereins-Einstellungen (`database().setSetting`)
  und Profil-Werte laufen darin zusammen und werden mit `commit()` gemeinsam
  geschrieben. Scheitert irgendein Schritt, wird alles zurückgerollt, auch im
  Speicher. Die Seite zeigt dann eine Fehlermeldung und lädt den gespeicherten
  Stand neu. Ein halb gespeichertes Profil gibt es nicht (Test
  `failedSaveWritesNothing`).
* **Einzelne Setter** (z. B. Hell/Dunkel in der Kopfzeile) schreiben sofort und
  nur bei echter Änderung. Sie geben `false` zurück, Details in
  `config().lastError()`.
* **Laden:** `AppContext::openDatabase()` hängt das Profil des Spielstands an,
  lädt Rollen/Taktiken und meldet `profileChanged()` (Farben und Modus werden
  über `ThemeManager::reload()` neu angewendet), dann die Spieler. Scheitert
  das Profil, bleibt der bisherige Spielstand mit seinem Profil aktiv.
* **Abnahme:** `test_profileswitch` legt einen Datenordner wie 2.0.4 an
  (getunte `config.ini`, eigene Taktik, Logo-Datei), öffnet ihn, legt einen
  zweiten Spielstand mit anderen Gewichten, Farben, Taktiken an, wechselt
  A → B → A → B, startet neu und prüft dasselbe noch einmal.

## Für spätere Pakete

* **Neue Einstellung, die zum Spielstand gehört:** Abschnitt in
  `isProfileSection()` (AppConfig.cpp) eintragen und den Standardwert in
  `profileDefaults()`. App-weite Einstellungen bleiben außerhalb der Liste.
* **Taktiken pro Spielstand bearbeiten/löschen:** `definitions().setRoot()` +
  `context.saveDefinitions()` schreibt ins Profil. Damit Updates gelöschte
  Vorlagen nicht zurückbringen, gelöschte mitgelieferte Taktiken im Dokument
  vermerken (z. B. eine Liste neben `retired_tactics`) und in
  `Definitions::applyShippedUpdate` beachten.
* **Eigene Farbvorlagen (umgesetzt):** app-weit in
  `<Datenordner>/color_templates.json` (`ColorTemplateStore`, atomar per
  `QSaveFile` geschrieben, Installer und Updates fassen den Datenordner nicht
  an). Jede Vorlage hält alle 14 Farben, einen Namen und optional eine Liga
  (leer = „Custom“). Die Id `user:<uuid>` steht als `ThemeSettings/theme_preset`
  im Profil. Anwenden setzt die Farben auf der Design-Seite, der
  Speichern-Knopf schreibt sie über die `AppConfig::Transaction` ins Profil.
  Löschen einer Vorlage lässt Spielstände, die sie nutzen, unverändert.
* **Vereinskopf (umgesetzt):** `ClubHeaderBar` zeichnet den Block in den
  Vereinsfarben; Farben und Kontrast kommen aus `clubHeaderPalette()`
  (`core/ThemeColors`). Das Wappen holt der Kopf ausschließlich über
  `AppContext::headerCrest()`. Weitere Quellen (z. B. der FM-Graphics-Ordner)
  docken dort an, ohne den Kopf zu ändern.
* **Formatänderungen:** `Profile/format` hochzählen und die Umstellung in
  `AppConfig::attachProfile` ergänzen.
