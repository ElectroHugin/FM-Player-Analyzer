#include "PlayerStatus.h"

#include "Freshness.h"

namespace fm {

namespace PlayerStatus {

QString retiredClubTag()
{
    return QStringLiteral("Retired");
}

QString freeAgentClubTag()
{
    return QStringLiteral("FrA");
}

bool isRetiredClub(const Player &player)
{
    return player.club.trimmed().compare(retiredClubTag(), Qt::CaseInsensitive) == 0;
}

bool isRetired(const Player &player, const FreshnessContext &freshness)
{
    return isRetiredClub(player)
           || Freshness::isRetired(player, freshness.currentCounter, freshness.retirementAge,
                                   freshness.staleAfterUploads, freshness.userClub);
}

bool isNationalEligible(const Player &player, const QString &countryCode, int ageLimit)
{
    if (countryCode.isEmpty())
        return false;
    if (player.nationality != countryCode && player.secondNationality != countryCode)
        return false;
    if (ageLimit > 0 && ageLimit < 99 && (player.age <= 0 || player.age > ageLimit))
        return false;
    return true;
}

bool isAvailableForNation(const Player &player, const NationalCriteria &criteria)
{
    return isNationalEligible(player, criteria.countryCode, criteria.ageLimit)
           && !isRetired(player, criteria.freshness);
}

void applyDeparture(Player &player, const QString &destination)
{
    player.club = destination;
    player.transferStatus = false;
    player.loanStatus = false;
    player.newClub.clear();
}

} // namespace PlayerStatus

} // namespace fm
