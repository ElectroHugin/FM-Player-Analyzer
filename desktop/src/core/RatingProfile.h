#pragma once

#include "RatingCurve.h"
#include <QJsonObject>
#include <QMap>
#include <QStringList>

namespace fm {

struct RatingOverride {
    std::optional<double> weight;
    std::optional<double> baseline;
    std::optional<std::array<double, 20>> shape;
    std::optional<double> factor;
};

struct ResolvedRatingAttribute {
    RatingCurve curve;
    double factor = 1.0;
    QString weightOrigin = QStringLiteral("base");
    QString baselineOrigin = QStringLiteral("base");
    QString shapeOrigin = QStringLiteral("base");
    QString factorOrigin = QStringLiteral("role-category");
};

// Value type. Workers receive a const copy/shared_ptr<const RatingProfile>.
// Dictionary keys are stable attribute IDs and explicit version/phase/role IDs.
struct RatingProfile {
    static constexpr int formatVersion = 1;
    QString id;
    QString name;
    QString description;
    QString origin;
    QString sourceVersion;
    QStringList sources;
    QStringList compatibleGames;
    QString calculationVersion = QStringLiteral("curves-uniform-range-v1");
    QMap<QString, RatingCurve> attributes;
    QMap<QString, QString> attributeEvidence; // Provenance only; excluded from formula revision.
    // Independent optional fields, never whole-record replacement.
    QMap<QString, QMap<QString, RatingOverride>> positions;
    QMap<QString, QMap<QString, RatingOverride>> roles;
    // Explicit one-parent mapping avoids ambiguous multi-position roles.
    QMap<QString, QString> rolePositions;
    QMap<QString, QString> rolePolicies; // Explicit per-role calculation policy, e.g. legacy goalkeepers.
    double keyFactor = 1.5;
    double preferableFactor = 1.2;
    // Share of the finished outfield rating (0..kMaxRoleShare) that comes from
    // an additional linear weight on the role's key and preferable attributes.
    // 0 disables it. Omitted from JSON when 0, so older files and formula
    // revisions stay unchanged.
    double roleShare = 0.0;
    static constexpr double kMaxRoleShare = 0.6;

    QString validate() const;
    ResolvedRatingAttribute resolve(const QString &attribute, const QString &role,
                                    bool key, bool preferable) const;
    QJsonObject toJson() const;
    // Formula digest excludes names/provenance, includes compatibility and every
    // effective rule. Role-definition snapshots form a separate cache key.
    QString revision() const;
    static std::optional<RatingProfile> fromJson(const QJsonObject &json, QString *error);
};

} // namespace fm
