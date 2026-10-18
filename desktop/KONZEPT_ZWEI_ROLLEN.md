# Konzept: zwei Rollen pro Slot (FM27-Schritt 3)

Stand: 2026-10-18 · FM27-Plan in [IDEEN.md](IDEEN.md).
Nur Konzept, gebaut wird erst, wenn die FM27-Rollenliste feststeht.

## Ausgangslage

Seit FM26 hat jeder Feldspieler eine Rolle **mit Ball** und eine **ohne Ball**,
dazu eigene Formationen für beide Phasen. Duties (Defend/Support/Attack) gibt es
nicht mehr. Unsere App kennt pro Taktik-Slot genau eine Rolle (`tactic_roles`:
Slot → Rolle) und leitet daraus alles ab: DWRS, Best XI, Kaufberatung,
Taktik-Explorer, Trainingsberater.

Festgelegt (Edmund, 2026-10-10):

- Bewertung eines Slots = gewichteter Mittelwert aus DWRS mit Ball und DWRS ohne
  Ball. Anteil einstellbar, **Standard 50 %**.
- Maßgeblich für die Position ist die Formation **ohne Ball** (Grundordnung).

## Kernidee: Rollen bleiben einzeln, nur der Slot kombiniert

Eine Rolle mit Ball und eine Rolle ohne Ball sind jede für sich ganz normale
Rollen mit Schlüssel- und Zusatzattributen. Der DWRS wird also **weiter pro
Rolle** gerechnet und gespeichert. Erst der Slot einer Taktik verbindet zwei
Rollen zu einem Wert.

Daraus folgt der wichtigste Vorteil: **Die Datenbank bleibt, wie sie ist.**
`player_roles`, `dwrs_history` und `dwrs_latest` speichern weiter
(Spieler, Rolle, Wert). Es gibt keine Schema-Migration und keine Gefahr für
bestehende Spielstände.

## 1. Datenformat `definitions_<id>.json`

Rollen bekommen eine Phase:

```json
"player_roles": {
  "Defense": {
    "OCB": {"name": "Overlapping Centre-Back", "phase": "ip"},
    "STP": {"name": "Stopper", "phase": "oop"}
  }
}
```

- `phase`: `ip` (mit Ball), `oop` (ohne Ball) oder `both`. Fehlt das Feld oder
  ist der Wert ein reiner Text wie in FM24, gilt `both`. Damit liest der Code die
  FM24-Datei unverändert.
- Rollenkürzel müssen über beide Phasen eindeutig sein. Haben zwei Phasen eine
  Rolle gleichen Namens, bekommt sie ein Kürzel mit Suffix (`-IP`/`-OOP`).

Taktiken bekommen pro Slot zwei Rollen:

```json
"tactic_roles": {
  "4-3-3 Pressing": {
    "GK": "SK",
    "DL": {"oop": "FB-OOP", "ip": "WB-IP", "ipSlot": "WBL"},
    "DC": {"oop": "STP", "ip": "BPD"}
  }
}
```

- Der Slot-Schlüssel ist die Position **ohne Ball** (`DL`). Er entscheidet, wer
  den Slot spielen darf.
- `ipSlot` (optional) ist die Position mit Ball, nur für die Anzeige auf dem
  Spielfeld. Fehlt sie, gilt der Slot-Schlüssel.
- Ein reiner Text (`"GK": "SK"`) heißt: dieselbe Rolle in beiden Phasen. So
  bleiben alle FM24-Taktiken gültig.

## 2. Bewertung eines Slots

```
Slot-DWRS = a · DWRS(Rolle mit Ball) + (1 − a) · DWRS(Rolle ohne Ball)
```

- `a` ist der Anteil „mit Ball“, Standard 0,5. Gespeichert als Setting pro
  Spielstand (`phase_weight_ip`, 0–100). Eine Einstellung pro Taktik ist später
  möglich, aber nicht nötig.
- Bei einer Ein-Rollen-Taktik (FM24) sind beide Rollen gleich, also ist der Wert
  exakt der heutige. **FM24 verhält sich unverändert.**
- Ein Spieler braucht einen Wert in **beiden** Rollen, sonst kommt er für den
  Slot nicht in Frage. Das entspricht der heutigen Regel „ohne Wert kein Platz“.
  Offene Frage siehe unten.

## 3. Auswirkungen je Bereich

| Bereich | Änderung |
|---|---|
| `Definitions` | liest `phase` und die Zwei-Rollen-Slots; neuer Accessor `tacticSlots(tactic)` → Liste von `{slot, ipRole, oopRole, ipSlot}`; der alte `tacticRoles()` liefert für FM24 weiter Slot → Rolle |
| Rollenzuweisung | Position → Rollen liefert Rollen beider Phasen; die additive Auto-Zuweisung bleibt, sie weist einfach mehr Rollen zu |
| `DwrsEngine`, `RatingsUpdater` | unverändert (pro Rolle) |
| `SquadBuilder` | Kandidatenwert pro Slot = kombinierter Wert statt Einzelwert; Eignung über die Position ohne Ball; `primaryRole` (fest angepinnte Rolle) bezieht sich auf die Rolle ohne Ball |
| Best XI, B-Elf, Kaufberatung, Registrierung | erben die Änderung über `SquadBuilder`; Anzeige zeigt beide Rollen („BPD / STP“) |
| Taktik-Explorer | rechnet über `SquadBuilder`, Anzeige wie oben |
| Trainingsberater | Ziel ist das Rollenpaar; Gewinn eines Trainingsschwerpunkts = a · Gewinn(mit Ball) + (1 − a) · Gewinn(ohne Ball). `training_roles.role` speichert das Paar als `ip|oop`, kein neues Schema |
| Taktik-Editor | pro Slot zwei Auswahlfelder (ohne/mit Ball) plus optional die Position mit Ball; bei FM24 bleibt ein Feld |
| Rollen-Editor | statt Duty die Phase (`ip`/`oop`/`both`); bei FM24 bleibt Duty |
| Spielfeld-Grafik | zeigt die Grundordnung ohne Ball; Rolle mit Ball als zweite Zeile |
| Einstellungen | Regler „Anteil mit Ball“ (Standard 50 %), einklappbarer Erklärtext |

## 4. Reihenfolge der Umsetzung

1. `Definitions`: Phase und Zwei-Rollen-Slots lesen, FM24-Format unverändert; Tests.
2. `SquadBuilder` und Trainingsberater: kombinierter Wert, mit Test „FM24 identisch“
   (Golden-Vergleich `fmsquad`).
3. Oberfläche: Taktik-Editor, Rollen-Editor, Anzeige in Best XI und Spielfeld,
   Regler in den Einstellungen.
4. Mit der echten FM27-Rollenliste `definitions_fm27.json` füllen.

## Offene Fragen (erst mit der FM27-Rollenliste zu klären)

1. **Fehlt eine der beiden Rollen beim Spieler:** Slot sperren (Vorschlag) oder
   nur mit der vorhandenen Rolle rechnen?
2. **Torwart:** hat er in FM27 ebenfalls zwei Rollen, oder bleibt es bei einer?
3. **Positionen mit Ball:** Reicht die Anzeige, oder soll die Position mit Ball
   auch die Eignung beeinflussen (z. B. ein Innenverteidiger, der mit Ball ins
   Mittelfeld rückt)?
