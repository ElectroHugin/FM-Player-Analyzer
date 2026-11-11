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
- Outfield attributes only; goalkeeper roles and Classic have no role part.

## Missing attributes

- A missing attribute leaves numerator and denominator.
- Below 70 % known weight (`CurveScore.coverage`) there is no rating.
- Classic keeps its 2.2.3 behaviour.

## Colour tiers

- Six tiers: Poor, Common (no fill), Uncommon, Rare, Epic, Legendary.
- Thresholds belong to the profile (`tierThresholds.field` / `.goalkeeper`, not part
  of the revision). Defaults: curves 55/65/72/78/84, Classic 48/55/61/68/76,
  goalkeepers 48/55/61/68/76.
- Colours are an app setting (`dwrs_color_mode`, `dwrs_tier_colors`); text colour is
  chosen for maximum contrast. The old rainbow scale remains as a mode.

## Display

- Curve profiles show one decimal, Classic whole numbers (`DwrsDisplay`).
- Decimal separator follows the UI language.
