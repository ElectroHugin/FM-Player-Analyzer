# Universal DWRS profiles: implementation design

Status: core foundations under development. The existing application evaluator
remains active until all consumers and migrations are integrated and verified.

## Calculation contract

`RatingCurve` stores a finite nonnegative total weight and 20 dimensionless,
nondecreasing shape values with endpoints 0 and 1, plus a finite baseline.
Contribution is baseline plus weight times shape. Disabling sets weight to zero
and retains shape and baseline for later editing. The baseline cancels during
normalization; storing it allows all 20 contribution points to be edited without
silently shifting other points. No role multiplier or weight is applied twice.

`evaluateCurves` returns an explicit status and optional unrounded normalized
score. A zero denominator is unavailable, not zero. Active missing attributes
produce unavailable rather than a fabricated point estimate. An inclusive range
uses the arithmetic mean of the contributions at each possible integer level:
this assumes a uniform distribution within the supplied bounds. The original
bounds remain the source data. This assumption is not a probability of success.

Classic requires a separate versioned compatibility policy: the 2.2.3 evaluator
uses midpoint attributes, zero for missing values, grouped arithmetic and
round-half-to-even. Missing values can produce negative legacy scores. Exact
Classic parity takes precedence over silently clipping or changing that behavior.
The UI must identify legacy behavior. Nonlinear profiles never use this missing
value policy. The existing engine is retained while golden parity is secured.

## Profile format

Exchange type `fm-dwrs-profile`, version 1. Metadata includes stable ID, personal
name, description, origin, source version, source URLs and compatible games.
Calculation version is independent of file format version and game version.
Full attribute definitions contain weight and all 20 shape values. Unknown
fields/formats, malformed types and invalid curves fail explicitly; no content
from an import is executed. All profile metadata and rules round-trip as JSON.

Position and role overrides independently replace only explicitly specified
weight, shape or role factor. Base -> position -> role is evaluated once.
Clearing a field restores inheritance. Each effective field reports its origin.
Role IDs use game/phase/role; explicit rolePositions assigns exactly one parent
position. Multi-position roles must have a deterministic documented parent in
the profile, rather than accumulating all eligible position modifiers.

The formula revision is SHA-256 of the canonical calculation fields. Rename,
description and source annotation changes do not alter the formula revision.
Role-definition snapshots must be included separately in the calculation/cache
identity, alongside calculation version, game and profile revision. Full-profile
metadata revisions and formula revisions must not be confused.

## Integration gates still to implement

- Snapshot all Classic group parameters and goalkeeper basis, including custom
  weights. Secure all-role FM24/FM27 golden parity before switching the engine.
- Register all visible attributes through HTML/live import, typed player arrays,
  SQLite migrations and export; append indexes without reordering existing ones.
- Library persistence uses atomic replacement and explicit conflict choices;
  keep immutable shipped originals and revisions referenced by history.
- Save selection and one-time migration decision belong to each save. Back up
  using SQLite's online backup path so committed WAL data is included, then
  migrate in a transaction with an idempotent completion marker.
- Recalculation works against immutable inputs and a generation/revision token.
  Reject superseded completions, and publish a consistent set of results once.
- Historical rows without proven formula provenance stay unknown. Never relabel
  them with the current profile. Store future attributes and exact rules with
  each normal history entry; formula changes begin a new section without a
  player-development delta across the boundary.
- Editor drafts and preview calculations are independent copies; cancelling
  never writes settings, player data or history. Saving and activation differ.
- Restore the pre-migration backup to return to 2.2.3; do not promise backward
  schema compatibility before the migration design is validated.

## Research preparation

See `harvestgreen-comparison.md` and its JSON companion for all 33 measurements,
source-cell references, fitted curves and exclusion reasons. The user selected
the 4 pp whole-curve cutoff on 2026-11-09. Eighteen visible attributes are active.
The cutoff is a modelling decision, not an estimate of statistical uncertainty.
