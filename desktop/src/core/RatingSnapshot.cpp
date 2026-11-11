#include "RatingSnapshot.h"
#include "Definitions.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace fm {
namespace {
constexpr double kRoleKeyRatio = 1.5;   // key attribute : preferable attribute in the role part
constexpr double kMinimumCoverage = 0.7; // below this share of known weight there is no rating
}

bool isGoalkeeperAttribute(int index)
{
    switch (static_cast<Attr>(index)) {
    case Attr::Handling: case Attr::AerialReach: case Attr::CommandOfArea: case Attr::Communication:
    case Attr::Kicking: case Attr::Throwing: case Attr::OneVsOne: case Attr::Reflexes:
    case Attr::Eccentricity: case Attr::RushingOutTendency: case Attr::Punching:
        return true;
    default:
        return false;
    }
}

std::shared_ptr<const RatingSnapshot> RatingSnapshot::compile(const RatingProfile &profile,
                                                             const Definitions &definitions,
                                                             QString *error)
{
    if (error) error->clear();
    auto fail = [&](const QString &message) -> std::shared_ptr<const RatingSnapshot> {
        if (error) *error = message;
        return {};
    };
    const auto issue = profile.validate();
    if (!issue.isEmpty()) return fail(issue);
    if (!profile.compatibleGames.contains(definitions.fmVersionId()))
        return fail(QStringLiteral("incompatible-game"));
    for (auto it = profile.attributes.begin(); it != profile.attributes.end(); ++it)
        if (attrIndexByName(it.key()) < 0) return fail(QStringLiteral("unknown-attribute:") + it.key());
    const auto gamePrefix = definitions.fmVersionId() + QLatin1Char('/');
    QHash<QString, QString> roleIds;
    for (const auto &role : definitions.validRoles())
        roleIds.insert(gamePrefix + definitions.rolePhase(role) + QLatin1Char('/') + role, role);
    for (auto it = profile.roles.begin(); it != profile.roles.end(); ++it)
        if (it.key().startsWith(gamePrefix) && !roleIds.contains(it.key()))
            return fail(QStringLiteral("unknown-role:") + it.key());
    for (auto it = profile.rolePolicies.begin(); it != profile.rolePolicies.end(); ++it)
        if (it.key().startsWith(gamePrefix) && !roleIds.contains(it.key()))
            return fail(QStringLiteral("unknown-role:") + it.key());
    for (auto it = profile.positions.begin(); it != profile.positions.end(); ++it)
        if (it.key().startsWith(gamePrefix)
            && !definitions.positionToRoleMapping().contains(it.key().mid(gamePrefix.size())))
            return fail(QStringLiteral("unknown-position:") + it.key());
    for (auto it = profile.rolePositions.begin(); it != profile.rolePositions.end(); ++it) {
        if (!it.key().startsWith(gamePrefix)) continue;
        if (!roleIds.contains(it.key()) || !it.value().startsWith(gamePrefix)
            || !definitions.positionToRoleMapping().value(it.value().mid(gamePrefix.size())).contains(roleIds.value(it.key())))
            return fail(QStringLiteral("incompatible-role-position:") + it.key());
    }
    auto snapshot = std::shared_ptr<RatingSnapshot>(new RatingSnapshot);
    snapshot->m_profileRevision = profile.revision();
    snapshot->m_profileId = profile.id;
    snapshot->m_profileName = profile.name;
    snapshot->m_calculationVersion = profile.calculationVersion;
    snapshot->m_game = definitions.fmVersionId();
    snapshot->m_fieldTiers = profile.effectiveFieldTiers();
    snapshot->m_goalkeeperTiers = profile.effectiveGoalkeeperTiers();
    QJsonObject compiledRoles;
    for (const auto &role : definitions.validRoles()) {
        const auto weights = definitions.roleWeights(role);
        const auto roleKey = definitions.fmVersionId() + QLatin1Char('/')
                             + definitions.rolePhase(role) + QLatin1Char('/') + role;
        RolePlan plan;
        plan.classic = profile.rolePolicies.value(roleKey, profile.calculationVersion) == QStringLiteral("classic-midpoint-v1");
        plan.goalkeeper = definitions.gkRoles().contains(role);
        QJsonArray compiledAttributes;
        for (auto it = profile.attributes.begin(); it != profile.attributes.end(); ++it) {
            const auto effective = profile.resolve(it.key(), roleKey, weights.key.contains(it.key()),
                                                    weights.preferable.contains(it.key()));
            if (!effective.curve.isValid()) return fail(QStringLiteral("invalid-effective-curve"));
            const double total = effective.curve.weight * effective.factor;
            if (!std::isfinite(total)) return fail(QStringLiteral("contribution-overflow"));
            if (total == 0) continue;
            plan.baseline += effective.curve.baseline * effective.factor;
            if (!std::isfinite(plan.baseline)) return fail(QStringLiteral("contribution-overflow"));
            AttributePlan attr;
            attr.index = attrIndexByName(it.key());
            attr.weight = total;
            QJsonArray contributions;
            for (int x = 0; x < 20; ++x) {
                attr.contributions[x] = total * effective.curve.shape[x];
                contributions.append(attr.contributions[x]);
            }
            compiledAttributes.append(QJsonObject{{QStringLiteral("attribute"), it.key()},
                                                  {QStringLiteral("contributions"), contributions}});
            plan.denominator += total;
            if (!std::isfinite(plan.denominator)) return fail(QStringLiteral("contribution-overflow"));
            plan.attributes.push_back(attr);
        }
        if (!plan.classic && !plan.goalkeeper && profile.roleShare > 0 && plan.denominator > 0) {
            // Role emphasis: every outfield key/preferable attribute of the role
            // gets an extra linear weight on top of its base curve, so that the
            // role part is exactly roleShare of the finished scale.
            QList<int> key, preferable;
            for (const auto &name : weights.key)
                if (const int index = attrIndexByName(name); index >= 0 && !isGoalkeeperAttribute(index) && !key.contains(index))
                    key.append(index);
            for (const auto &name : weights.preferable)
                if (const int index = attrIndexByName(name); index >= 0 && !isGoalkeeperAttribute(index)
                    && !key.contains(index) && !preferable.contains(index))
                    preferable.append(index);
            const double units = kRoleKeyRatio * key.size() + preferable.size();
            if (units > 0) {
                const double total = profile.roleShare / (1.0 - profile.roleShare) * plan.denominator;
                const double unit = total / units;
                const auto addLinear = [&](int index, double weight) {
                    auto found = std::find_if(plan.attributes.begin(), plan.attributes.end(),
                                              [index](const AttributePlan &a) { return a.index == index; });
                    if (found == plan.attributes.end()) {
                        AttributePlan attr; attr.index = index;
                        plan.attributes.push_back(attr);
                        found = plan.attributes.end() - 1;
                    }
                    found->weight += weight;
                    for (int x = 0; x < 20; ++x) found->contributions[x] += weight * x / 19.0;
                    plan.denominator += weight;
                };
                for (const int index : key) addLinear(index, kRoleKeyRatio * unit);
                for (const int index : preferable) addLinear(index, unit);
                QJsonArray roleAttributes;
                for (const auto &attr : plan.attributes) {
                    QJsonArray contributions;
                    for (double value : attr.contributions) contributions.append(value);
                    roleAttributes.append(QJsonObject{{QStringLiteral("attribute"), attrNames()[attr.index]},
                                                      {QStringLiteral("contributions"), contributions}});
                }
                compiledAttributes = roleAttributes;
            }
        }
        compiledRoles.insert(roleKey, QJsonObject{{QStringLiteral("attributes"), compiledAttributes},
                                                 {QStringLiteral("goalkeeper"), plan.goalkeeper}, {QStringLiteral("classic"), plan.classic},
                                                 {QStringLiteral("baseline"), plan.baseline}});
        snapshot->m_roles.insert(role, std::move(plan));
    }
    const QJsonObject identity{{QStringLiteral("profileRevision"), profile.revision()},
                               {QStringLiteral("calculationVersion"), profile.calculationVersion},
                               {QStringLiteral("game"), definitions.fmVersionId()},
                               {QStringLiteral("compiledRoles"), compiledRoles}};
    snapshot->m_cacheKey = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    return snapshot;
}

CurveScore RatingSnapshot::evaluatePlan(const Player &player, const RolePlan &plan)
{
    CurveScore result;
    if (plan.denominator == 0) return result;
    double known = 0;
    for (const auto &attribute : plan.attributes) {
        const int low = player.attrLo[attribute.index];
        const int high = player.attrHi[attribute.index];
        if (low == 0 && high == 0 && !plan.classic) continue; // missing: left out of both sides
        if ((low < 1 && !(plan.classic && low == 0 && high == 0)) || high > 20 || low > high) {
            result.status = CurveScore::Status::InvalidInput;
            return result;
        }
        double mean = 0;
        if (plan.classic) {
            const double midpoint = (low + high) / 2.0;
            if (midpoint == 0) mean = -attribute.contributions[1];
            else {
                const int lower = int(std::floor(midpoint));
                mean = attribute.contributions[lower - 1];
                if (lower < 20) mean += (midpoint - lower) * (attribute.contributions[lower] - mean);
            }
        } else for (int x = low; x <= high; ++x)
            mean += attribute.contributions[x - 1] / (high - low + 1);
        result.absolute += plan.classic ? mean : std::clamp(mean, 0.0, attribute.weight);
        known += attribute.weight;
    }
    if (!std::isfinite(result.absolute)) {
        result.absolute = 0;
        result.status = CurveScore::Status::InvalidInput;
        return result;
    }
    result.coverage = plan.classic ? 1.0 : std::clamp(known / plan.denominator, 0.0, 1.0);
    if (!plan.classic && (known <= 0 || known < kMinimumCoverage * plan.denominator - 1e-9)) {
        result.absolute = 0;
        result.status = CurveScore::Status::MissingAttribute;
        return result;
    }
    result.status = CurveScore::Status::Available;
    result.normalized = plan.classic ? std::nearbyint(result.absolute / plan.denominator * 100.0)
                                     : std::clamp(result.absolute / known * 100.0, 0.0, 100.0);
    result.absolute += plan.baseline;
    if (!std::isfinite(result.absolute)) {
        result.absolute = 0;
        result.normalized.reset();
        result.status = CurveScore::Status::InvalidInput;
    }
    return result;
}

CurveScore RatingSnapshot::evaluate(const Player &player, const QString &role) const
{
    const auto found = m_roles.constFind(role);
    if (found == m_roles.constEnd()) {
        CurveScore result;
        result.status = CurveScore::Status::InvalidInput;
        return result;
    }
    return evaluatePlan(player, *found);
}

std::vector<CurveScore> RatingSnapshot::evaluateRole(const std::vector<Player> &players,
                                                   const QString &role) const
{
    std::vector<CurveScore> results;
    results.reserve(players.size());
    const auto found = m_roles.constFind(role);
    for (const auto &player : players)
        results.push_back(found == m_roles.constEnd() ? evaluate(player, role) : evaluatePlan(player, *found));
    return results;
}

QHash<int, double> RatingSnapshot::trainingGains(const Player &player, const QString &role) const
{
    QHash<int, double> result;
    const auto found = m_roles.constFind(role);
    if (found == m_roles.constEnd()) return result;
    const auto score = evaluatePlan(player, *found);
    if (!score.normalized) return result;
    const double known = found->classic ? found->denominator : found->denominator * score.coverage;
    if (known <= 0) return result;
    for (const auto &attribute : found->attributes) {
        const int low = player.attrLo[attribute.index];
        const int high = player.attrHi[attribute.index];
        if (low == 0) continue;
        double gain = 0;
        if (found->classic) {
            const auto at = [&](double x) {
                const int lower = int(std::floor(x));
                const double value = attribute.contributions[lower - 1];
                return lower == 20 ? value : value + (x - lower) * (attribute.contributions[lower] - value);
            };
            gain = at((std::min(low + 1, 20) + std::min(high + 1, 20)) / 2.0) - at((low + high) / 2.0);
        } else for (int x = low; x <= high; ++x)
            gain += (attribute.contributions[std::min(x, 19)] - attribute.contributions[x - 1])
                    / (high - low + 1);
        result.insert(attribute.index, gain / known * 100.0);
    }
    return result;
}

QList<int> RatingSnapshot::missingAttributes(const Player &player, const QString &role) const
{
    QList<int> result;
    const auto found = m_roles.constFind(role);
    if (found == m_roles.constEnd() || found->classic) return result;
    for (const auto &attribute : found->attributes)
        if (player.attrLo[attribute.index] == 0 && player.attrHi[attribute.index] == 0)
            result.append(attribute.index);
    return result;
}

QList<int> RatingSnapshot::ratedAttributes() const
{
    QSet<int> seen;
    for (const auto &plan : m_roles)
        if (!plan.classic)
            for (const auto &attribute : plan.attributes) seen.insert(attribute.index);
    QList<int> result(seen.begin(), seen.end());
    std::sort(result.begin(), result.end());
    return result;
}

bool RatingSnapshot::isGoalkeeperRole(const QString &role) const
{
    const auto found = m_roles.constFind(role);
    return found != m_roles.constEnd() && found->goalkeeper;
}
} // namespace fm
