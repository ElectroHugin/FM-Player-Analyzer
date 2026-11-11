#pragma once
#include "Player.h"
#include "RatingProfile.h"
#include <QHash>
#include <memory>

namespace fm {
class Definitions;

// Compiled, immutable calculation state. It owns every table used by workers;
// subsequent edits to the source profile or role definitions cannot affect it.
class RatingSnapshot {
public:
    static std::shared_ptr<const RatingSnapshot> compile(const RatingProfile &profile,
                                                        const Definitions &definitions,
                                                        QString *error);
    CurveScore evaluate(const Player &player, const QString &role) const;
    std::vector<CurveScore> evaluateRole(const std::vector<Player> &players,
                                        const QString &role) const;
    // Exact expected DWRS gain for +1 at each possible current integer level,
    // capped at 20. A range preserves its original probability masses.
    QHash<int, double> trainingGains(const Player &player, const QString &role) const;
    bool isGoalkeeperRole(const QString &role) const;
    bool hasRole(const QString &role) const { return m_roles.contains(role); }
    // Active (weighted) attributes of the role that this player lacks. Always
    // empty for the Classic policy, which treats unknown values as before.
    QList<int> missingAttributes(const Player &player, const QString &role) const;
    // Every attribute that feeds at least one curve-policy role.
    QList<int> ratedAttributes() const;
    // Classic snapshots round to whole percent; curve profiles keep decimals.
    bool isClassic() const { return m_calculationVersion == QStringLiteral("classic-midpoint-v1"); }
    QString profileId() const { return m_profileId; }
    QString profileName() const { return m_profileName; }
    QString calculationVersion() const { return m_calculationVersion; }
    QString game() const { return m_game; }
    DwrsTierThresholds fieldTiers() const { return m_fieldTiers; }
    DwrsTierThresholds goalkeeperTiers() const { return m_goalkeeperTiers; }
    QString cacheKey() const { return m_cacheKey; }
    QString profileRevision() const { return m_profileRevision; }

private:
    struct AttributePlan {
        int index = -1;
        double weight = 0;
        std::array<double, 20> contributions{};
    };
    struct RolePlan {
        std::vector<AttributePlan> attributes;
        double denominator = 0;
        double baseline = 0;
        bool goalkeeper = false;
        bool classic = false;
    };
    static CurveScore evaluatePlan(const Player &player, const RolePlan &plan);
    QHash<QString, RolePlan> m_roles;
    QString m_cacheKey;
    QString m_profileRevision;
    QString m_profileId;
    QString m_profileName;
    QString m_calculationVersion;
    QString m_game;
    DwrsTierThresholds m_fieldTiers{};
    DwrsTierThresholds m_goalkeeperTiers{};
};

// Goalkeeper-only attributes; the role part never weights them.
bool isGoalkeeperAttribute(int index);
} // namespace fm
