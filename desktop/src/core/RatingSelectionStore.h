#pragma once
#include "RatingProfile.h"
#include <QJsonArray>
#include <memory>
namespace fm {
class Database;
class Definitions;
class ClassicRatingSnapshot;
struct RatingSelection {
    QString game, generation, computationKey, migrationDecision;
    RatingProfile profile;
    QJsonObject definitions, legacyBasis;
    QJsonArray switches;
};
// Self-contained selection inside one save. Never changes historical ratings.
// The caller publishes/recalculates only after a successful committed result.
class RatingSelectionStore {
public:
    explicit RatingSelectionStore(Database &database) : m_database(database) {}
    std::optional<RatingSelection> load(QString *error);
    std::optional<RatingSelection> initialize(const RatingProfile &profile, const Definitions &definitions,
        bool existingSave, std::shared_ptr<const ClassicRatingSnapshot> legacy, QString *error);
    std::optional<RatingSelection> activate(const RatingProfile &profile, const Definitions &definitions,
        const QString &expectedGeneration, const QString &saveDate, QString *error);
    std::optional<RatingSelection> keepPrevious(const QString &expectedGeneration, QString *error);
    static QString documentName();
private:
    bool write(const RatingSelection &selection, QString *error);
    Database &m_database;
};
}
