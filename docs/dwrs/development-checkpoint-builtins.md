# Built-in profiles and selection persistence checkpoint

Status: incomplete development branch, not `2.3.0-test.1` acceptance.

## Verified implementation

- `BuiltinRatingProfiles` constructs the editable Classic curve profile from the
  existing category distribution. Group counts, role factors and goalkeeper
  differences are preserved. Its compiled curve evaluation passes all 4,608
  frozen FM24/FM27 reference cases, including custom weights, ranges and missing
  values. The separate frozen Classic snapshot remains available for preserving
  the exact historical arithmetic basis during migration.
- The Harvestgreen profile reconstructs all measured visible curves from the
  reviewed four-point inputs, uses equal-weight isotonic regression, disables
  fitted totals <=4 percentage points, interpolates to 18 and plateaus to 20.
  There are 18 active base attributes, including Crossing and Vision. Hidden
  attributes never enter the visible profile. The archived comparison contains
  33 measurements plus six explicitly uninvestigated attributes; those six
  remain disabled and editable. Goalkeepers have explicit separate legacy rules.
- The two built-ins are bundled with both games' role definitions and the
  reviewed measurement table. The settings editor seeds them without replacing
  existing user edits or resurrecting deliberately deleted profiles. Their
  original definitions remain in the library for restoring existing profiles.
- Attribute evidence is separate metadata (`measured`, `noise-filtered`,
  `not-investigated`, `legacy`). It round-trips in JSON and does not change a
  formula revision. English/German tooltips distinguish these states; the editor
  shows the selected profile's source version. Older library JSON can omit the
  new optional evidence and role-policy fields without changing formula IDs.

## Calculation policy and inheritance

`curves-uniform-range-v1` keeps the expected contribution across each possible
integer value and returns unavailable for missing active attributes.
`classic-midpoint-v1` preserves the old midpoint and missing-as-zero policy,
including legacy negative scores and whole-number half-even rounding. Edited
nonlinear Classic curves use interpolation at the midpoint; at unknown value
zero the first increment is extrapolated. This explicit compatibility policy
must be identified in the finished UI. It is not used for Harvestgreen outfield
ratings. Per-role policy overrides preserve the separate goalkeeper basis.

Each role receives one explicit parent position: the first lexicographically
sorted eligible position from its game definitions. This mapping is stored in
the profile, never recomputed implicitly during rating. FM27 phases have separate
role identifiers. An advanced editor must expose and allow changing this mapping.

## Selection store

`RatingSelectionStore` is a tested core component, not yet wired into activation
buttons or the application's rating consumers. It stores a complete profile,
definition snapshot, compiled identity, generation token and migration decision
in the save's existing transactional document store. Existing-save initialization
requires a Classic profile and a frozen legacy basis. Existing FM27 saves start
with a pending decision; repeat initialization preserves the original selection
and custom weights. Keeping it records a one-time decision.

Activation validates/compiles before committing, checks the expected generation,
rejects a mismatched game, and atomically archives the new calculation basis with
the selection. Superseded writers and corrupt state are rejected. Formula changes
append a separate switch record with UTC action time and an explicitly supplied
game date; renaming does not create a formula-change segment. These records are
not player-development samples. Previous bases remain archived by compiled ID.
No old historical rating is relabeled or recalculated by this component.

Tests use temporary databases. They cover repeated initialization, individual
weights, one-time choice, stale writes, metadata-only changes, preserved older
bases, corrupted state, and a database-triggered write failure proving rollback
of both selection and archive. No production database was migrated or modified.

## Remaining integration

The three pre-existing uncommitted AppContext/AppPaths changes were backed up and
preserved verbatim. Application startup must still initialize selections with
the correct new-save defaults or captured existing settings. The editor must
publish changes through the shared library service, show affected saves, and
offer explicit activation/preview and the migration decision. All rating
consumers and workers must switch together; no activation button is exposed as
working before that is true. Profile-wide undo/close protection, advanced rules,
same-snapshot squad/Best XI preview, history segmentation and complete attribute
history, performance checks, packaging and the complete prerelease remain open.

The existing save-document tables are reused; no additional irreversible schema
migration is introduced here. The schema-v17 backup/rollback requirements from
the earlier checkpoint still apply.
`msvc-release -Test`: full build and 49/49 CTest suites passed in the final run (11.12 seconds). Optional real-save FM27 testing remains unrun without a supplied test save.
