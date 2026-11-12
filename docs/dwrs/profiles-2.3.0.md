# Rating profiles in 2.3.0

Short reference for what 2.3.0 adds on top of `dwrs-design.md`.

## Selection per save

- Every save stores exactly one rating profile (`RatingSelectionStore`).
- New FM27 save: `dwrs-fm27-harvestgreen22`. New FM24 save: `dwrs-classic-florin133`.
- Existing save: default weights map to Classic; changed weights become a personal
  profile `my-weights-<game>-<rev8>`. FM27 saves are asked once whether to switch.
- Switching recalculates once (`RatingsOrigin::Recalc`) and is recorded in the
  history, so development does not count the switch as progress.

## Role share

Curve profiles rate attributes in general. To keep roles apart, an optional role
part adds linear contributions for the role's key and preferable attributes:

- `ρ` = role share (0..0.6, FM27 profile 0.33), `M` = the plan's denominator.
- Role mass `R = ρ / (1 − ρ) · M`, unit `u = R / (1.5·|K| + |P|)`.
- Each key attribute adds `1.5·u·x/19`, each preferable one `u·x/19` (x = value − 1).
- Outfield attributes only for this part; Classic has no role part.

## FM27 goalkeepers

Own analysis of FM27 save statistics (goals prevented per shot on target, checked
on a second save), not harvestgreen22 data. Linear curves, base weights as shares:

| Weight | Attributes |
|---:|---|
| 12 | Anticipation, Handling, Reflexes, Positioning |
| 8 | Concentration, Communication, One vs One, Agility |
| 4 | Aerial Reach, Command of Area, Composure |
| 2 | Kicking, Throwing, Bravery, Decisions |
| 0 | Punching, Eccentricity, Rushing Out and all others |

Role part (`goalkeeperRoleShare`, FM27 profile 0.20): goalkeeper role lists are
almost identical, so only the differences count.

- Mark per role: key 1.5, preferable 1, else 0; Eccentricity is ignored.
- `d(r, a)` = mark of `r` minus the lowest mark among goalkeeper roles of the same phase.
- `D_max` = largest `Σ d(r, ·)` over all goalkeeper roles (4.5, BGK).
- Unit `u = ρ / (1 − ρ) · M / D_max`; each difference attribute adds `u·d·x/19`.
- NGK has no differences and uses the base part only.

## Missing attributes

- A missing attribute leaves numerator and denominator.
- Below 70 % known weight (`CurveScore.coverage`) there is no rating.
- Classic keeps its 2.2.3 behaviour.

## Colour schemes

- Colours are an app setting (Settings → DWRS colouring), not part of a profile.
  `tierThresholds` in older profile files is accepted and ignored.
- Built-in schemes: Original (rainbow, default), Rarity, Traffic light,
  Colour-blind friendly, Subtle (purple). Each band has a field and a goalkeeper
  threshold, a colour or no fill, and an optional blend width at the borders.
- Rarity: field 55/65/72/78/84, goalkeepers 48/55/61/69/78.
- Own schemes are saved app-wide in `dwrs_color_schemes.json`; the save stores the
  chosen id plus a copy. Text colour is chosen for maximum contrast.

## Display

- Curve profiles show one decimal, Classic whole numbers (`DwrsDisplay`).
- Decimal separator follows the UI language.
