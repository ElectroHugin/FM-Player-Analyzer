#include "Freshness.h"

#include <algorithm>

namespace fm {

namespace Freshness {

int uploadsSinceSeen(const Player &player, int currentCounter)
{
    // Before any upload is tracked nothing can be stale.
    if (currentCounter <= 0)
        return 0;
    // A never-stamped row (imported before tracking began) counts as last seen
    // at upload 0: it ages with every tracked upload it is missing from, so a
    // player who vanished before tracking started eventually turns stale /
    // auto-retired instead of looking fresh forever. The counter starts at 0
    // when tracking begins, so this never floods an existing database at once.
    const int lastSeen = std::max(player.lastSeenUpdate, 0);
    const int delta = currentCounter - lastSeen;
    return delta > 0 ? delta : 0;
}

bool isStale(const Player &player, int currentCounter, int staleAfterUploads)
{
    if (staleAfterUploads < 1)
        return false;
    return uploadsSinceSeen(player, currentCounter) >= staleAfterUploads;
}

bool isRetired(const Player &player, int currentCounter, int retirementAge,
               int staleAfterUploads, const QString &userClub)
{
    if (player.age < retirementAge)
        return false;
    if (!userClub.isEmpty() && player.club == userClub)
        return false;
    return isStale(player, currentCounter, staleAfterUploads);
}

} // namespace Freshness

} // namespace fm
