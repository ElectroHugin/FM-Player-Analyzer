#include "RatingSnapshot.h"
#include "Definitions.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace fm {
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
    QJsonObject compiledRoles;
    for (const auto &role : definitions.validRoles()) {
        const auto weights = definitions.roleWeights(role);
        const auto roleKey = definitions.fmVersionId() + QLatin1Char('/')
                             + definitions.rolePhase(role) + QLatin1Char('/') + role;
        RolePlan plan;
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
        compiledRoles.insert(roleKey, QJsonObject{{QStringLiteral("attributes"), compiledAttributes},
                                                 {QStringLiteral("goalkeeper"), plan.goalkeeper},
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
    for (const auto &attribute : plan.attributes) {
        const int low = player.attrLo[attribute.index];
        const int high = player.attrHi[attribute.index];
        if (low == 0 && high == 0) {
            result.status = CurveScore::Status::MissingAttribute;
            return result;
        }
        if (low < 1 || high > 20 || low > high) {
            result.status = CurveScore::Status::InvalidInput;
            return result;
        }
        double mean = 0;
        for (int x = low; x <= high; ++x)
            mean += attribute.contributions[x - 1] / (high - low + 1);
        // Bound accumulated roundoff at the theoretical per-attribute maximum.
        result.absolute += std::clamp(mean, 0.0, attribute.weight);
    }
    if (!std::isfinite(result.absolute)) {
        result.absolute = 0;
        result.status = CurveScore::Status::InvalidInput;
        return result;
    }
    result.status = CurveScore::Status::Available;
    result.normalized = std::clamp(result.absolute / plan.denominator * 100.0, 0.0, 100.0);
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
    if (found == m_roles.constEnd() || !evaluatePlan(player, *found).normalized) return result;
    for (const auto &attribute : found->attributes) {
        const int low = player.attrLo[attribute.index];
        const int high = player.attrHi[attribute.index];
        double gain = 0;
        for (int x = low; x <= high; ++x)
            gain += (attribute.contributions[std::min(x, 19)] - attribute.contributions[x - 1])
                    / (high - low + 1);
        result.insert(attribute.index, gain / found->denominator * 100.0);
    }
    return result;
}

bool RatingSnapshot::isGoalkeeperRole(const QString &role) const
{
    const auto found = m_roles.constFind(role);
    return found != m_roles.constEnd() && found->goalkeeper;
}
} // namespace fm
