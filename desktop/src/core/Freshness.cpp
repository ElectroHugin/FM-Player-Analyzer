#include "Freshness.h"

namespace fm {

namespace Freshness {

int uploadsSinceSeen(const Player &player, int currentCounter)
{
    // Before any upload is tracked, or for rows that were never stamped, treat
    // the data as fresh so legacy databases are not flooded with "stale" flags
    // until real tracking data has accumulated.
    if (currentCounter <= 0 || player.lastSeenUpdate <= 0)
        return 0;
    const int delta = currentCounter - player.lastSeenUpdate;
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
