#pragma once
#include "core/RatingProfile.h"
#include <QList>
namespace fm {
QList<RatingProfile> loadBuiltinRatingProfiles(QString *error);
}
