# DWRS editor implementation checkpoint

This is development work on `feature/2.3.0-dwrs-profiles`, not a complete
`2.3.0-test.1` release. The application still evaluates players with the legacy
engine. No production save has been opened or migrated for these checks.

## Implemented

- Settings now displays **DWRS Rating / DWRS-Bewertung**. The existing field and
  goalkeeper group weights, role multipliers, agreed playing-time weights and
  manual recalculation controls remain present with their existing behavior.
- A profile library editor in the same settings page supports creation, naming,
  saving a draft, saving as a new profile, deletion, restoring an original as a
  draft, discarding changes, and complete JSON import/export. Conflicting imports
  require an explicit new name and receive a new identity. Concurrent file edits
  are rejected instead of overwritten. Library writes are atomic.
- All 47 visible attributes are available even when absent from an imported
  sparse profile. Editing an absent attribute creates its curve; merely viewing
  it does not mutate the profile. Search supports localized labels and canonical
  names. Technical, mental, physical and goalkeeper filters are available.
- The curve editor supports dragging, keyboard navigation, 20 numeric inputs,
  weight, contribution/increment display, linear and plateau actions, reset and
  per-attribute undo/redo. Invalid decreases are rejected explicitly. Focus-only
  changes do not round stored values to the spinbox display precision.
- English translations and German source text cover the new controls and all
  attribute labels. Personal profile names remain unchanged. Theme colors drive
  the chart and controls; action rows wrap at narrower widths.

## Format refinement

Curves now preserve a finite `baseline` alongside weight and normalized shape:
`C(x) = baseline + weight * shape[x-1]`. This allows editing the first of all
20 points without silently shifting the others. The baseline cancels from the
normalized score, but remains available for absolute contribution display and
lossless JSON exchange. Overrides may inherit or replace the baseline separately.
Unreleased format-1 JSON without this field reads as baseline zero. Disabled
attributes retain their shape and baseline but contribute nothing.

## Verification

- Full Windows MSVC release compilation and all 47 CTest suites pass, including
  the focus-preservation regression test. The final rerun is recorded in the plan.
- New widget tests verify draft isolation, save, concurrent modification
  rejection, reset/discard behavior, 47-attribute availability and translation.
- Profile editor rendering is exercised at 800 and 1000 pixels in English and
  German, with both existing day and night themes, using temporary settings and
  profile files. These are isolated widget tests, not a complete application tour.
- Existing Classic reference, import, database and analysis suites remain in the
  full run. Optional real-save FM27 tests have not been supplied a save file.

## Remaining release gates

The editor is not the complete profile feature. Built-in Classic/goalkeeper
representation, built-in seeding, profile activation, affected-save reporting,
one-time migration choices and defaults still require integration. Advanced
position/role editing and origin controls, same-snapshot squad/Best XI preview,
full application consumer conversion, history provenance and segmentation,
worker stale-result protection, real-squad/performance acceptance, packaging and
prerelease workflow remain open. Undo/redo currently covers the selected
attribute only; profile-wide draft navigation and closing protection still need
completion. No prerelease artifact has been published.

## Manual check of this development slice

Use a temporary application data directory, not a production save. Open Settings
→ DWRS Rating, create a personal profile, select an attribute, edit its last point
and save. Reopen the profile to verify persistence. Change another point and
discard the draft: the saved curve must remain. Try a decrease: it must be rejected.
Export and import under a new name when prompted. Check the page in English and
German and switch day/night modes. Verify the existing group/role/playing-time
controls and recalculation are still present. Saving a profile does not activate
it for player ratings in this checkpoint.

## Kurzanleitung für diesen Entwicklungsstand

Nur mit einem temporären Datenverzeichnis prüfen. Unter Einstellungen →
DWRS-Bewertung ein persönliches Profil erstellen, ein Attribut bearbeiten und
das Profil speichern. Ungespeicherte Änderungen über „Entwurf verwerfen“
zurücknehmen. Fallende Kurven müssen abgelehnt werden. JSON-Export und -Import
unter einem neuen Namen prüfen. Deutsch/Englisch sowie helles/dunkles Theme
testen. Die bisherigen Gewichte, Rollenfaktoren, Spielzeitgewichte und die
Neuberechnung bleiben erreichbar. Ein gespeichertes Profil wird in diesem
Entwicklungsstand noch nicht für die Spielerbewertung aktiviert.

Numeric input presentation: the user requested at most two decimal places, with
redundant trailing zeros hidden. Original imported/internal precision is retained
until the user actually edits a value; merely focusing an input never rounds the
stored curve. JSON exchange retains the stored precision.
