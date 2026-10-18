#include "ImportSchema.h"

#include "Constants.h"

namespace fm {

// FM24 export-header -> internal-column mapping. The internal names on the
// right are the app's canonical attribute/column names and never change; a
// future FM release only needs its own header->internal map.
static const QHash<QString, QString> &fm24AttributeMapping()
{
    static const QHash<QString, QString> mapping = {
        {QStringLiteral("Reg"), QStringLiteral("Registration")},
        {QStringLiteral("Inf"), QStringLiteral("Information")},
        {QStringLiteral("Name"), QStringLiteral("Name")},
        {QStringLiteral("Age"), QStringLiteral("Age")},
        {QStringLiteral("Wage"), QStringLiteral("Wage")},
        {QStringLiteral("Transfer Value"), QStringLiteral("Transfer Value")},
        {QStringLiteral("Nat"), QStringLiteral("Nationality")},
        {QStringLiteral("2nd Nat"), QStringLiteral("Second Nationality")},
        {QStringLiteral("Position"), QStringLiteral("Position")},
        {QStringLiteral("Personality"), QStringLiteral("Personality")},
        {QStringLiteral("Media Handling"), QStringLiteral("Media Handling")},
        {QStringLiteral("Av Rat"), QStringLiteral("Average Rating")},
        {QStringLiteral("Left Foot"), QStringLiteral("Left Foot")},
        {QStringLiteral("Right Foot"), QStringLiteral("Right Foot")},
        {QStringLiteral("Height"), QStringLiteral("Height")},
        {QStringLiteral("1v1"), QStringLiteral("One vs One")},
        {QStringLiteral("Acc"), QStringLiteral("Acceleration")},
        {QStringLiteral("Aer"), QStringLiteral("Aerial Reach")},
        {QStringLiteral("Agg"), QStringLiteral("Aggression")},
        {QStringLiteral("Agi"), QStringLiteral("Agility")},
        {QStringLiteral("Ant"), QStringLiteral("Anticipation")},
        {QStringLiteral("Bal"), QStringLiteral("Balance")},
        {QStringLiteral("Bra"), QStringLiteral("Bravery")},
        {QStringLiteral("Cmd"), QStringLiteral("Command of Area")},
        {QStringLiteral("Cnt"), QStringLiteral("Concentration")},
        {QStringLiteral("Cmp"), QStringLiteral("Composure")},
        {QStringLiteral("Cro"), QStringLiteral("Crossing")},
        {QStringLiteral("Dec"), QStringLiteral("Decisions")},
        {QStringLiteral("Det"), QStringLiteral("Determination")},
        {QStringLiteral("Dri"), QStringLiteral("Dribbling")},
        {QStringLiteral("Fin"), QStringLiteral("Finishing")},
        {QStringLiteral("Fir"), QStringLiteral("First Touch")},
        {QStringLiteral("Fla"), QStringLiteral("Flair")},
        {QStringLiteral("Han"), QStringLiteral("Handling")},
        {QStringLiteral("Hea"), QStringLiteral("Heading")},
        {QStringLiteral("Jum"), QStringLiteral("Jumping Reach")},
        {QStringLiteral("Kic"), QStringLiteral("Kicking")},
        {QStringLiteral("Ldr"), QStringLiteral("Leadership")},
        {QStringLiteral("Lon"), QStringLiteral("Long Shots")},
        {QStringLiteral("Mar"), QStringLiteral("Marking")},
        {QStringLiteral("OtB"), QStringLiteral("Off the Ball")},
        {QStringLiteral("Pac"), QStringLiteral("Pace")},
        {QStringLiteral("Pas"), QStringLiteral("Passing")},
        {QStringLiteral("Pos"), QStringLiteral("Positioning")},
        {QStringLiteral("Ref"), QStringLiteral("Reflexes")},
        {QStringLiteral("Sta"), QStringLiteral("Stamina")},
        {QStringLiteral("Str"), QStringLiteral("Strength")},
        {QStringLiteral("Tck"), QStringLiteral("Tackling")},
        {QStringLiteral("Tea"), QStringLiteral("Teamwork")},
        {QStringLiteral("Tec"), QStringLiteral("Technique")},
        {QStringLiteral("Thr"), QStringLiteral("Throwing")},
        {QStringLiteral("TRO"), QStringLiteral("Rushing Out (Tendency)")},
        {QStringLiteral("Vis"), QStringLiteral("Vision")},
        {QStringLiteral("Wor"), QStringLiteral("Work Rate")},
        {QStringLiteral("UID"), QStringLiteral("Unique ID")},
        {QStringLiteral("Cor"), QStringLiteral("Corners")},
        {QStringLiteral("Club"), QStringLiteral("Club")},
        {QStringLiteral("Agreed Playing Time"), QStringLiteral("Agreed Playing Time")},
        {QStringLiteral("Preferred Foot"), QStringLiteral("Preferred Foot")},
    };
    return mapping;
}

const QHash<QString, QString> &attributeMapping(const QString &versionId)
{
    // Dispatch on the FM version. New releases get another branch here; unknown
    // or empty ids fall back to the default version.
    Q_UNUSED(versionId); // only FM24 exists today
    return fm24AttributeMapping();
}

const QHash<QString, QString> &attributeMapping()
{
    return attributeMapping(defaultFmVersionId());
}

// FM24 columns the app intentionally ignores (legacy IGNORED_EXPORT_COLUMNS).
static const QSet<QString> &fm24IgnoredExportColumns()
{
    static const QSet<QString> ignored = {
        QStringLiteral("CON"), QStringLiteral("Ability"),
        QStringLiteral("Position/Role/Duty"), QStringLiteral("Pun")};
    return ignored;
}

const QSet<QString> &ignoredExportColumns(const QString &versionId)
{
    Q_UNUSED(versionId); // only FM24 exists today
    return fm24IgnoredExportColumns();
}

static const QStringList &fm24RequiredImportColumns()
{
    static const QStringList required = {QStringLiteral("UID"), QStringLiteral("Name")};
    return required;
}

const QStringList &requiredImportColumns(const QString &versionId)
{
    Q_UNUSED(versionId); // only FM24 exists today
    return fm24RequiredImportColumns();
}

} // namespace fm
