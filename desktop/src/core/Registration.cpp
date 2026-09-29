#include "Registration.h"

#include "Definitions.h"

#include <QSet>

#include <algorithm>
#include <optional>

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
    switch (settings.league) {
    case LeagueRules::PremierLeague:
        // 25-man list, at most 17 non-home-grown; no club-trained quota.
        break;
    case LeagueRules::None:
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
        const SquadResult squad = builder.calculateSquadAndSurplus(
            pool, definitions.tacticRoles().value(tactic), definitions.tacticSlotOrder(tactic),
            ratings);
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
        if (isExempt(*entry.player, competition)) {
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

std::vector<QuotaCost> quotaCosts(const SquadBuilder &builder, const Definitions &definitions,
                                  const std::vector<const Player *> &pool,
                                  const std::vector<const Player *> &eligible,
                                  const QStringList &tactics, const RoleRatings &ratings)
{
    const auto xiAverage = [&](const std::vector<const Player *> &players,
                               const QString &tactic) {
        const SquadResult squad = builder.calculateSquadAndSurplus(
            players, definitions.tacticRoles().value(tactic),
            definitions.tacticSlotOrder(tactic), ratings);
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
