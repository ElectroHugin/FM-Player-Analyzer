#include "AttributeTiers.h"

namespace fm {

// FM24 importance tiers (legacy constants.py). A new release starts from these
// as an assumption until its match engine has been tested.
static const QHash<QString, QString> &fm24StatCategories()
{
    static const QHash<QString, QString> categories = {
        {QStringLiteral("Pace"), QStringLiteral("Extremely Important")},
        {QStringLiteral("Acceleration"), QStringLiteral("Extremely Important")},
        {QStringLiteral("Jumping Reach"), QStringLiteral("Important")},
        {QStringLiteral("Anticipation"), QStringLiteral("Important")},
        {QStringLiteral("Balance"), QStringLiteral("Important")},
        {QStringLiteral("Agility"), QStringLiteral("Important")},
        {QStringLiteral("Concentration"), QStringLiteral("Important")},
        {QStringLiteral("Finishing"), QStringLiteral("Important")},
        {QStringLiteral("Work Rate"), QStringLiteral("Good")},
        {QStringLiteral("Dribbling"), QStringLiteral("Good")},
        {QStringLiteral("Stamina"), QStringLiteral("Good")},
        {QStringLiteral("Strength"), QStringLiteral("Good")},
        {QStringLiteral("Passing"), QStringLiteral("Good")},
        {QStringLiteral("Determination"), QStringLiteral("Good")},
        {QStringLiteral("Vision"), QStringLiteral("Good")},
        {QStringLiteral("Long Shots"), QStringLiteral("Decent")},
        {QStringLiteral("Marking"), QStringLiteral("Decent")},
        {QStringLiteral("Decisions"), QStringLiteral("Decent")},
        {QStringLiteral("First Touch"), QStringLiteral("Decent")},
        {QStringLiteral("Off the Ball"), QStringLiteral("Almost Irrelevant")},
        {QStringLiteral("Tackling"), QStringLiteral("Almost Irrelevant")},
        {QStringLiteral("Teamwork"), QStringLiteral("Almost Irrelevant")},
        {QStringLiteral("Composure"), QStringLiteral("Almost Irrelevant")},
        {QStringLiteral("Technique"), QStringLiteral("Almost Irrelevant")},
        {QStringLiteral("Positioning"), QStringLiteral("Almost Irrelevant")},
    };
    return categories;
}

// FM24 goalkeeper tiers, derived on 2026-10-10 from season statistics of 335
// keepers (expected goals prevented per 90, attributes controlled for overall
// level). All 13 goalkeeping attributes the app imports take part; the legacy
// scheme rated only six and predicted xGP/90 clearly worse (r 0.43 vs 0.58).
// Role-listed attributes outside these tiers count as "Key"/"Preferable"
// (see DwrsEngine::buildPlan).
static const QHash<QString, QString> &fm24GkStatCategories()
{
    static const QHash<QString, QString> categories = {
        {QStringLiteral("Positioning"), QStringLiteral("Top Importance")},
        {QStringLiteral("Aerial Reach"), QStringLiteral("Top Importance")},
        {QStringLiteral("Handling"), QStringLiteral("Top Importance")},
        {QStringLiteral("Agility"), QStringLiteral("High Importance")},
        {QStringLiteral("Anticipation"), QStringLiteral("High Importance")},
        {QStringLiteral("Kicking"), QStringLiteral("High Importance")},
        {QStringLiteral("Reflexes"), QStringLiteral("Medium Importance")},
        {QStringLiteral("Command of Area"), QStringLiteral("Medium Importance")},
        {QStringLiteral("Concentration"), QStringLiteral("Medium Importance")},
        {QStringLiteral("Throwing"), QStringLiteral("Medium Importance")},
        {QStringLiteral("Bravery"), QStringLiteral("Other")},
        {QStringLiteral("One vs One"), QStringLiteral("Other")},
        {QStringLiteral("Rushing Out (Tendency)"), QStringLiteral("Other")},
    };
    return categories;
}

static const QHash<QString, double> &fm24WeightDefaults()
{
    static const QHash<QString, double> defaults = {
        {QStringLiteral("Extremely Important"), 8.0},
        {QStringLiteral("Important"), 4.0},
        {QStringLiteral("Good"), 2.0},
        {QStringLiteral("Decent"), 1.0},
        {QStringLiteral("Almost Irrelevant"), 0.2},
    };
    return defaults;
}

// Goalkeeper tier weights (scheme 2, see fm24GkStatCategories). Key/Preferable
// stay small: outfield attributes on goalkeeper role lists (Decisions, Vision,
// ...) are often unscouted and then read as 0.
static const QHash<QString, double> &fm24GkWeightDefaults()
{
    static const QHash<QString, double> defaults = {
        {QStringLiteral("Top Importance"), 3.0},
        {QStringLiteral("High Importance"), 2.0},
        {QStringLiteral("Medium Importance"), 1.0},
        {QStringLiteral("Key"), 0.5},
        {QStringLiteral("Preferable"), 0.25},
        {QStringLiteral("Other"), 0.2},
    };
    return defaults;
}

// Dispatch on the FM version like attributeMapping(): new releases get another
// branch; unknown or empty ids fall back to the default version.
const QHash<QString, QString> &globalStatCategories(const QString &versionId)
{
    Q_UNUSED(versionId); // only FM24 exists today
    return fm24StatCategories();
}

const QHash<QString, QString> &gkStatCategories(const QString &versionId)
{
    Q_UNUSED(versionId);
    return fm24GkStatCategories();
}

const QHash<QString, double> &weightDefaults(const QString &versionId)
{
    Q_UNUSED(versionId);
    return fm24WeightDefaults();
}

const QHash<QString, double> &gkWeightDefaults(const QString &versionId)
{
    Q_UNUSED(versionId);
    return fm24GkWeightDefaults();
}

} // namespace fm
