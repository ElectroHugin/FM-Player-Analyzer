#pragma once
#include "RatingProfile.h"
#include "Definitions.h"
#include <QList>

namespace fm {
class AppConfig;
// One game captures a save's custom settings; both games build the shared default.
std::optional<RatingProfile> classicRatingProfile(const QList<Definitions> &definitions,
                                                 const AppConfig &config, QString *error);
// Uses the reviewed source table, never an executable external expression.
std::optional<RatingProfile> harvestgreenRatingProfile(const QList<Definitions> &definitions,
                                                      const AppConfig &config,
                                                      const QJsonObject &measurements, QString *error);
} // namespace fm
