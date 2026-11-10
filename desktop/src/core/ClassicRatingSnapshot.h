#pragma once
#include "Player.h"
#include "RatingCurve.h"
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <memory>

namespace fm {
class DwrsEngine;

// Frozen 2.2.3 arithmetic, including missing-as-zero and midpoint ranges.
// This compatibility policy is separate from nonlinear curve evaluation.
class ClassicRatingSnapshot {
public:
    static std::shared_ptr<const ClassicRatingSnapshot> capture(const DwrsEngine &engine);
    static std::shared_ptr<const ClassicRatingSnapshot> fromJson(const QJsonObject &json, QString *error);
    QJsonObject toJson() const;
    QString revision() const;
    CurveScore evaluate(const Player &player, const QString &role) const;
    QString game() const { return m_game; }
private:
    struct Plan {
        std::vector<int> attributes, categories, counts;
        std::vector<double> factors, weights;
        double worst = 0, best = 0;
    };
    static bool prepare(Plan &plan);
    QString m_game;
    QMap<QString, Plan> m_roles;
};
} // namespace fm
