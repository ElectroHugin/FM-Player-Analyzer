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

// FM27 export-header -> internal-column mapping. FM27 has no export of its
// own; the headers are those the FM View Exporter writes, i.e. the full column
// names the game shows (english UI). Reference: docs/IMPORT-FM27.md and the
// exporter's docs/columns/fm27.csv (column analyzer_field). The game has no
// "Reg" column any more, so registration status is not imported.
static const QHash<QString, QString> &fm27AttributeMapping()
{
    static const QHash<QString, QString> mapping = {
        {QStringLiteral("Unique ID"), QStringLiteral("Unique ID")},
        {QStringLiteral("Player"), QStringLiteral("Name")},
        {QStringLiteral("Inf"), QStringLiteral("Information")},
        {QStringLiteral("Age"), QStringLiteral("Age")},
        {QStringLiteral("Wage"), QStringLiteral("Wage")},
        {QStringLiteral("Transfer Value"), QStringLiteral("Transfer Value")},
        // "Nation" appears twice: the first column (code, e.g. "GER") wins,
        // like FM24's "Nat"; the second one (full name) is skipped as a
        // duplicate header.
        {QStringLiteral("Nation"), QStringLiteral("Nationality")},
        {QStringLiteral("2nd Nat"), QStringLiteral("Second Nationality")},
        {QStringLiteral("Position"), QStringLiteral("Position")},
        {QStringLiteral("Personality"), QStringLiteral("Personality")},
        {QStringLiteral("Media Handling"), QStringLiteral("Media Handling")},
        {QStringLiteral("Rating"), QStringLiteral("Average Rating")},
        {QStringLiteral("Left Foot"), QStringLiteral("Left Foot")},
        {QStringLiteral("Right Foot"), QStringLiteral("Right Foot")},
        {QStringLiteral("Preferred Foot"), QStringLiteral("Preferred Foot")},
        {QStringLiteral("Height"), QStringLiteral("Height")},
        {QStringLiteral("Club"), QStringLiteral("Club")},
        {QStringLiteral("Playing Time"), QStringLiteral("Agreed Playing Time")},
        // Goalkeeping
        {QStringLiteral("Aerial Reach"), QStringLiteral("Aerial Reach")},
        {QStringLiteral("Command Of Area"), QStringLiteral("Command of Area")},
        {QStringLiteral("Handling"), QStringLiteral("Handling")},
        {QStringLiteral("Kicking"), QStringLiteral("Kicking")},
        {QStringLiteral("One On Ones"), QStringLiteral("One vs One")},
        {QStringLiteral("Reflexes"), QStringLiteral("Reflexes")},
        {QStringLiteral("Rushing Out (Tendency)"), QStringLiteral("Rushing Out (Tendency)")},
        {QStringLiteral("Throwing"), QStringLiteral("Throwing")},
        // Mental
        {QStringLiteral("Aggression"), QStringLiteral("Aggression")},
        {QStringLiteral("Anticipation"), QStringLiteral("Anticipation")},
        {QStringLiteral("Bravery"), QStringLiteral("Bravery")},
        {QStringLiteral("Composure"), QStringLiteral("Composure")},
        {QStringLiteral("Concentration"), QStringLiteral("Concentration")},
        {QStringLiteral("Decisions"), QStringLiteral("Decisions")},
        {QStringLiteral("Determination"), QStringLiteral("Determination")},
        {QStringLiteral("Flair"), QStringLiteral("Flair")},
        {QStringLiteral("Leadership"), QStringLiteral("Leadership")},
        {QStringLiteral("Off The Ball"), QStringLiteral("Off the Ball")},
        {QStringLiteral("Positioning"), QStringLiteral("Positioning")},
        {QStringLiteral("Team Work"), QStringLiteral("Teamwork")},
        {QStringLiteral("Vision"), QStringLiteral("Vision")},
        {QStringLiteral("Work Rate"), QStringLiteral("Work Rate")},
        // Physical
        {QStringLiteral("Acceleration"), QStringLiteral("Acceleration")},
        {QStringLiteral("Agility"), QStringLiteral("Agility")},
        {QStringLiteral("Balance"), QStringLiteral("Balance")},
        {QStringLiteral("Jumping Reach"), QStringLiteral("Jumping Reach")},
        {QStringLiteral("Pace"), QStringLiteral("Pace")},
        {QStringLiteral("Stamina"), QStringLiteral("Stamina")},
        {QStringLiteral("Strength"), QStringLiteral("Strength")},
        // Technical
        {QStringLiteral("Corners"), QStringLiteral("Corners")},
        {QStringLiteral("Crossing"), QStringLiteral("Crossing")},
        {QStringLiteral("Dribbling"), QStringLiteral("Dribbling")},
        {QStringLiteral("Finishing"), QStringLiteral("Finishing")},
        {QStringLiteral("First Touch"), QStringLiteral("First Touch")},
        {QStringLiteral("Heading"), QStringLiteral("Heading")},
        {QStringLiteral("Long Shots"), QStringLiteral("Long Shots")},
        {QStringLiteral("Marking"), QStringLiteral("Marking")},
        {QStringLiteral("Passing"), QStringLiteral("Passing")},
        {QStringLiteral("Tackling"), QStringLiteral("Tackling")},
        {QStringLiteral("Technique"), QStringLiteral("Technique")},
    };
    return mapping;
}

static bool isFm27(const QString &versionId)
{
    return versionId == QLatin1String("fm27");
}

const QHash<QString, QString> &attributeMapping(const QString &versionId)
{
    // Dispatch on the FM version. New releases get another branch here; unknown
    // or empty ids fall back to the default version.
    if (isFm27(versionId))
        return fm27AttributeMapping();
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

// FM27 columns the app does not read: every other column the game offers
// (from the exporter's docs/columns/fm27.csv), so only headers the game
// renamed or added in a patch show up as unknown.
static const QSet<QString> &fm27IgnoredExportColumns()
{
    static const QSet<QString> ignored = {
        QStringLiteral("Best Pos"), QStringLiteral("Best Role"), QStringLiteral("Eccentricity"),
        QStringLiteral("Communication"), QStringLiteral("Punching"),
        QStringLiteral("Natural Fitness"), QStringLiteral("Free Kick Taking"),
        QStringLiteral("Long Throws"), QStringLiteral("Penalty Taking"),
        QStringLiteral("Home-Grown Status"), QStringLiteral("Media Description"),
        QStringLiteral("Recent Changes (Last 2 Weeks)"), QStringLiteral("Playing Time Happiness"),
        QStringLiteral("ShT"), QStringLiteral("Tck/90"), QStringLiteral("Shot/90"),
        QStringLiteral("Shot %"), QStringLiteral("ShT/90"),
        QStringLiteral("Shots From Outside The Box Per 90 minutes"), QStringLiteral("Shots"),
        QStringLiteral("Goals From Outside The Box"), QStringLiteral("Free Kick Shots"),
        QStringLiteral("xG/shot"), QStringLiteral("Conv %"), QStringLiteral("Svt"),
        QStringLiteral("Svp"), QStringLiteral("Svh"), QStringLiteral("Sv %"),
        QStringLiteral("xSv %"), QStringLiteral("xGP/90"), QStringLiteral("xGP"),
        QStringLiteral("Pr passes/90"), QStringLiteral("PsP"), QStringLiteral("Poss Lost/90"),
        QStringLiteral("Ps C/90"), QStringLiteral("Ps C"), QStringLiteral("Ps A/90"),
        QStringLiteral("Pas A"), QStringLiteral("Pas %"), QStringLiteral("OP-KP/90"),
        QStringLiteral("KP/90"), QStringLiteral("Key"), QStringLiteral("CCC"),
        QStringLiteral("Ch C/90"), QStringLiteral("Asts/90"), QStringLiteral("Off"),
        QStringLiteral("Sprints/90"), QStringLiteral("Drb/90"), QStringLiteral("Drb"),
        QStringLiteral("Dist/90"), QStringLiteral("Distance"), QStringLiteral("MLG"),
        QStringLiteral("OP-Crs C/90"), QStringLiteral("OP-Crs C"), QStringLiteral("OP-Crs A/90"),
        QStringLiteral("OP-Crs A"), QStringLiteral("OP-Cr %"), QStringLiteral("Cr C/90"),
        QStringLiteral("Cr C"), QStringLiteral("Crs A/90"), QStringLiteral("Cr A"),
        QStringLiteral("Cr C/A"), QStringLiteral("K Hdrs/90"), QStringLiteral("Hdr %"),
        QStringLiteral("Hdrs W/90"), QStringLiteral("Hdrs"), QStringLiteral("Hdrs L/90"),
        QStringLiteral("Aer A/90"), QStringLiteral("Hdrs A"), QStringLiteral("AT League Goals"),
        QStringLiteral("AT League Apps"), QStringLiteral("AT Gls"), QStringLiteral("AT Apps"),
        QStringLiteral("Form"), QStringLiteral("Form International"), QStringLiteral("Games Won"),
        QStringLiteral("Games Missed In A Row"), QStringLiteral("Games Lost"),
        QStringLiteral("Games Drawn"), QStringLiteral("Game Win Ratio"), QStringLiteral("Fouls"),
        QStringLiteral("Fouls Against"), QStringLiteral("Club Form"),
        QStringLiteral("Average Rating International"), QStringLiteral("Last 5 Games Club"),
        QStringLiteral("Last 5 Games International"), QStringLiteral("Last Match Rating"),
        QStringLiteral("PoM"), QStringLiteral("Starts"), QStringLiteral("Tcon/90"),
        QStringLiteral("Tgls/90"), QStringLiteral("Saves/90"), QStringLiteral("Yel"),
        QStringLiteral("xG"), QStringLiteral("Tcon"), QStringLiteral("Team Goals"),
        QStringLiteral("Red"), QStringLiteral("Pts/Gm"), QStringLiteral("Pen/R"),
        QStringLiteral("Pens S"), QStringLiteral("Pens Saved Ratio"), QStringLiteral("Pens Saved"),
        QStringLiteral("Pens Faced"), QStringLiteral("Pens"), QStringLiteral("NP-xG/90"),
        QStringLiteral("NP-xG"), QStringLiteral("Minutes Since Last Goal"),
        QStringLiteral("Mins/Gm"), QStringLiteral("Minutes Since Last Conceded"),
        QStringLiteral("Minutes"), QStringLiteral("Goals per 90 minutes"),
        QStringLiteral("Goals Conceded"), QStringLiteral("Goals"), QStringLiteral("xG/90"),
        QStringLiteral("xG-OP"), QStringLiteral("xA/90"), QStringLiteral("xA"),
        QStringLiteral("Con/90"), QStringLiteral("Cln/90"), QStringLiteral("Clean Sheets"),
        QStringLiteral("Apps"), QStringLiteral("Mins/Gl"), QStringLiteral("Average Rating Club"),
        QStringLiteral("Assists"), QStringLiteral("Contract Start Date"),
        QStringLiteral("Wage Of Offered Contract"), QStringLiteral("Expires"),
        QStringLiteral("Division"), QStringLiteral("Based In"), QStringLiteral("Cons"),
        QStringLiteral("Pros"), QStringLiteral("Style"), QStringLiteral("Potential"),
        QStringLiteral("Role Ability Out Of Possession"), QStringLiteral("Role Ability"),
        QStringLiteral("Ability"), QStringLiteral("Yth Apps"), QStringLiteral("National Team"),
        QStringLiteral("Int Gls"), QStringLiteral("Int Apps"), QStringLiteral("K Tck/90"),
        QStringLiteral("K Tck"), QStringLiteral("Int/90"), QStringLiteral("Itc"),
        QStringLiteral("Clr/90"), QStringLiteral("Clearances"), QStringLiteral("Blk/90"),
        QStringLiteral("Poss Won/90"), QStringLiteral("Pres A"), QStringLiteral("Pres A/90"),
        QStringLiteral("Pres C"), QStringLiteral("Pres C/90"), QStringLiteral("Shts Blckd"),
        QStringLiteral("Shts Blckd/90"), QStringLiteral("Tck R"), QStringLiteral("Tck A"),
        QStringLiteral("Tck C"), QStringLiteral("Blk"), QStringLiteral("Appearance Fee"),
        QStringLiteral("Match Highest Earner Clause"), QStringLiteral("Extension After Promotion"),
        QStringLiteral("Minimum Fee Release Clause"), QStringLiteral("Wage Contribution"),
        QStringLiteral("Wage After Tax"), QStringLiteral("Transfer Options"),
        QStringLiteral("Status Of Offered Contract"), QStringLiteral("Set For Release"),
        QStringLiteral("Loan Duration"), QStringLiteral("Contract Status"),
        QStringLiteral("FFP Contribution"), QStringLiteral("Expiry Date Of Offered Contract"),
        QStringLiteral("Top Division Relegation Wage Drop"),
        QStringLiteral("Wage After Reaching Club League Games"),
        QStringLiteral("Top Division Promotion Wage Rise"),
        QStringLiteral("Sell On Fee Percentage"), QStringLiteral("Relegation Wage Drop"),
        QStringLiteral("Promotion Wage Rise"),
        QStringLiteral("Percentage of Wages paid by Sponsor"),
        QStringLiteral("Optional Contract Extension By Club"),
        QStringLiteral("One-Year Extension After League Games (Promoted Final Season)"),
        QStringLiteral("One-Year Extension After League Games (Final Season)"),
        QStringLiteral("One-Year Extension After League Games (Avoid Relegation Final Season)"),
        QStringLiteral("Unused Substitute Fee"),
        QStringLiteral("Seasonal Landmark Goal Bonus - Number Of Goals"),
        QStringLiteral("Seasonal Landmark Goal Bonus"),
        QStringLiteral("Seasonal Landmark Combined Goal and Assist Bonus - Number Of Combined Goals and Assists"),
        QStringLiteral("Seasonal Landmark Combined Goal and Assist Bonus"),
        QStringLiteral("Seasonal Landmark Assist Bonus - Number Of Assists"),
        QStringLiteral("Seasonal Landmark Assist Bonus"), QStringLiteral("Int Cap Bonus"),
        QStringLiteral("Goal Bonus"), QStringLiteral("Clean Sheet Bonus"),
        QStringLiteral("Assist Bonus"), QStringLiteral("Active Non Promotion Release Clause"),
        QStringLiteral("Non-Playing Job Offer Release Clause"),
        QStringLiteral("Relegation Release Clause"), QStringLiteral("Injury Release Clause"),
        QStringLiteral("Active Relegation Release Clause"), QStringLiteral("Yearly Wage Rise"),
        QStringLiteral("Hierarchy"), QStringLiteral("Waive Club Compensation For Managerial Role"),
        QStringLiteral("Happiness"), QStringLiteral("Contract Type"),
        QStringLiteral("Non Promotion Release Clause"),
        QStringLiteral("Minimum Fee Release Clause (Foreign Clubs) - Expiry Date"),
        QStringLiteral("Minimum Fee Release Clause (Foreign Clubs)"),
        QStringLiteral("Minimum Fee Release Clause (Domestic Clubs) - Expiry Date"),
        QStringLiteral("Minimum Fee Release Clause (Domestic Clubs)"),
        QStringLiteral("Minimum Fee Release Clause (Domestic Clubs in Higher Division) - Expiry Date"),
        QStringLiteral("Minimum Fee Release Clause (Domestic Clubs in Higher Division)"),
        QStringLiteral("Minimum Fee Release Clause (Clubs in a Major Continental Competition) - Expiry Date"),
        QStringLiteral("Minimum Fee Release Clause (Clubs in a Major Continental Competition)"),
        QStringLiteral("Minimum Fee Release Clause (Clubs in a Continental Competition) - Expiry Date"),
        QStringLiteral("Minimum Fee Release Clause (Clubs in a Continental Competition)"),
        QStringLiteral("Minimum Fee Release Clause - Expiry Date"),
        QStringLiteral("Social Groups"), QStringLiteral("Short-term Plans"),
        QStringLiteral("Promises"), QStringLiteral("Progress"),
        QStringLiteral("Overall Happiness"), QStringLiteral("Morale"),
        QStringLiteral("Long-Term Plans"), QStringLiteral("Birth Region"),
        QStringLiteral("International Happiness"), QStringLiteral("Prom."),
        QStringLiteral("Pref."), QStringLiteral("Nation of Birth"),
        QStringLiteral("Favoured Personnel"), QStringLiteral("Favoured Clubs"),
        QStringLiteral("EU National"), QStringLiteral("Eligible"), QStringLiteral("Due Date"),
        QStringLiteral("DoB"), QStringLiteral("Sec. Position"), QStringLiteral("No."),
        QStringLiteral("World Reputation"), QStringLiteral("Training Load"),
        QStringLiteral("Time Missed"), QStringLiteral("Recurring Injury"),
        QStringLiteral("Overall Risk"), QStringLiteral("CON"), QStringLiteral("SHP"),
        QStringLiteral("Match Load"), QStringLiteral("Injury Susceptibility"),
        QStringLiteral("Injury Risk"), QStringLiteral("Injury preventing training"),
        QStringLiteral("Injury"), QStringLiteral("Injured On"),
        QStringLiteral("International Assists"), QStringLiteral("International Goals Conceded"),
        QStringLiteral("Fatigue"), QStringLiteral("International Appearances (Season)"),
        QStringLiteral("Yth Gls"), QStringLiteral("Effectiveness"),
        QStringLiteral("Form with Value"), QStringLiteral("Tactical Understanding"),
        QStringLiteral("Next Match Info"), QStringLiteral("Player Buy-In"),
        QStringLiteral("Player Ability"), QStringLiteral("Quality of Next Opposition Report"),
        QStringLiteral("Opposition Player Knowledge"), QStringLiteral("Passing Style"),
        QStringLiteral("Build Up"), QStringLiteral("Chance Creation"),
        QStringLiteral("In Possession Shape"), QStringLiteral("Line of Engagement"),
        QStringLiteral("Pressing Intensity"), QStringLiteral("Pressing Trap"),
        QStringLiteral("Out Of Possession Shape"), QStringLiteral("Player Tactical Ability"),
        QStringLiteral("Tactical Complexity"), QStringLiteral("Player Trait"),
        QStringLiteral("Position/Role"), QStringLiteral("Training Happiness"),
        QStringLiteral("Training Happiness Details"), QStringLiteral("Training Rating"),
        QStringLiteral("Workload"), QStringLiteral("Manager Tendencies"),
        QStringLiteral("Teammates and Staff Relationships"), QStringLiteral("Managerial Support"),
        QStringLiteral("Player Role Suitability"), QStringLiteral("Player Capability"),
        QStringLiteral("Tactical Comprehension"), QStringLiteral("WP Needed"),
        QStringLiteral("Fitness"), QStringLiteral("Goalkeeping"),
        QStringLiteral("Possession - Technical"), QStringLiteral("Possession - Tactical"),
        QStringLiteral("Defending - Technical"), QStringLiteral("Attacking - Tactical"),
        QStringLiteral("Additional Focus"), QStringLiteral("Set Pieces"),
        QStringLiteral("Overall Training Improvement"), QStringLiteral("Postion Suitability"),
        QStringLiteral("Last Transfer Fee"), QStringLiteral("Loan Status"),
        QStringLiteral("On Loan From"), QStringLiteral("Overall Fee"),
        QStringLiteral("Player's Interest"), QStringLiteral("Transfer Fees Received"),
        QStringLiteral("Transfer Status"), QStringLiteral("Type"),
        QStringLiteral("Current Year +1 Future Playing Time"),
        QStringLiteral("Work Permit Required"), QStringLiteral("Fee"),
        QStringLiteral("Current Year +4 Future Playing Time"),
        QStringLiteral("Current Year +3 Future Playing Time"),
        QStringLiteral("Current Year +2 Future Playing Time"), QStringLiteral("Asking Price"),
        QStringLiteral("Agent"), QStringLiteral("Actual Playing Time"),
        QStringLiteral("Future Playing Time"), QStringLiteral("Last Club")
    };
    return ignored;
}

const QSet<QString> &ignoredExportColumns(const QString &versionId)
{
    if (isFm27(versionId))
        return fm27IgnoredExportColumns();
    return fm24IgnoredExportColumns();
}

static const QStringList &fm24RequiredImportColumns()
{
    static const QStringList required = {QStringLiteral("UID"), QStringLiteral("Name")};
    return required;
}

static const QStringList &fm27RequiredImportColumns()
{
    static const QStringList required = {QStringLiteral("Unique ID"), QStringLiteral("Player")};
    return required;
}

const QStringList &requiredImportColumns(const QString &versionId)
{
    if (isFm27(versionId))
        return fm27RequiredImportColumns();
    return fm24RequiredImportColumns();
}

} // namespace fm
