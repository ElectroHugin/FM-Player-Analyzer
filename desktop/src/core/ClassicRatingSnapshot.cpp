#include "ClassicRatingSnapshot.h"
#include "DwrsEngine.h"
#include "Definitions.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace fm {
namespace {
template<class T> QJsonArray encode(const std::vector<T> &values) {
    QJsonArray array; for (const auto value : values) array.append(value); return array;
}
template<class T> bool decode(const QJsonValue &value, std::vector<T> &result) {
    if (!value.isArray()) return false;
    for (const auto &item : value.toArray()) {
        if (!item.isDouble() || !std::isfinite(item.toDouble())) return false;
        const double number = item.toDouble();
        if constexpr (std::is_integral_v<T>) {
            if (number < 0 || number > kAttrCount || std::floor(number) != number) return false;
        }
        result.push_back(static_cast<T>(number));
    }
    return true;
}
bool keys(const QJsonObject &object, const QStringList &allowed) {
    if (object.size() != allowed.size()) return false;
    for (auto it = object.begin(); it != object.end(); ++it) if (!allowed.contains(it.key())) return false;
    return true;
}
}
bool ClassicRatingSnapshot::prepare(Plan &plan)
{
    if (plan.attributes.size() != plan.categories.size() || plan.attributes.size() != plan.factors.size()
        || plan.weights.size() != plan.counts.size() || plan.attributes.size() > kAttrCount) return false;
    std::vector<int> counts(plan.weights.size(), 0);
    std::vector<double> worst(plan.weights.size(), 0), best(plan.weights.size(), 0);
    QSet<int> seen;
    for (size_t i = 0; i < plan.attributes.size(); ++i) {
        const int a = plan.attributes[i], c = plan.categories[i];
        if (a < 0 || a >= kAttrCount || seen.contains(a) || c < 0 || size_t(c) >= counts.size()
            || !std::isfinite(plan.factors[i]) || plan.factors[i] < 0) return false;
        seen.insert(a); ++counts[c];
        worst[c] += plan.factors[i]; best[c] += 20.0 * plan.factors[i];
    }
    if (counts != plan.counts) return false;
    plan.worst = plan.best = 0;
    for (size_t c = 0; c < counts.size(); ++c) {
        if (counts[c] <= 0 || !std::isfinite(plan.weights[c]) || plan.weights[c] < 0) return false;
        plan.worst += plan.weights[c] * (worst[c] / counts[c]);
        plan.best += plan.weights[c] * (best[c] / counts[c]);
    }
    return std::isfinite(plan.worst) && std::isfinite(plan.best)
           && std::isfinite(plan.best - plan.worst);
}
std::shared_ptr<const ClassicRatingSnapshot> ClassicRatingSnapshot::capture(const DwrsEngine &engine)
{
    auto snapshot = std::shared_ptr<ClassicRatingSnapshot>(new ClassicRatingSnapshot);
    snapshot->m_game = engine.m_definitions.fmVersionId();
    for (auto it = engine.m_planCache.begin(); it != engine.m_planCache.end(); ++it) {
        Plan plan{it->attrIndexes, it->categoryIndexes, it->categoryCounts, it->roleWeights, it->categoryWeights};
        if (!prepare(plan)) return {};
        snapshot->m_roles.insert(it.key(), std::move(plan));
    }
    return snapshot;
}
CurveScore ClassicRatingSnapshot::evaluate(const Player &player, const QString &role) const
{
    CurveScore result;
    const auto it = m_roles.constFind(role);
    if (it == m_roles.constEnd()) { result.status = CurveScore::Status::InvalidInput; return result; }
    const auto &plan = *it;
    const double denominator = plan.best - plan.worst;
    if (denominator == 0) return result;
    std::vector<double> sums(plan.weights.size(), 0);
    for (size_t i = 0; i < plan.attributes.size(); ++i) {
        const int a = plan.attributes[i];
        const int low = player.attrLo[a], high = player.attrHi[a];
        if (low > high || high > 20 || (low == 0 && high != 0)) {
            result.status = CurveScore::Status::InvalidInput; return result;
        }
        const double value = (low + high) / 2.0;
        sums[plan.categories[i]] += value * plan.factors[i];
    }
    for (size_t c = 0; c < plan.weights.size(); ++c)
        result.absolute += plan.weights[c] * (sums[c] / plan.counts[c]);
    const double normalized = (result.absolute - plan.worst) / denominator * 100.0;
    if (!std::isfinite(result.absolute) || !std::isfinite(normalized)) {
        result.absolute = 0; result.status = CurveScore::Status::InvalidInput; return result;
    }
    result.status = CurveScore::Status::Available;
    result.normalized = std::nearbyint(normalized);
    return result;
}
QJsonObject ClassicRatingSnapshot::toJson() const
{
    QJsonObject roles;
    for (auto it = m_roles.begin(); it != m_roles.end(); ++it)
        roles.insert(it.key(), QJsonObject{{"attributes", encode(it->attributes)}, {"categories", encode(it->categories)},
                                         {"counts", encode(it->counts)}, {"factors", encode(it->factors)}, {"weights", encode(it->weights)}});
    return {{"format", "fm-classic-snapshot"}, {"version", 1}, {"calculationVersion", "classic-2.2.3-v1"},
            {"game", m_game}, {"roles", roles}};
}
QString ClassicRatingSnapshot::revision() const
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(toJson()).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}
std::shared_ptr<const ClassicRatingSnapshot> ClassicRatingSnapshot::fromJson(const QJsonObject &json, QString *error)
{
    if (error) error->clear();
    const auto fail = [&] { if (error) *error = QStringLiteral("invalid-classic-snapshot"); return std::shared_ptr<const ClassicRatingSnapshot>{}; };
    if (!keys(json, {"format", "version", "calculationVersion", "game", "roles"})
        || json.value("format") != QJsonValue("fm-classic-snapshot") || json.value("version") != QJsonValue(1)
        || json.value("calculationVersion") != QJsonValue("classic-2.2.3-v1")
        || !json.value("roles").isObject()) return fail();
    auto snapshot = std::shared_ptr<ClassicRatingSnapshot>(new ClassicRatingSnapshot);
    snapshot->m_game = json.value("game").toString();
    if (snapshot->m_game != "fm24" && snapshot->m_game != "fm27") return fail();
    const auto roles = json.value("roles").toObject();
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        if (it.key().trimmed().isEmpty() || !it->isObject()) return fail();
        const auto object = it->toObject(); Plan plan;
        if (!keys(object, {"attributes", "categories", "counts", "factors", "weights"})
            || !decode(object.value("attributes"), plan.attributes) || !decode(object.value("categories"), plan.categories)
            || !decode(object.value("counts"), plan.counts) || !decode(object.value("factors"), plan.factors)
            || !decode(object.value("weights"), plan.weights) || !prepare(plan)) return fail();
        snapshot->m_roles.insert(it.key(), std::move(plan));
    }
    return snapshot;
}
} // namespace fm
