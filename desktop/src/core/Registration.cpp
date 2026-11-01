#include "Registration.h"

#include "Constants.h"
#include "Definitions.h"

#include <QSet>

#include <algorithm>
#include <optional>

namespace fm {

namespace Registration {

namespace {

struct LeagueInfo {
    LeagueRules rules;
    const char *key;
    const char *name; // proper noun, not translated
    const char *cup;
};

const LeagueInfo kLeagues[] = {
    {LeagueRules::Bundesliga, "bundesliga", "Bundesliga", "DFB-Pokal"},
    {LeagueRules::PremierLeague, "premier_league", "Premier League", "FA Cup / League Cup"},
    {LeagueRules::LaLiga, "laliga", "LaLiga", "Copa del Rey"},
    {LeagueRules::SerieA, "serie_a", "Serie A", "Coppa Italia"},
    {LeagueRules::Ligue1, "ligue_1", "Ligue 1", "Coupe de France"},
    {LeagueRules::LigaPortugal, "liga_portugal", "Liga Portugal", "Taça de Portugal"},
    {LeagueRules::SwissSuperLeague, "swiss_super_league", "Super League (Schweiz)",
     "Schweizer Cup"},
    {LeagueRules::AustrianBundesliga, "austrian_bundesliga", "Bundesliga (Österreich)",
     "ÖFB-Cup"},
    {LeagueRules::RussianPremierLiga, "russian_premier_liga", "Russian Premier Liga",
     "Russian Cup"},
    {LeagueRules::UkrainianPremierLeague, "ukrainian_premier_league",
     "Ukrainian Premier League", "Ukrainian Cup"},
    {LeagueRules::SuperLig, "super_lig", "Süper Lig", "Türkiye Kupası"},
};

const LeagueInfo *infoOf(LeagueRules rules)
{
    for (const LeagueInfo &info : kLeagues) {
        if (info.rules == rules)
            return &info;
    }
    return nullptr;
}

} // namespace

QString leagueRulesKey(LeagueRules rules)
{
    const LeagueInfo *info = infoOf(rules);
    return info ? QString::fromLatin1(info->key) : QStringLiteral("none");
}

LeagueRules leagueRulesFromKey(const QString &key)
{
    for (const LeagueInfo &info : kLeagues) {
        if (key == QLatin1String(info.key))
            return info.rules;
    }
    return LeagueRules::None;
}

QString leagueDisplayName(LeagueRules rules)
{
    const LeagueInfo *info = infoOf(rules);
    return info ? QString::fromUtf8(info->name) : QString();
}

QString cupDisplayName(LeagueRules rules)
{
    const LeagueInfo *info = infoOf(rules);
    return info ? QString::fromUtf8(info->cup) : QString();
}

bool restrictsSquad(LeagueRules rules)
{
    switch (rules) {
    case LeagueRules::None:
    case LeagueRules::Bundesliga:
    case LeagueRules::Ligue1:
    case LeagueRules::AustrianBundesliga:
        return false; // 99 places, no quota the analyzer can check
    default:
        return true;
    }
}

// Only versions whose registry entry models the rules. fmDataVersion() falls
// back to the default version for unknown ids, so the id must match exactly.
static bool modelsRegistrationRules(const QString &fmVersionId)
{
    const FmDataVersion &version = fmDataVersion(fmVersionId);
    return version.id == fmVersionId && version.registrationRules;
}

QList<LeagueRules> leagueRulesFor(const QString &fmVersionId)
{
    if (!modelsRegistrationRules(fmVersionId))
        return {};
    if (fmVersionId == QLatin1String("fm24"))
        return {LeagueRules::Bundesliga, LeagueRules::PremierLeague};
    QList<LeagueRules> all;
    for (const LeagueInfo &info : kLeagues)
        all.append(info.rules);
    return all;
}

bool uefaRulesFor(const QString &fmVersionId)
{
    return modelsRegistrationRules(fmVersionId);
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

bool expireU21Override(PlayerRegistration &registration, int oldAge, int newAge)
{
    if (registration.u21 == PlayerRegistration::U21::Auto || newAge <= oldAge
        || u21StatusByAge(newAge) != U21Status::No)
        return false;
    registration.u21 = PlayerRegistration::U21::Auto;
    return true;
}

QString expiredU21SettingKey()
{
    return QStringLiteral("registration_u21_expired");
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

Quota quotaFor(Competition competition, const Settings &settings)
{
    Quota quota;
    if (competition == Competition::Uefa) {
        // List A: 25, at most 17 not locally trained, 4 places reserved for
        // club-trained players, at least two goalkeepers.
        quota.maxNonClubTrained = 21;
        quota.minGoalkeepers = std::max(2, settings.minGoalkeepers);
        quota.exemptKeepersCount = false;
        return quota;
    }
    // Size and the trained-in-country (home-grown) / club-trained minimums
    // become nested maximums: e.g. 25 places with at least 8 home-grown, 4 of
    // them club-trained = at most 17 non-home-grown and 21 non-club-trained.
    const auto set = [&quota](int size, int minHomeGrown, int minClubTrained, bool u21Exempt) {
        quota.maxSize = size;
        quota.maxNonHomeGrown = size - minHomeGrown;
        quota.maxNonClubTrained = size - minClubTrained;
        quota.u21Exempt = u21Exempt;
    };
    switch (settings.league) {
    case LeagueRules::PremierLeague:
        set(25, 8, 0, true); // U21 need no slot
        break;
    case LeagueRules::LaLiga:
        set(25, 0, 0, false); // non-EU limit and U19-EU exemption not modelled
        break;
    case LeagueRules::SerieA:
        set(25, 8, 4, false);
        break;
    case LeagueRules::LigaPortugal:
        set(68, 10, 0, false); // clubs with a B team; without one not documented
        break;
    case LeagueRules::SwissSuperLeague:
        set(25, 8, 0, false); // non-EU limit not modelled
        break;
    case LeagueRules::RussianPremierLiga:
        set(55, 8, 4, false); // foreigner limit and domestic U18 exemption not modelled
        break;
    case LeagueRules::UkrainianPremierLeague:
        set(25, 8, 4, true); // U21 need no slot
        break;
    case LeagueRules::SuperLig:
        set(28, 0, 0, false); // foreigner limits and domestic U21 exemption not modelled
        break;
    case LeagueRules::Bundesliga:
    case LeagueRules::Ligue1:
    case LeagueRules::AustrianBundesliga:
    case LeagueRules::None:
        // No list is built for these (see restrictsSquad); no quota applies.
        quota.maxNonHomeGrown = quota.maxSize;
        break;
    }
    quota.minGoalkeepers = settings.minGoalkeepers;
    return quota;
}

bool isExempt(const Player &player, Competition competition)
{
    return competition == Competition::Uefa ? isListBEligible(player) : isU21(player);
}

bool isExempt(const Player &player, Competition competition, const Quota &quota)
{
    if (competition == Competition::Uefa)
        return isListBEligible(player);
    return quota.u21Exempt && isU21(player);
}

Category category(const Player &player)
{
    if (isClubTrained(player))
        return Category::ClubTrained;
    return isHomeGrown(player) ? Category::HomeGrown : Category::NonHomeGrown;
}

std::vector<RankedPlayer> rankPool(const SquadBuilder &builder, const Definitions &definitions,
                                   const std::vector<const Player *> &pool,
                                   const QStringList &tactics, const RoleRatings &ratings)
{
    QHash<QString, Tier> tierByUid;
    const auto promote = [&tierByUid](const QString &uid, Tier tier) {
        const auto it = tierByUid.find(uid);
        if (it == tierByUid.end())
            tierByUid.insert(uid, tier);
        else if (tier < it.value())
            it.value() = tier;
    };
    for (const QString &tactic : tactics) {
        const SquadResult squad = builder.calculateSquadForTactic(pool, tactic, ratings);
        for (const XiCell &cell : squad.startingXi)
            if (cell.isFilled())
                promote(cell.playerUid, Tier::StartingXi);
        for (const XiCell &cell : squad.bTeam)
            if (cell.isFilled())
                promote(cell.playerUid, Tier::BTeam);
        for (const QString &uid : squad.depthPlayerUids)
            promote(uid, Tier::Depth);
    }

    std::vector<RankedPlayer> ranked;
    ranked.reserve(pool.size());
    for (const Player *p : pool)
        ranked.push_back({p, tierByUid.value(p->uid, Tier::Reserve),
                          SquadBuilder::bestDwrsForPlayer(*p, ratings)});
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const RankedPlayer &a, const RankedPlayer &b) {
                         if (a.tier != b.tier)
                             return a.tier < b.tier;
                         return a.bestDwrs > b.bestDwrs;
                     });
    return ranked;
}

Proposal propose(const std::vector<RankedPlayer> &ranked, Competition competition,
                 const Quota &quota)
{
    Proposal result;
    result.quota = quota;

    std::vector<const RankedPlayer *> candidates;
    for (const RankedPlayer &entry : ranked) {
        if (isExempt(*entry.player, competition, quota)) {
            result.exempt.push_back(entry);
            if (quota.exemptKeepersCount && entry.player->isGoalkeeper())
                ++result.goalkeepers;
        } else {
            candidates.push_back(&entry);
        }
    }

    const auto listedCount = [&result] { return static_cast<int>(result.listed.size()); };
    // Why `cat` does not fit right now, or nullopt when it does.
    const auto blocker = [&](Category cat) -> std::optional<LeftOutReason> {
        if (listedCount() >= quota.maxSize)
            return LeftOutReason::ListFull;
        if (cat == Category::NonHomeGrown && result.nonHomeGrown >= quota.maxNonHomeGrown)
            return LeftOutReason::NonHomeGrownFull;
        if (cat != Category::ClubTrained
            && result.nonHomeGrown + result.homeGrownOnly >= quota.maxNonClubTrained)
            return LeftOutReason::NonClubTrainedFull;
        return std::nullopt;
    };
    QSet<const Player *> taken;
    const auto take = [&](const RankedPlayer &entry, Category cat) {
        result.listed.push_back(entry);
        taken.insert(entry.player);
        switch (cat) {
        case Category::NonHomeGrown:
            ++result.nonHomeGrown;
            break;
        case Category::HomeGrown:
            ++result.homeGrownOnly;
            break;
        case Category::ClubTrained:
            ++result.clubTrained;
            break;
        }
        if (entry.player->isGoalkeeper())
            ++result.goalkeepers;
    };

    // 1) The best keepers up to the minimum, before outfielders fill the quotas.
    for (const RankedPlayer *entry : candidates) {
        if (result.goalkeepers >= quota.minGoalkeepers)
            break;
        const Category cat = category(*entry->player);
        if (entry->player->isGoalkeeper() && !blocker(cat))
            take(*entry, cat);
    }
    // 2) Everyone else in rank order, as long as a quota has room.
    for (const RankedPlayer *entry : candidates) {
        if (taken.contains(entry->player))
            continue;
        const Category cat = category(*entry->player);
        if (const auto reason = blocker(cat))
            result.leftOut.push_back({*entry, *reason});
        else
            take(*entry, cat);
    }
    // Keep the listed players in rank order (keepers were taken first).
    QHash<const Player *, int> rankIndex;
    for (int i = 0; i < static_cast<int>(ranked.size()); ++i)
        rankIndex.insert(ranked[static_cast<size_t>(i)].player, i);
    std::stable_sort(result.listed.begin(), result.listed.end(),
                     [&rankIndex](const RankedPlayer &a, const RankedPlayer &b) {
                         return rankIndex.value(a.player) < rankIndex.value(b.player);
                     });
    result.emptySlots = quota.maxSize - listedCount();
    return result;
}

bool isListed(const Player &player, Competition competition)
{
    return competition == Competition::Uefa ? player.registration.uefaListed
                                            : player.registration.leagueListed;
}

bool isEligible(const Player &player, Competition competition)
{
    return isListed(player, competition) || isExempt(player, competition);
}

bool isEligible(const Player &player, Competition competition, const Quota &quota)
{
    return isListed(player, competition) || isExempt(player, competition, quota);
}

bool hasSavedList(const std::vector<const Player *> &players, Competition competition)
{
    return std::any_of(players.begin(), players.end(), [competition](const Player *p) {
        return isListed(*p, competition);
    });
}

ListCheck checkList(const std::vector<const Player *> &listed,
                    const std::vector<const Player *> &exempt, const Quota &quota)
{
    ListCheck check;
    check.size = static_cast<int>(listed.size());
    for (const Player *p : listed) {
        switch (category(*p)) {
        case Category::NonHomeGrown:
            ++check.nonHomeGrown;
            break;
        case Category::HomeGrown:
            ++check.homeGrownOnly;
            break;
        case Category::ClubTrained:
            ++check.clubTrained;
            break;
        }
        if (p->isGoalkeeper())
            ++check.goalkeepers;
    }
    if (quota.exemptKeepersCount) {
        for (const Player *p : exempt)
            if (p->isGoalkeeper())
                ++check.goalkeepers;
    }
    check.tooMany = check.size > quota.maxSize;
    check.tooManyNonHomeGrown = check.nonHomeGrown > quota.maxNonHomeGrown;
    check.tooManyNonClubTrained =
        check.nonHomeGrown + check.homeGrownOnly > quota.maxNonClubTrained;
    check.tooFewGoalkeepers = check.goalkeepers < quota.minGoalkeepers;
    return check;
}

std::vector<QuotaCost> quotaCosts(const SquadBuilder &builder, const Definitions &definitions,
                                  const std::vector<const Player *> &pool,
                                  const std::vector<const Player *> &eligible,
                                  const QStringList &tactics, const RoleRatings &ratings)
{
    const auto xiAverage = [&](const std::vector<const Player *> &players,
                               const QString &tactic) {
        const SquadResult squad = builder.calculateSquadForTactic(players, tactic, ratings);
        double sum = 0.0;
        int filled = 0;
        for (const XiCell &cell : squad.startingXi) {
            if (cell.isFilled()) {
                sum += cell.rating;
                ++filled;
            }
        }
        return filled > 0 ? sum / filled : 0.0;
    };
    std::vector<QuotaCost> costs;
    for (const QString &tactic : tactics)
        costs.push_back({tactic, xiAverage(pool, tactic), xiAverage(eligible, tactic)});
    return costs;
}

} // namespace Registration

} // namespace fm
