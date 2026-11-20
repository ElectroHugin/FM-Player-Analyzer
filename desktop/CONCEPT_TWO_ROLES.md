# Concept: two roles per slot (FM27 step 3)

As of 2026-10-18 · FM27 plan in [IDEAS.md](IDEAS.md).
Concept only; it gets built once the FM27 role list is settled.

## Starting point

Since FM26 every outfield player has one role **in possession** and one **out
of possession**, plus separate formations for both phases. Duties
(Defend/Support/Attack) no longer exist. Our app knows exactly one role per
tactic slot (`tactic_roles`: slot → role) and derives everything from it: DWRS,
Best XI, signing advice, tactic explorer, training advisor.

Decided (Edmund, 2026-10-10):

- A slot's rating = weighted mean of the in-possession DWRS and the
  out-of-possession DWRS. The share is adjustable, **default 50 %**.
- The **out-of-possession** formation (base shape) decides the position.

## Core idea: roles stay separate, only the slot combines them

An in-possession role and an out-of-possession role are each a normal role with
key and preferable attributes. The DWRS is therefore still calculated and stored
**per role**. Only a tactic's slot combines two roles into one value.

The main benefit follows from this: **the database stays as it is.**
`player_roles`, `dwrs_history` and `dwrs_latest` keep storing
(player, role, value). No schema migration, no risk for existing saves.

## 1. Data format `definitions_<id>.json`

Roles get a phase:

```json
"player_roles": {
  "Defense": {
    "OCB": {"name": "Overlapping Centre-Back", "phase": "ip"},
    "STP": {"name": "Stopper", "phase": "oop"}
  }
}
```

- `phase`: `ip` (in possession), `oop` (out of possession) or `both`. If the
  field is missing or the value is plain text as in FM24, `both` applies. This
  way the code reads the FM24 file unchanged.
- Role abbreviations must be unique across both phases. If both phases have a
  role with the same name, it gets an abbreviation with a suffix
  (`-IP`/`-OOP`).

Tactics get two roles per slot:

```json
"tactic_roles": {
  "4-3-3 Pressing": {
    "GK": "SK",
    "DL": {"oop": "FB-OOP", "ip": "WB-IP", "ipSlot": "WBL"},
    "DC": {"oop": "STP", "ip": "BPD"}
  }
}
```

- The slot key is the **out-of-possession** position (`DL`). It decides who may
  play the slot.
- `ipSlot` (optional) is the in-possession position, for display on the pitch
  only. If missing, the slot key applies.
- Plain text (`"GK": "SK"`) means the same role in both phases. All FM24 tactics
  stay valid.

## 2. Rating a slot

```
Slot DWRS = a · DWRS(in-possession role) + (1 − a) · DWRS(out-of-possession role)
```

- `a` is the in-possession share, default 0.5. Stored as a setting per save
  (`phase_weight_ip`, 0–100). A setting per tactic is possible later but not
  needed.
- For a one-role tactic (FM24) both roles are the same, so the value is exactly
  today's. **FM24 behaves unchanged.**
- A player needs a value in **both** roles, otherwise he is not considered for
  the slot. This matches today's rule "no value, no place". See the open
  questions below.

## 3. Impact per area

| Area | Change |
|---|---|
| `Definitions` | reads `phase` and the two-role slots; new accessor `tacticSlots(tactic)` → list of `{slot, ipRole, oopRole, ipSlot}`; the old `tacticRoles()` still returns slot → role for FM24 |
| Role assignment | position → roles returns roles of both phases; the additive auto assignment stays, it simply assigns more roles |
| `DwrsEngine`, `RatingsUpdater` | unchanged (per role) |
| `SquadBuilder` | candidate value per slot = combined value instead of a single value; eligibility via the out-of-possession position; `primaryRole` (pinned role) refers to the out-of-possession role |
| Best XI, B team, signing advice, registration | inherit the change via `SquadBuilder`; the display shows both roles ("BPD / STP") |
| Tactic explorer | calculates via `SquadBuilder`, display as above |
| Training advisor | the target is the role pair; gain of a training focus = a · gain(in possession) + (1 − a) · gain(out of possession). `training_roles.role` stores the pair as `ip|oop`, no new schema |
| Tactic editor | two selectors per slot (out of / in possession) plus the optional in-possession position; FM24 keeps one selector |
| Role editor | phase (`ip`/`oop`/`both`) instead of duty; FM24 keeps duty |
| Pitch graphic | shows the out-of-possession base shape; the in-possession role as a second line |
| Settings | slider "in-possession share" (default 50 %), collapsible explanation |

## 4. Order of implementation

1. `Definitions`: read phase and two-role slots, FM24 format unchanged; tests.
2. `SquadBuilder` and training advisor: combined value, with an "FM24 identical"
   test (golden comparison `fmsquad`).
3. UI: tactic editor, role editor, display in Best XI and on the pitch, slider
   in the settings.
4. Fill `definitions_fm27.json` with the real FM27 role list.

## Open questions (to settle with the FM27 role list)

1. **A player lacks one of the two roles:** lock the slot (proposal) or calculate
   with the available role only?
2. **Goalkeeper:** does he also have two roles in FM27, or does he keep one?
3. **In-possession positions:** is the display enough, or should the
   in-possession position also affect eligibility (e.g. a centre-back who steps
   into midfield in possession)?
