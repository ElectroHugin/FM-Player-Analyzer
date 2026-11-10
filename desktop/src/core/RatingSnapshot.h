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
    };
    static CurveScore evaluatePlan(const Player &player, const RolePlan &plan);
    QHash<QString, RolePlan> m_roles;
    QString m_cacheKey;
    QString m_profileRevision;
};
} // namespace fm
