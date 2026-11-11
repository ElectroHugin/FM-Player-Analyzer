#include "BuiltinRatingProfiles.h"
#include "AppConfig.h"
#include "ClassicRatingSnapshot.h"
#include "DwrsEngine.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace fm {
namespace {
QString roleId(const Definitions &definitions, const QString &role) {
    return definitions.fmVersionId() + '/' + definitions.rolePhase(role) + '/' + role;
}
QMap<QString, RatingCurve> curvesFor(const QJsonObject &plan) {
    QMap<QString, RatingCurve> result;
    for (const auto &name : attrNames()) result.insert(name, RatingCurve::linear(0));
    const auto attributes = plan.value("attributes").toArray();
    const auto categories = plan.value("categories").toArray();
    const auto counts = plan.value("counts").toArray();
    const auto weights = plan.value("weights").toArray();
    for (int i = 0; i < attributes.size(); ++i) {
        const int category = categories[i].toInt();
        const double coefficient = weights[category].toDouble() / counts[category].toInt();
        auto curve = RatingCurve::linear(coefficient * 19); curve.baseline = coefficient;
        result[attrNames()[attributes[i].toInt()]] = curve;
    }
    return result;
}
void assignParents(RatingProfile &profile, const Definitions &definitions) {
    auto positions = definitions.positionToRoleMapping().keys(); std::sort(positions.begin(), positions.end());
    for (const auto &role : definitions.validRoles())
        for (const auto &position : positions)
            if (definitions.positionToRoleMapping().value(position).contains(role)) {
                profile.rolePositions.insert(roleId(definitions, role), definitions.fmVersionId() + '/' + position);
                break; // One explicit, stable parent; never apply multiple positions.
            }
}
}
std::optional<RatingProfile> classicRatingProfile(const QList<Definitions> &definitions,
                                                 const AppConfig &config, QString *error)
{
    if (error) error->clear();
    const auto fail = [&](const QString &message) -> std::optional<RatingProfile> { if (error) *error = message; return {}; };
    if (definitions.isEmpty()) return fail(QStringLiteral("definitions-required"));
    RatingProfile result;
    result.id = QStringLiteral("dwrs-classic-florin133"); result.name = QStringLiteral("DWRS Classic · florin133");
    result.description = QStringLiteral("FM Player Analyzer 2.2.3 DWRS implementation based on florin133. Linear contributions, legacy midpoint ranges and missing-value behavior; not an unchanged research dataset.");
    result.origin = QStringLiteral("builtin-classic"); result.sourceVersion = QStringLiteral("FM Player Analyzer 2.2.3");
    result.sources = {QStringLiteral("https://www.reddit.com/r/footballmanagergames/comments/16fuksi/a_not_so_short_guide_to_meta_player_attributes/")};
    result.calculationVersion = QStringLiteral("classic-midpoint-v1");
    result.keyFactor = config.roleMultiplier(QStringLiteral("key"));
    result.preferableFactor = config.roleMultiplier(QStringLiteral("preferable"));
    for (const auto &defs : definitions) {
        if (result.compatibleGames.contains(defs.fmVersionId())) return fail(QStringLiteral("duplicate-game"));
        result.compatibleGames.append(defs.fmVersionId());
        DwrsEngine engine(defs, config);
        const auto frozen = ClassicRatingSnapshot::capture(engine);
        if (!frozen) return fail(QStringLiteral("invalid-classic-settings"));
        const auto plans = frozen->toJson().value("roles").toObject();
        if (result.attributes.isEmpty()) {
            for (const auto &role : defs.validRoles()) if (!defs.gkRoles().contains(role)) {
                result.attributes = curvesFor(plans.value(role).toObject()); break;
            }
            if (result.attributes.isEmpty()) return fail(QStringLiteral("field-role-required"));
        }
        for (const auto &role : defs.validRoles()) {
            const auto curves = curvesFor(plans.value(role).toObject());
            QMap<QString, RatingOverride> overrides;
            for (auto it = curves.begin(); it != curves.end(); ++it) {
                const auto base = result.attributes.value(it.key()); RatingOverride rule;
                if (base.weight != it->weight) rule.weight = it->weight;
                if (base.baseline != it->baseline) rule.baseline = it->baseline;
                if (rule.weight || rule.baseline) overrides.insert(it.key(), rule);
            }
            if (!overrides.isEmpty()) result.roles.insert(roleId(defs, role), overrides);
        }
        assignParents(result, defs);
    }
    for (const auto &name : attrNames()) result.attributeEvidence[name] = QStringLiteral("legacy");
    const auto validation = result.validate(); if (!validation.isEmpty()) return fail(validation);
    return result;
}
std::optional<RatingProfile> harvestgreenRatingProfile(const QList<Definitions> &definitions,
                                                      const AppConfig &config,
                                                      const QJsonObject &measurements, QString *error)
{
    auto classic = classicRatingProfile(definitions, config, error); if (!classic) return {};
    const auto fail = [&](const QString &message) -> std::optional<RatingProfile> { if (error) *error = message; return {}; };
    if (measurements.value("status") != QJsonValue("RULE_APPROVED_2026-11-09")
        || measurements.value("cutoffPercentagePoints") != QJsonValue(4)
        || !measurements.value("records").isArray()) return fail(QStringLiteral("unapproved-source-data"));
    RatingProfile result = *classic;
    result.id = QStringLiteral("dwrs-fm27-harvestgreen22"); result.name = QStringLiteral("DWRS FM27 · harvestgreen22");
    result.origin = QStringLiteral("builtin-harvestgreen"); result.sourceVersion = QStringLiteral("harvestgreen22 2026-11-03 · isotonic equal weights · cutoff 4 pp");
    result.description = QStringLiteral("Visible FM27 measurements fitted monotonically with equal knot weights; totals <=4 pp disabled by modelling choice, not a statistical confidence threshold. Plateau 18–20. Unmeasured outfield attributes disabled. Goalkeepers retain the separately identified 2.2.3 basis. Role factors 1.5/1.2 are DWRS model choices. Scores are not win probabilities.");
    result.sources = {QStringLiteral("https://sortitoutsi.net/content/78219/fm27-attribute-test-tactical-understanding-training-system-preset-tactics-ai-reverse-engineering-engine-for-test"), QStringLiteral("https://github.com/ElectroHugin/FM-Player-Analyzer/issues/17")};
    result.calculationVersion = QStringLiteral("curves-uniform-range-v1"); result.roles.clear();
    result.keyFactor = 1.5; result.preferableFactor = 1.2;
    // The measurements come from FM27 matches only, so the profile is FM27-only.
    result.roleShare = 0.33;
    result.compatibleGames = {QStringLiteral("fm27")};
    for (auto it = result.rolePositions.begin(); it != result.rolePositions.end();)
        it = it.key().startsWith(QStringLiteral("fm27/")) ? std::next(it) : result.rolePositions.erase(it);
    result.rolePolicies.clear(); result.positions.clear();
    for (const auto &name : attrNames()) { result.attributes[name] = RatingCurve::linear(0); result.attributeEvidence[name] = QStringLiteral("not-investigated"); }
    QSet<QString> measured;
    int measuredCount = 0;
    for (const auto &entry : measurements.value("records").toArray()) {
        if (!entry.isObject()) return fail(QStringLiteral("invalid-measurement"));
        const auto record = entry.toObject(); const auto name = record.value("attribute").toString();
        if (measured.contains(name) || name.isEmpty()) return fail(QStringLiteral("duplicate-measurement"));
        measured.insert(name);
        if (record.value("original").isNull() && record.value("reason") == QJsonValue("Not investigated")) continue;
        ++measuredCount;
        if (attrIndexByName(name) < 0) {
            if (name != "Pressure" && name != "Consistency" && name != "Important Matches") return fail(QStringLiteral("unknown-measured-attribute"));
            continue;
        }
        const auto points = record.value("original").toArray();
        if (points.size() != 4) return fail(QStringLiteral("invalid-measurement"));
        std::array<double, 4> values{};
        for (int i = 0; i < 4; ++i) { if (!points[i].isDouble()) return fail(QStringLiteral("invalid-measurement")); values[i] = points[i].toDouble(); }
        const auto curve = harvestgreenCurve(values, 4); if (!curve) return fail(QStringLiteral("invalid-measurement"));
        result.attributes[name] = *curve;
        result.attributeEvidence[name] = curve->weight > 0 ? QStringLiteral("measured") : QStringLiteral("noise-filtered");
    }
    if (measuredCount != 33 || measured.size() != 39) return fail(QStringLiteral("incomplete-measurements"));
    if (!result.compatibleGames.isEmpty() && std::none_of(definitions.begin(), definitions.end(),
            [](const Definitions &d) { return d.fmVersionId() == QStringLiteral("fm27"); }))
        return fail(QStringLiteral("fm27-definitions-required"));
    for (const auto &defs : definitions) if (defs.fmVersionId() == QStringLiteral("fm27")) for (const auto &role : defs.gkRoles()) {
        const auto id = roleId(defs, role); const auto weights = defs.roleWeights(role);
        for (const auto &name : attrNames()) {
            const auto original = classic->resolve(name, id, weights.key.contains(name), weights.preferable.contains(name));
            RatingOverride rule; rule.weight = original.curve.weight; rule.baseline = original.curve.baseline;
            rule.shape = original.curve.shape; rule.factor = original.factor;
            result.roles[id][name] = rule;
        }
        result.rolePolicies[id] = QStringLiteral("classic-midpoint-v1");
    }
    const auto validation = result.validate(); if (!validation.isEmpty()) return fail(validation);
    return result;
}
} // namespace fm
