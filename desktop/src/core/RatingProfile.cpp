#include "RatingProfile.h"
#include "Attributes.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace fm {
namespace {
bool nonnegative(double value) { return std::isfinite(value) && value >= 0; }
QJsonArray shapeJson(const std::array<double, 20> &shape)
{
    QJsonArray result;
    for (double value : shape)
        result.append(value);
    return result;
}
std::optional<std::array<double, 20>> readShape(const QJsonValue &value)
{
    if (!value.isArray() || value.toArray().size() != 20)
        return std::nullopt;
    std::array<double, 20> result;
    const auto array = value.toArray();
    for (int i = 0; i < 20; ++i) {
        if (!array[i].isDouble())
            return std::nullopt;
        result[i] = array[i].toDouble();
    }
    return RatingCurve{0, result}.isValid() ? std::optional(result) : std::nullopt;
}
bool onlyKeys(const QJsonObject &object, const QStringList &allowed)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key()))
            return false;
    return true;
}
QJsonObject overridesJson(const QMap<QString, QMap<QString, RatingOverride>> &scopes)
{
    QJsonObject result;
    for (auto scope = scopes.begin(); scope != scopes.end(); ++scope) {
        QJsonObject attributes;
        for (auto attr = scope->begin(); attr != scope->end(); ++attr) {
            QJsonObject fields;
            if (attr->weight) fields.insert(QStringLiteral("weight"), *attr->weight);
            if (attr->shape) fields.insert(QStringLiteral("shape"), shapeJson(*attr->shape));
            if (attr->factor) fields.insert(QStringLiteral("factor"), *attr->factor);
            attributes.insert(attr.key(), fields);
        }
        result.insert(scope.key(), attributes);
    }
    return result;
}
bool readOverrides(const QJsonValue &value, QMap<QString, QMap<QString, RatingOverride>> &out)
{
    if (!value.isObject()) return false;
    const auto scopes = value.toObject();
    for (auto scope = scopes.begin(); scope != scopes.end(); ++scope) {
        if (!scope->isObject()) return false;
        QMap<QString, RatingOverride> attributes;
        const auto object = scope->toObject();
        for (auto attr = object.begin(); attr != object.end(); ++attr) {
            if (!attr->isObject()) return false;
            const auto fields = attr->toObject();
            if (!onlyKeys(fields, {QStringLiteral("weight"), QStringLiteral("shape"), QStringLiteral("factor")}))
                return false;
            RatingOverride rule;
            if (fields.contains(QStringLiteral("weight"))) {
                if (!fields.value(QStringLiteral("weight")).isDouble()) return false;
                rule.weight = fields.value(QStringLiteral("weight")).toDouble();
            }
            if (fields.contains(QStringLiteral("factor"))) {
                if (!fields.value(QStringLiteral("factor")).isDouble()) return false;
                rule.factor = fields.value(QStringLiteral("factor")).toDouble();
            }
            if (fields.contains(QStringLiteral("shape"))) {
                rule.shape = readShape(fields.value(QStringLiteral("shape")));
                if (!rule.shape) return false;
            }
            attributes.insert(attr.key(), rule);
        }
        out.insert(scope.key(), attributes);
    }
    return true;
}
}

QString RatingProfile::validate() const
{
    if (id.trimmed().isEmpty() || name.trimmed().isEmpty())
        return QStringLiteral("identity-required");
    if (calculationVersion != QStringLiteral("curves-uniform-range-v1"))
        return QStringLiteral("unsupported-calculation-version");
    if (compatibleGames.isEmpty()) return QStringLiteral("compatibility-required");
    for (const auto &game : compatibleGames)
        if (game != QStringLiteral("fm24") && game != QStringLiteral("fm27"))
            return QStringLiteral("unsupported-game");
    if (!nonnegative(keyFactor) || !nonnegative(preferableFactor))
        return QStringLiteral("invalid-role-factor");
    for (auto it = attributes.begin(); it != attributes.end(); ++it)
        if (attrIndexByName(it.key()) < 0 || !it->isValid()) return QStringLiteral("invalid-attribute-curve");
    const auto validScope = [&](const QString &scope, bool role) {
        const auto parts = scope.split(QLatin1Char('/'));
        if (parts.size() != (role ? 3 : 2) || !compatibleGames.contains(parts.front())
            || parts.back().trimmed().isEmpty()) return false;
        return !role || parts[1] == QStringLiteral("both") || parts[1] == QStringLiteral("ip")
                     || parts[1] == QStringLiteral("oop");
    };
    for (const auto *scopes : {&positions, &roles}) {
        for (auto scope = scopes->begin(); scope != scopes->end(); ++scope) {
            if (!validScope(scope.key(), scopes == &roles)) return QStringLiteral("invalid-scope");
            for (auto rule = scope->begin(); rule != scope->end(); ++rule) {
                if (!attributes.contains(rule.key())) return QStringLiteral("unknown-override-attribute");
                if ((rule->weight && !nonnegative(*rule->weight))
                    || (rule->factor && !nonnegative(*rule->factor))
                    || (rule->shape && !RatingCurve{0, *rule->shape}.isValid()))
                    return QStringLiteral("invalid-override");
            }
        }
    }
    for (auto it = rolePositions.begin(); it != rolePositions.end(); ++it)
        if (!validScope(it.key(), true) || !validScope(it.value(), false)
            || it.key().section(QLatin1Char('/'), 0, 0) != it.value().section(QLatin1Char('/'), 0, 0))
            return QStringLiteral("invalid-role-position");
    return {};
}

ResolvedRatingAttribute RatingProfile::resolve(const QString &attribute, const QString &role,
                                               bool key, bool preferable) const
{
    ResolvedRatingAttribute result;
    result.curve = attributes.value(attribute, RatingCurve::linear(0));
    result.factor = key ? keyFactor : preferable ? preferableFactor : 1.0;
    auto apply = [&](const QMap<QString, RatingOverride> &rules, const QString &origin) {
        const auto found = rules.constFind(attribute);
        if (found == rules.constEnd()) return;
        if (found->weight) { result.curve.weight = *found->weight; result.weightOrigin = origin; }
        if (found->shape) { result.curve.shape = *found->shape; result.shapeOrigin = origin; }
        if (found->factor) { result.factor = *found->factor; result.factorOrigin = origin; }
    };
    const auto position = rolePositions.value(role);
    if (!position.isEmpty()) apply(positions.value(position), QStringLiteral("position:") + position);
    apply(roles.value(role), QStringLiteral("role:") + role);
    return result;
}

QJsonObject RatingProfile::toJson() const
{
    QJsonObject curves;
    for (auto it = attributes.begin(); it != attributes.end(); ++it)
        curves.insert(it.key(), QJsonObject{{QStringLiteral("weight"), it->weight},
                                           {QStringLiteral("shape"), shapeJson(it->shape)}});
    QJsonObject mapping;
    for (auto it = rolePositions.begin(); it != rolePositions.end(); ++it)
        mapping.insert(it.key(), it.value());
    return {{QStringLiteral("format"), QStringLiteral("fm-dwrs-profile")},
            {QStringLiteral("version"), formatVersion},
            {QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("description"), description}, {QStringLiteral("origin"), origin},
            {QStringLiteral("sourceVersion"), sourceVersion},
            {QStringLiteral("sources"), QJsonArray::fromStringList(sources)},
            {QStringLiteral("compatibleGames"), QJsonArray::fromStringList(compatibleGames)},
            {QStringLiteral("calculationVersion"), calculationVersion},
            {QStringLiteral("keyFactor"), keyFactor}, {QStringLiteral("preferableFactor"), preferableFactor},
            {QStringLiteral("attributes"), curves}, {QStringLiteral("positions"), overridesJson(positions)},
            {QStringLiteral("roles"), overridesJson(roles)}, {QStringLiteral("rolePositions"), mapping}};
}

QString RatingProfile::revision() const
{
    auto formula = toJson();
    for (const auto &key : {"id", "name", "description", "origin", "sourceVersion", "sources"})
        formula.remove(QLatin1String(key));
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(formula).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

std::optional<RatingProfile> RatingProfile::fromJson(const QJsonObject &json, QString *error)
{
    if (error) error->clear();
    auto fail = [&](const QString &code) -> std::optional<RatingProfile> {
        if (error) *error = code;
        return std::nullopt;
    };
    if (json.value(QStringLiteral("format")) != QStringLiteral("fm-dwrs-profile")
        || json.value(QStringLiteral("version")) != formatVersion)
        return fail(QStringLiteral("unsupported-format"));
    RatingProfile result;
    const auto expected = result.toJson();
    if (!onlyKeys(json, expected.keys())) return fail(QStringLiteral("unknown-field"));
    for (auto it = expected.begin(); it != expected.end(); ++it)
        if (!json.contains(it.key()) || json.value(it.key()).type() != it.value().type())
            return fail(QStringLiteral("missing-or-invalid-field:") + it.key());
    result.id = json.value(QStringLiteral("id")).toString();
    result.name = json.value(QStringLiteral("name")).toString();
    result.description = json.value(QStringLiteral("description")).toString();
    result.origin = json.value(QStringLiteral("origin")).toString();
    result.sourceVersion = json.value(QStringLiteral("sourceVersion")).toString();
    result.calculationVersion = json.value(QStringLiteral("calculationVersion")).toString();
    result.keyFactor = json.value(QStringLiteral("keyFactor")).toDouble();
    result.preferableFactor = json.value(QStringLiteral("preferableFactor")).toDouble();
    for (const auto &field : {QStringLiteral("sources"), QStringLiteral("compatibleGames")}) {
        QStringList list;
        for (const auto &entry : json.value(field).toArray()) {
            if (!entry.isString()) return fail(QStringLiteral("invalid-list-entry"));
            list.append(entry.toString());
        }
        if (field == QStringLiteral("sources")) result.sources = list;
        else result.compatibleGames = list;
    }
    const auto attrs = json.value(QStringLiteral("attributes")).toObject();
    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
        if (!it->isObject()) return fail(QStringLiteral("invalid-attribute"));
        const auto fields = it->toObject();
        if (!onlyKeys(fields, {QStringLiteral("weight"), QStringLiteral("shape")})
            || !fields.value(QStringLiteral("weight")).isDouble())
            return fail(QStringLiteral("invalid-attribute"));
        const auto shape = readShape(fields.value(QStringLiteral("shape")));
        if (!shape) return fail(QStringLiteral("invalid-shape"));
        result.attributes.insert(it.key(), {fields.value(QStringLiteral("weight")).toDouble(), *shape});
    }
    if (!readOverrides(json.value(QStringLiteral("positions")), result.positions)
        || !readOverrides(json.value(QStringLiteral("roles")), result.roles))
        return fail(QStringLiteral("invalid-overrides"));
    const auto mapping = json.value(QStringLiteral("rolePositions")).toObject();
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        if (!it->isString()) return fail(QStringLiteral("invalid-role-position"));
        result.rolePositions.insert(it.key(), it->toString());
    }
    const auto issue = result.validate();
    if (!issue.isEmpty()) return fail(issue);
    return result;
}
} // namespace fm
