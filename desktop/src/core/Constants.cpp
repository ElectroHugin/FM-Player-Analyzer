#include "Constants.h"

#include "Attributes.h"

namespace fm {

const std::array<QString, kAttrCount> &attrNames()
{
    static const std::array<QString, kAttrCount> names = {
        QStringLiteral("One vs One"),
        QStringLiteral("Acceleration"),
        QStringLiteral("Aerial Reach"),
        QStringLiteral("Aggression"),
        QStringLiteral("Agility"),
        QStringLiteral("Anticipation"),
        QStringLiteral("Balance"),
        QStringLiteral("Bravery"),
        QStringLiteral("Command of Area"),
        QStringLiteral("Concentration"),
        QStringLiteral("Composure"),
        QStringLiteral("Corners"),
        QStringLiteral("Crossing"),
        QStringLiteral("Decisions"),
        QStringLiteral("Determination"),
        QStringLiteral("Dribbling"),
        QStringLiteral("Finishing"),
        QStringLiteral("First Touch"),
        QStringLiteral("Flair"),
        QStringLiteral("Handling"),
        QStringLiteral("Heading"),
        QStringLiteral("Jumping Reach"),
        QStringLiteral("Kicking"),
        QStringLiteral("Leadership"),
        QStringLiteral("Long Shots"),
        QStringLiteral("Marking"),
        QStringLiteral("Off the Ball"),
        QStringLiteral("Pace"),
        QStringLiteral("Passing"),
        QStringLiteral("Positioning"),
        QStringLiteral("Reflexes"),
        QStringLiteral("Rushing Out (Tendency)"),
        QStringLiteral("Stamina"),
        QStringLiteral("Strength"),
        QStringLiteral("Tackling"),
        QStringLiteral("Teamwork"),
        QStringLiteral("Technique"),
        QStringLiteral("Throwing"),
        QStringLiteral("Vision"),
        QStringLiteral("Work Rate"),
    };
    return names;
}

int attrIndexByName(const QString &fullName)
{
    static const QHash<QString, int> index = [] {
        QHash<QString, int> h;
        const auto &names = attrNames();
        for (int i = 0; i < kAttrCount; ++i)
            h.insert(names[i], i);
        return h;
    }();
    return index.value(fullName, -1);
}

const QList<FmDataVersion> &fmDataVersions()
{
    // Registry of Football Manager releases, newest first. FM24 keeps the
    // unsuffixed "definitions.json" its users already have; later releases get
    // "definitions_<id>.json". See Constants.h for what a new release needs.
    static const QList<FmDataVersion> versions = {
        {QStringLiteral("fm24"), QStringLiteral("Football Manager 2024"), true,
         QStringLiteral("definitions.json"), QStringLiteral("Football Manager 2024"),
         {QStringLiteral("html"), QStringLiteral("csv")}, /*provisionalWeights*/ false, /*registrationRules*/ true},
    };
    return versions;
}

QString defaultFmVersionId()
{
    return QStringLiteral("fm24");
}

QString newestFmVersionId()
{
    for (const FmDataVersion &version : fmDataVersions()) {
        if (version.supported)
            return version.id;
    }
    return defaultFmVersionId();
}

const FmDataVersion &fmDataVersion(const QString &versionId)
{
    const QList<FmDataVersion> &versions = fmDataVersions();
    for (const FmDataVersion &version : versions) {
        if (version.id == versionId)
            return version;
    }
    for (const FmDataVersion &version : versions) {
        if (version.id == defaultFmVersionId())
            return version;
    }
    return versions.first();
}

const QHash<QString, QString> &personalityDefaults()
{
    static const QHash<QString, QString> defaults = {
        // Good (positive)
        {QStringLiteral("Model Citizen"), QStringLiteral("good")},
        {QStringLiteral("Perfectionist"), QStringLiteral("good")},
        {QStringLiteral("Resolute"), QStringLiteral("good")},
        {QStringLiteral("Model Professional"), QStringLiteral("good")},
        {QStringLiteral("Professional"), QStringLiteral("good")},
        {QStringLiteral("Fairly Professional"), QStringLiteral("good")},
        {QStringLiteral("Spirited"), QStringLiteral("good")},
        {QStringLiteral("Driven"), QStringLiteral("good")},
        {QStringLiteral("Determined"), QStringLiteral("good")},
        {QStringLiteral("Fairly Determined"), QStringLiteral("good")},
        {QStringLiteral("Iron Willed"), QStringLiteral("good")},
        {QStringLiteral("Resilient"), QStringLiteral("good")},
        {QStringLiteral("Charismatic Leader"), QStringLiteral("good")},
        {QStringLiteral("Born Leader"), QStringLiteral("good")},
        {QStringLiteral("Leader"), QStringLiteral("good")},
        {QStringLiteral("Very Ambitious"), QStringLiteral("good")},
        {QStringLiteral("Ambitious"), QStringLiteral("good")},
        {QStringLiteral("Fairly Ambitious"), QStringLiteral("good")},
        {QStringLiteral("Fickle"), QStringLiteral("good")},
        {QStringLiteral("Mercenary"), QStringLiteral("good")},
        // Bad (negative)
        {QStringLiteral("Slack"), QStringLiteral("bad")},
        {QStringLiteral("Casual"), QStringLiteral("bad")},
        {QStringLiteral("Temperamental"), QStringLiteral("bad")},
        {QStringLiteral("Easily Discouraged"), QStringLiteral("bad")},
        {QStringLiteral("Low Determination"), QStringLiteral("bad")},
        {QStringLiteral("Spineless"), QStringLiteral("bad")},
        {QStringLiteral("Low Self-Belief"), QStringLiteral("bad")},
        {QStringLiteral("Unambitious"), QStringLiteral("bad")},
        // Neutral
        {QStringLiteral("Jovial"), QStringLiteral("neutral")},
        {QStringLiteral("Light-Hearted"), QStringLiteral("neutral")},
        {QStringLiteral("Devoted"), QStringLiteral("neutral")},
        {QStringLiteral("Very Loyal"), QStringLiteral("neutral")},
        {QStringLiteral("Loyal"), QStringLiteral("neutral")},
        {QStringLiteral("Fairly Loyal"), QStringLiteral("neutral")},
        {QStringLiteral("Honest"), QStringLiteral("neutral")},
        {QStringLiteral("Sporting"), QStringLiteral("neutral")},
        {QStringLiteral("Fairly Sporting"), QStringLiteral("neutral")},
        {QStringLiteral("Unsporting"), QStringLiteral("neutral")},
        {QStringLiteral("Realist"), QStringLiteral("neutral")},
        {QStringLiteral("Balanced"), QStringLiteral("neutral")},
    };
    return defaults;
}

const QStringList &fieldPlayerAptOptions()
{
    static const QStringList options = {
        QStringLiteral("None"), QStringLiteral("Star Player"), QStringLiteral("Important Player"),
        QStringLiteral("Regular Starter"), QStringLiteral("Squad Player"), QStringLiteral("Impact Sub"),
        QStringLiteral("Fringe Player"), QStringLiteral("Breakthrough Prospect"),
        QStringLiteral("Future Prospect"), QStringLiteral("Youngster"), QStringLiteral("Emergency Backup"),
        QStringLiteral("2nd Team Regular"), QStringLiteral("Surplus to Requirements"),
    };
    return options;
}

const QStringList &gkAptOptions()
{
    static const QStringList options = {
        QStringLiteral("None"), QStringLiteral("Star Player"), QStringLiteral("Important Player"),
        QStringLiteral("First-Choice Goalkeeper"), QStringLiteral("Cup Goalkeeper"),
        QStringLiteral("Backup"), QStringLiteral("Breakthrough Prospect"), QStringLiteral("Future Prospect"),
        QStringLiteral("Youngster"), QStringLiteral("Emergency Backup"),
        QStringLiteral("Surplus to Requirements"),
    };
    return options;
}

const QHash<QString, QString> &aptAbbreviations()
{
    static const QHash<QString, QString> abbreviations = {
        {QStringLiteral("Important Player"), QStringLiteral("Important")},
        {QStringLiteral("Breakthrough Prospect"), QStringLiteral("Prospect")},
        {QStringLiteral("Future Prospect"), QStringLiteral("Future")},
        {QStringLiteral("Emergency Backup"), QStringLiteral("Backup")},
        {QStringLiteral("2nd Team Regular"), QStringLiteral("2nd Team")},
        {QStringLiteral("Surplus to Requirements"), QStringLiteral("Surplus")},
    };
    return abbreviations;
}

} // namespace fm
