#include "Registration.h"

namespace fm {

namespace Registration {

QString leagueRulesKey(LeagueRules rules)
{
    switch (rules) {
    case LeagueRules::PremierLeague:
        return QStringLiteral("premier_league");
    case LeagueRules::None:
        break;
    }
    return QStringLiteral("none");
}

LeagueRules leagueRulesFromKey(const QString &key)
{
    if (key == QLatin1String("premier_league"))
        return LeagueRules::PremierLeague;
    return LeagueRules::None;
}

QList<LeagueRules> leagueRulesFor(const QString &fmVersionId)
{
    if (fmVersionId == QLatin1String("fm24"))
        return {LeagueRules::PremierLeague};
    return {};
}

bool uefaRulesFor(const QString &fmVersionId)
{
    return fmVersionId == QLatin1String("fm24");
}

U21Status u21StatusByAge(int age)
{
    if (age <= 0 || age == 21)
        return U21Status::Uncertain;
    return age <= 20 ? U21Status::Yes : U21Status::No;
}

U21Status u21Status(const Player &player)
{
    switch (player.registration.u21) {
    case PlayerRegistration::U21::Yes:
        return U21Status::Yes;
    case PlayerRegistration::U21::No:
        return U21Status::No;
    case PlayerRegistration::U21::Auto:
        break;
    }
    return u21StatusByAge(player.age);
}

bool isU21(const Player &player)
{
    return u21Status(player) == U21Status::Yes;
}

bool isHomeGrown(const Player &player)
{
    return player.registration.homeGrown || player.registration.clubTrained;
}

bool isClubTrained(const Player &player)
{
    return player.registration.clubTrained;
}

bool isListBEligible(const Player &player)
{
    return isU21(player) && isClubTrained(player);
}

void normalize(PlayerRegistration &registration)
{
    if (registration.clubTrained)
        registration.homeGrown = true;
}

} // namespace Registration

} // namespace fm
