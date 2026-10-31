# Testnetz

Zwei Ebenen sichern die Rechenlogik ab. Die erste läuft automatisch, die zweite
braucht echte Spielstände und läuft deshalb von Hand vor jedem Merge auf `main`.

## 1 · Unit-Tests (automatisch in CI)

`ctest` führt alle Qt-Test-Suiten unter `src/tests/` aus. Die CI macht das bei
jedem Push auf Windows und Linux (`.github/workflows/ci.yml`, `linux.yml`), der
Release-Workflow noch einmal vor dem Release.

```
cmake --preset linux-release      # bzw. msvc-debug unter Windows
cmake --build --preset linux-release
ctest --preset linux-release
```

Abgedeckte Kernmodule (Auszug):

| Modul | Test |
|---|---|
| DWRS-Berechnung | `test_dwrs` |
| Best XI / B-Elf | `test_squadbuilder`, `test_roleratings` |
| Standardrollen-Zuweisung (additiv) | `test_roleassignment` |
| DWRS-Neuberechnung, 1-%-Schwelle, Cache-Patch | `test_ratingsupdater`, `test_roleratings` |
| Lückenanalyse (Verdrängung, falsche Seite, Abfall, leere Slots) | `test_gapanalysis` |
| Taktik-Explorer (Abdeckung, Tiefe, Median, Rangfolge) | `test_tacticexplorer` |
| Talentwert | `test_talentengine` |
| Import CSV/HTML | `test_csvimport`, `test_htmlimporter` |
| Spielfeld-Geometrie je Version | `test_pitchgeometry` |

Die Tests nutzen `legacy/config/definitions.json` als feste Datengrundlage
(FM24). Neue Versionsprofile (FM27) bekommen eigene Testfälle, sobald ihre
Rollen und Formationen feststehen.

## 2 · Golden-Master-Vergleich (von Hand, vor jedem Merge auf `main`)

Vergleicht die C++-Ergebnisse Wert für Wert mit der alten Python-Version.
Braucht einen echten Legacy-Spielstand und läuft deshalb nicht in CI.
**Nur auf einer Kopie arbeiten**, echte Spielstände nie schreibend anfassen
(zum Testen eignet sich `brighton.db`).

1. Referenz aus der Python-Version erzeugen (im Ordner `legacy/`):
   ```
   python tools/export_golden.py <db_name> <out>
   python tools/export_golden_squad.py <db_name> <out>
   ```
2. Spielstand in das neue Format überführen:
   ```
   fmmigrate legacy/databases/<db_name>.db <out>/migrated.db
   ```
3. Vergleichen:
   ```
   fmgolden <out>/migrated.db legacy/config/config.ini legacy/config/definitions.json \
            <out>/dwrs_<db_name>.csv <out>/talent_<db_name>.csv
   fmsquad  <out>/migrated.db legacy/config/config.ini legacy/config/definitions.json \
            <out>/squad_<db_name>.json
   ```
   Beide Werkzeuge geben die Zahl der Abweichungen aus.

Bewusste Abweichungen von der Legacy-Version (Leih-Logik, additive
Rollenzuweisung) können hier als Unterschied auftauchen. Sie sind gewollt und
werden nicht „repariert“.
