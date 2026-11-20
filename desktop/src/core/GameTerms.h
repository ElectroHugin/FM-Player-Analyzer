#pragma once

#include <QString>

namespace fm {

// Game terms (player values, attribute names, positions) in the UI language,
// worded as that language version of FM words them (from the glossary, see
// GameTermsData.h). The app keeps working with the English texts; these are
// for display only. English UI, unknown texts and languages without a term
// return the English text unchanged. The UI language is
// Definitions::displayLanguage().
namespace GameTerms {

// category: a glossary category ("personality", "foot_strength",
// "preferred_foot", "playing_time", "media_handling", "registration", ...).
QString text(const QString &category, const QString &english);

// Attribute name as the app stores it ("Teamwork", "One vs One").
QString attribute(const QString &name);

// Position string as stored ("AM (RL), ST (C)", "D/WB (R)") with the game's
// position and side codes ("OM (RL), ST (Z)").
QString positions(const QString &positionRaw);

} // namespace GameTerms
} // namespace fm
