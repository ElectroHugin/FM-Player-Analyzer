#pragma once

// Turns an FM27 save (core/Fm27Save) into an import table in the FM View
// Exporter's column layout, so the regular import pipeline takes it over.
//
// This is the only place where the visibility rules live. With attribute
// masking on (the default) a player is imported only when the user knows him
// the way the game shows it:
//   - players of the user's own club: exact values;
//   - world reputation of at least kFamousReputation: exact values (the game
//     shows such players fully without a scout report);
//   - the user's scouting knowledge (fog_of_war) of at least
//     kFullKnowledge: exact values;
//   - knowledge from kPartialKnowledge up: partly as ranges. The game does not
//     store its ranges; the analyzer draws its own, once per player and
//     attribute (stable across imports), with the true value at a random place
//     inside, never in the middle by rule. Fewer ranges the more is known.
// Everyone else is left out. Hidden values (ability, potential, consistency,
// injury proneness, important matches, dirtiness, versatility) are never put
// into the table, whatever the setting.
//
// With masking off the user plays with real data, like the game's own option:
// every player with a club, exact values; hidden values stay out.

#include "ImportTable.h"

#include <QHash>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>

namespace fm {

namespace fm27save {
struct SaveData;
struct SavePlayer;
} // namespace fm27save

namespace Fm27SaveImport {

inline constexpr int kFamousReputation = 8000;
inline constexpr int kFullKnowledge = 78;
inline constexpr int kPartialKnowledge = 56;

struct Options {
    bool attributeMasking = true;
};

// How the user knows a player (the reason he is imported, or not).
enum class Knowledge { None, OwnClub, Famous, Scouted, PartlyScouted, TooLittle };

Knowledge knowledgeOf(const fm27save::SavePlayer &player, const fm27save::SaveData &save,
                      quint32 ownClubUid);

// Builds the table; fails with FileOpen/Other errors and a message.
bool buildTable(const QString &savePath, const Options &options, ImportTable *out,
                ImportError *error, const std::function<void(int, int)> &progress = {});

// Same on already read save data (tests).
void buildTable(const fm27save::SaveData &save, const Options &options, ImportTable *out);

// The app-wide masking switch (set by the app from the save profile before an
// import; read on the worker thread).
void setAttributeMasking(bool enabled);
bool attributeMasking();

// --- Exposed for tests ---------------------------------------------------------

// FM's position text ("D (RC), WB (R)") from the 15 position ratings.
QString positionText(const std::array<uint8_t, 15> &ratings);
// FM's foot word for a 1-20 strength.
QString footText(int strength);
// Nation code for the save's nation id ("" when unknown).
QString nationCode(int nationId);
// The analyzer's own range for one attribute: [lo, hi] containing value,
// width 0 = exact. Deterministic per (uniqueId, attribute).
std::pair<int, int> drawRange(quint32 uniqueId, int attribute, int value, int width);
// How many of the 47 visible attributes show as ranges at this knowledge.
int rangedAttributeCount(int knowledge);

} // namespace Fm27SaveImport

} // namespace fm
