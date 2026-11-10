# Harvestgreen profile: approved preparation rule

Status: user approved a 4 percentage-point cutoff on 2026-11-09. Integration and release validation remain pending.

Source: harvestgreen22 workbook dated 2026-11-03, Sheet1, Tables 1–3. Values below are percentage-point changes from attribute 12; the four knots are 1 / 6 / 12 / 18. Source SHA-256: `51f578da6b282969d998875f87cc391e63cf6df9777c39fce59371afd4cb08c6`.

Approved rule: fit all four observations with equal-weight isotonic regression, subtract the fitted first value, then disable the entire attribute when the fitted 1→18 total is at most 4 percentage points. Keep the entire fitted curve otherwise. This is an explicit modelling choice, not a statistically validated threshold. It does not clip individual negative segments and retain every positive fluctuation.

Interpolate retained curves linearly between knots and hold them constant at 18–20. Total weight is the fitted endpoint difference; the normalized shape carries no second weight. Role factors 1.5 / 1.2 / 1.0 remain separate DWRS choices. Hidden attributes are excluded regardless of magnitude. Goalkeepers retain the separate Classic basis.

| Attribute | Original 1 / 6 / 12 / 18 | Isotonic, baseline removed | Approved weight | Treatment |
|---|---:|---:|---:|---|
| Acceleration | -34 / -21.4 / 0 / 14.1 | 0 / 12.6 / 34 / 48.1 | 48.100 | Retained |
| Aggression | 0.2 / 1.4 / 0 / 1.2 | 0 / 0.5 / 0.5 / 1 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Agility | -8.4 / -3.2 / 0 / 0.2 | 0 / 5.2 / 8.4 / 8.6 | 8.600 | Retained |
| Anticipation | -19.8 / -8.8 / 0 / 4.8 | 0 / 11 / 19.8 / 24.6 | 24.600 | Retained |
| Balance | -14.2 / -8.2 / 0 / 0 | 0 / 6 / 14.2 / 14.2 | 14.200 | Retained |
| Bravery | -2.4 / -2.4 / 0 / 1.4 | 0 / 0 / 2.4 / 3.8 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Composure | -5 / -2 / 0 / -2 | 0 / 3 / 4 / 4 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Concentration | -13.4 / -5.6 / 0 / 4.4 | 0 / 7.8 / 13.4 / 17.8 | 17.800 | Retained |
| Consistency | -6.4 / -2 / 0 / 1.2 | 0 / 4.4 / 6.4 / 7.6 | 0.000 | Hidden attribute excluded |
| Corners | — | — | 0.000 | Not investigated |
| Crossing | -4.6 / -4 / 0 / 0.6 | 0 / 0.6 / 4.6 / 5.2 | 5.200 | Retained |
| Decisions | -1.6 / -2.4 / 0 / 1 | 0 / 0 / 2 / 3 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Determination | -6 / 0 / 0 / 2.8 | 0 / 6 / 6 / 8.8 | 8.800 | Retained |
| Dribbling | -6.8 / -3.8 / 0 / 3.6 | 0 / 3 / 6.8 / 10.4 | 10.400 | Retained |
| Finishing | -7.8 / -4.8 / 0 / 2.2 | 0 / 3 / 7.8 / 10 | 10.000 | Retained |
| First Touch | 0.8 / 1.6 / 0 / -3.6 | 0 / 0 / 0 / 0 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Flair | -0.6 / -1.6 / 0 / 1.8 | 0 / 0 / 1.1 / 2.9 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Free Kick Taking | — | — | 0.000 | Not investigated |
| Heading | -6.8 / -1.6 / 0 / 0 | 0 / 5.2 / 6.8 / 6.8 | 6.800 | Retained |
| Important Matches | -8.8 / -5 / 0 / 0 | 0 / 3.8 / 8.8 / 8.8 | 0.000 | Hidden attribute excluded |
| Jumping Reach | -18 / -12.2 / 0 / 15.4 | 0 / 5.8 / 18 / 33.4 | 33.400 | Retained |
| Leadership | — | — | 0.000 | Not investigated |
| Long Shots | -9.8 / -4.6 / 0 / 0 | 0 / 5.2 / 9.8 / 9.8 | 9.800 | Retained |
| Long Throws | — | — | 0.000 | Not investigated |
| Marking | -4.2 / -0.6 / 0 / -2.2 | 0 / 3.267 / 3.267 / 3.267 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Natural Fitness | -12.6 / -4 / 0 / 2.8 | 0 / 8.6 / 12.6 / 15.4 | 15.400 | Retained |
| Off the Ball | 1.6 / -2.4 / 0 / -6 | 0 / 0 / 0 / 0 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Pace | -40 / -26.8 / 0 / 19.4 | 0 / 13.2 / 40 / 59.4 | 59.400 | Retained |
| Passing | 2 / 1 / 0 / -0.8 | 0 / 0 / 0 / 0 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Penalty Taking | — | — | 0.000 | Not investigated |
| Positioning | 3.4 / 0.4 / 0 / 2.6 | 0 / 0 / 0 / 1.333 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Pressure | -15.6 / -2 / 0 / 5.2 | 0 / 13.6 / 15.6 / 20.8 | 0.000 | Hidden attribute excluded |
| Stamina | -29.4 / -17.6 / 0 / 14 | 0 / 11.8 / 29.4 / 43.4 | 43.400 | Retained |
| Strength | -18.8 / -8.8 / 0 / 4.6 | 0 / 10 / 18.8 / 23.4 | 23.400 | Retained |
| Tackling | -1 / 1.6 / 0 / -5 | 0 / 0 / 0 / 0 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Teamwork | — | — | 0.000 | Not investigated |
| Technique | -0.4 / -1.2 / 0 / -5.4 | 0 / 0 / 0 / 0 | 0.000 | Fitted total <= 4 pp (approved modelling cutoff) |
| Vision | -8.2 / -8.2 / 0 / -5.2 | 0 / 0 / 5.6 / 5.6 | 5.600 | Retained |
| Work Rate | -22 / -6.2 / 0 / 0 | 0 / 15.8 / 22 / 22 | 22.000 | Retained |

## Sensitivity and limitations

At the approved 4 pp cutoff, Crossing (5.2) and Vision (5.6) are retained: 18 of 30 measured visible attributes are active. At 6 pp both would be disabled; at 8 pp Heading (6.8) would additionally be disabled. All six unmeasured field attributes remain disabled and editable.

Vision illustrates why raw endpoint differences and fitted totals are different: [-8.2, -8.2, 0, -5.2] fits to [-8.2, -8.2, -2.6, -2.6], giving 5.6 rather than the raw 3.0. No individual match results are available to estimate reliable uncertainty or independence. The baseline at 12 is a shared reference, not an independent experiment.

Tables 9–10 are scaled FM27 data and are not used. Tables 11–12 are FM24 comparisons and do not replace Classic. The result is a relative rating contribution, never a win probability. The JSON companion preserves source cells, the unshifted fit, and all 20 proposed contributions.
