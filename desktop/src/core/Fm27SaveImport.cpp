#include "Fm27SaveImport.h"

#include "Fm27Save.h"
#include "Utils.h"

#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace fm::Fm27SaveImport {

using namespace fm27save;

namespace {

std::atomic<bool> g_masking{true};

// Exporter column name for each visible save attribute. Hidden ones
// (Consistency, Dirtiness, Important Matches, Injury Proneness, Versatility)
// have no column and never leave this file.
struct VisibleAttr {
    int saveIndex;
    const char *column;
};
const VisibleAttr kVisible[] = {
    {Crossing, "Crossing"}, {Dribbling, "Dribbling"}, {Finishing, "Finishing"},
    {Heading, "Heading"}, {LongShots, "Long Shots"}, {Marking, "Marking"},
    {OffTheBall, "Off The Ball"}, {Passing, "Passing"}, {PenaltyTaking, "Penalty Taking"},
    {Tackling, "Tackling"}, {Vision, "Vision"}, {Handling, "Handling"},
    {AerialReach, "Aerial Reach"}, {CommandOfArea, "Command Of Area"},
    {Communication, "Communication"}, {Kicking, "Kicking"}, {Throwing, "Throwing"},
    {Anticipation, "Anticipation"}, {Decisions, "Decisions"}, {OneOnOnes, "One On Ones"},
    {Positioning, "Positioning"}, {Reflexes, "Reflexes"}, {FirstTouch, "First Touch"},
    {Technique, "Technique"}, {Flair, "Flair"}, {Corners, "Corners"}, {Teamwork, "Team Work"},
    {WorkRate, "Work Rate"}, {LongThrows, "Long Throws"}, {Eccentricity, "Eccentricity"},
    {RushingOut, "Rushing Out (Tendency)"}, {Punching, "Punching"},
    {Acceleration, "Acceleration"}, {FreeKickTaking, "Free Kick Taking"}, {Strength, "Strength"},
    {Stamina, "Stamina"}, {Pace, "Pace"}, {JumpingReach, "Jumping Reach"},
    {Leadership, "Leadership"}, {Balance, "Balance"}, {Bravery, "Bravery"},
    {Aggression, "Aggression"}, {Agility, "Agility"}, {NaturalFitness, "Natural Fitness"},
    {Determination, "Determination"}, {Composure, "Composure"}, {Concentration, "Concentration"},
};
constexpr int kVisibleCount = int(sizeof(kVisible) / sizeof(kVisible[0]));
static_assert(kVisibleCount == 47);

// Which attributes the game leaves as ranges first when it knows a player
// only partly, measured on 251 partly scouted players (outfield players:
// goalkeeping and set pieces first). Goalkeepers: the outfield skills first.
const int kOutfieldRangeOrder[] = {
    LongThrows, Kicking, Punching, RushingOut, CommandOfArea, AerialReach, Communication,
    Throwing, Handling, OneOnOnes, Reflexes, Corners, FreeKickTaking, Eccentricity, Marking,
    PenaltyTaking, Tackling, Anticipation, Concentration, Flair, Vision, Finishing, Crossing,
    Heading, OffTheBall, Positioning, LongShots, Aggression, Leadership, Determination,
    NaturalFitness, Passing, Stamina, Dribbling, Decisions, Teamwork, Composure, Technique,
    Bravery, Strength, WorkRate, FirstTouch, Balance, Agility, JumpingReach, Acceleration, Pace,
};
const int kGoalkeeperRangeOrder[] = {
    Dribbling, Finishing, Crossing, LongShots, Heading, OffTheBall, Marking, Tackling, Corners,
    FreeKickTaking, PenaltyTaking, LongThrows, Technique, Flair, Passing, Vision, FirstTouch,
    WorkRate, Teamwork, Leadership, Determination, Aggression, Anticipation, Bravery, Stamina,
    NaturalFitness, Balance, Strength, Pace, Acceleration, Agility, JumpingReach, Eccentricity,
    Punching, RushingOut, Throwing, Kicking, Communication, Composure, Concentration, Decisions,
    Positioning, CommandOfArea, OneOnOnes, AerialReach, Handling, Reflexes,
};

// A stable 64-bit mix (splitmix64): same input, same output, on every run.
quint64 mix(quint64 x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

double unit(quint32 uniqueId, int attribute, int salt)
{
    const quint64 h = mix((quint64(uniqueId) << 20) ^ (quint64(attribute) << 8) ^ quint64(salt));
    return double(h >> 11) * (1.0 / 9007199254740992.0);
}

// Range width (1-5) for one attribute: measured on partly scouted players
// (1: 13 %, 2: 43 %, 3: 33 %, 4: 10 %, 5: 1 %); a little narrower once the
// knowledge passes 70.
int rangeWidth(quint32 uniqueId, int attribute, int knowledge)
{
    const double u = unit(uniqueId, attribute, 1);
    int width = u < 0.13 ? 1 : u < 0.56 ? 2 : u < 0.89 ? 3 : u < 0.99 ? 4 : 5;
    if (knowledge >= 70)
        width = std::max(1, width - 1);
    return width;
}

QString attrText(int lo, int hi)
{
    return lo == hi ? QString::number(lo) : QStringLiteral("%1-%2").arg(lo).arg(hi);
}

// Money as the game writes it in euros: "€950", "€9.5K", "€375K", "€1.4M",
// "€72M". The save keeps pounds; the game's own rate converts them.
constexpr double kEurosPerPound = 1.1641;

QString moneyText(double euros)
{
    const auto trimmed = [](double v, int decimals) {
        QString t = QString::number(v, 'f', decimals);
        if (t.contains(QLatin1Char('.'))) {
            while (t.endsWith(QLatin1Char('0')))
                t.chop(1);
            if (t.endsWith(QLatin1Char('.')))
                t.chop(1);
        }
        return t;
    };
    if (euros >= 1e6)
        return QStringLiteral("€") + trimmed(euros / 1e6, euros >= 1e7 ? 0 : 1) + QStringLiteral("M");
    if (euros >= 1e3)
        return QStringLiteral("€") + trimmed(euros / 1e3, euros >= 1e4 ? 0 : 1) + QStringLiteral("K");
    return QStringLiteral("€") + QString::number(qRound(euros));
}

QString wageText(qint64 poundsPerWeek)
{
    return moneyText(double(poundsPerWeek) * kEurosPerPound) + QStringLiteral(" p/w");
}

// The game shows money it does not know exactly as a range ("€21K - €24.5K");
// the analyzer draws its own around the true value (stable per player and
// salt), hi/lo between the two ratios, the value never in the middle by rule.
QString moneyRangeText(quint32 uniqueId, int salt, double euros, double minRatio, double maxRatio,
                       const QString &suffix = QString())
{
    const double ratio = minRatio + (maxRatio - minRatio) * unit(uniqueId, 900 + salt, 11);
    const double place = 0.15 + 0.7 * unit(uniqueId, 950 + salt, 12); // where the value sits
    // lo * ratio^place = value  ->  lo = value / ratio^place, hi = lo * ratio
    const double lo = euros / std::pow(ratio, place);
    const double hi = lo * ratio;
    const QString loText = moneyText(lo), hiText = moneyText(hi);
    return (loText == hiText ? loText : loText + QStringLiteral(" - ") + hiText) + suffix;
}

// The contract's agreed playing time in the game's words ("" when unknown).
QString playingTimeText(int status, bool keeper)
{
    switch (status) {
    case 1: return QStringLiteral("Star Player");
    case 2: return QStringLiteral("Important Player");
    case 3: return QStringLiteral("Regular Starter");
    case 4: return QStringLiteral("Squad Player");
    case 5: return QStringLiteral("Impact Sub");
    case 7: return QStringLiteral("Fringe Player");
    case 9: return QStringLiteral("Emergency Backup");
    case 10: return QStringLiteral("Breakthrough Prospect");
    case 11: return QStringLiteral("Future Prospect");
    case 13: return QStringLiteral("Youngster");
    case 14: return QStringLiteral("2nd Team Regular");
    case 15: return QStringLiteral("First-Choice Goalkeeper");
    case 16:
    case 17:
    case 18: return QStringLiteral("Cup Goalkeeper");
    case 20: return QStringLiteral("Backup");
    case 21: return QStringLiteral("Emergency Backup");
    case 22: return QStringLiteral("Surplus to Requirements");
    default: break;
    }
    Q_UNUSED(keeper);
    return {};
}

// Whether the user plays a women's career. A club's men's and women's sides
// are separate clubs of the same name, so the summary's club name does not
// tell; the user's scouting does, as it only covers one of them. Without any
// scouting knowledge the career counts as a men's one.
bool womensCareer(const SaveData &save)
{
    qsizetype women = 0, men = 0;
    for (const SavePlayer &p : save.players) {
        if (!save.knowledgeByPindex.contains(p.pindex) || !p.clubUid)
            continue;
        (p.women ? women : men) += 1;
    }
    return women > men;
}

int findOwnClub(const SaveData &save, bool women)
{
    // The summary names the user's club the way lists do (short name).
    QHash<quint32, int> counts;
    for (const SavePlayer &p : save.players) {
        if (p.clubUid && p.women == women && p.clubShortName == save.clubName)
            ++counts[p.clubUid];
    }
    quint32 best = 0;
    int bestCount = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > bestCount) {
            best = it.key();
            bestCount = it.value();
        }
    }
    return int(best);
}

} // namespace

void setAttributeMasking(bool enabled)
{
    g_masking.store(enabled);
}

bool attributeMasking()
{
    return g_masking.load();
}

QString nationCode(int nationId)
{
    // The save stores no nation names (the game draws them from its database).
    // Ids measured against the exporter's Nation column on two careers
    // (Bayern, Leganés; same ids in both). Unlisted ids stay empty.
    static const QHash<int, QString> codes = {
        {0, "ALG"}, {1, "ANG"}, {2, "BEN"}, {4, "BFA"}, {5, "BDI"}, {6, "CMR"}, {7, "CPV"}, {8, "CTA"},
        {9, "CHA"}, {11, "EGY"}, {12, "EQG"}, {14, "GAB"}, {15, "GAM"}, {16, "GHA"}, {17, "GUI"},
        {18, "GNB"}, {19, "CIV"}, {20, "KEN"}, {22, "LBR"}, {23, "LBY"}, {24, "MAD"}, {26, "MLI"},
        {27, "MTN"}, {28, "MRI"}, {29, "MAR"}, {30, "MOZ"}, {31, "NAM"}, {32, "NIG"}, {33, "NGA"},
        {36, "SEN"}, {38, "SLE"}, {39, "SOM"}, {40, "RSA"}, {41, "SDN"}, {44, "CGO"}, {45, "TOG"},
        {46, "TUN"}, {48, "COD"}, {49, "ZAM"}, {50, "ZIM"}, {51, "AFG"}, {52, "BHR"}, {55, "CHN"},
        {56, "HKG"}, {57, "IND"}, {59, "IRN"}, {60, "IRQ"}, {61, "JPN"}, {62, "JOR"}, {64, "KAZ"},
        {66, "KGZ"}, {68, "LBN"}, {70, "MAS"}, {75, "OMA"}, {77, "QAT"}, {78, "KSA"}, {80, "KOR"},
        {82, "SYR"}, {83, "TPE"}, {84, "TJK"}, {85, "THA"}, {86, "PHI"}, {88, "UAE"}, {89, "UZB"},
        {90, "FRA"}, {97, "CAN"}, {99, "CRC"}, {100, "CUB"}, {102, "SLV"}, {104, "GUA"}, {106, "HAI"},
        {107, "HON"}, {108, "JAM"}, {109, "MEX"}, {111, "ESP"}, {112, "PAN"}, {113, "PUR"},
        {117, "SUR"}, {119, "TRI"}, {120, "USA"}, {126, "ALB"}, {127, "AND"}, {128, "ARM"},
        {129, "AUT"}, {130, "AZE"}, {131, "BEL"}, {132, "BLR"}, {133, "BIH"}, {134, "BUL"},
        {135, "CRO"}, {137, "CZE"}, {138, "DEN"}, {139, "ENG"}, {140, "EST"}, {142, "FIN"},
        {143, "FRA"}, {144, "GEO"}, {145, "GER"}, {146, "GRE"}, {147, "HUN"}, {148, "ISL"},
        {149, "ISR"}, {150, "ITA"}, {151, "LVA"}, {152, "LIE"}, {153, "LTU"}, {154, "LUX"},
        {155, "MKD"}, {158, "NED"}, {159, "NIR"}, {160, "NOR"}, {161, "POL"}, {162, "POR"},
        {163, "IRL"}, {164, "ROU"}, {165, "RUS"}, {166, "SMR"}, {167, "SCO"}, {168, "SVK"},
        {169, "SVN"}, {170, "ESP"}, {171, "SWE"}, {172, "SUI"}, {173, "TUR"}, {174, "UKR"},
        {176, "SRB"}, {177, "AUS"}, {180, "NZL"}, {183, "TAH"}, {187, "ARG"}, {188, "BOL"},
        {189, "BRA"}, {190, "CHI"}, {191, "COL"}, {192, "ECU"}, {193, "PAR"}, {194, "PER"},
        {195, "URU"}, {196, "VEN"}, {197, "PLE"}, {210, "NCL"}, {212, "DOM"}, {216, "GIB"},
        {219, "KOS"}, {225, "GUF"}, {226, "GLP"}, {227, "MTQ"}, {229, "SMN"}, {230, "FRA"},
        {231, "FRA"}, {234, "COM"}, {247, "MNE"},
    };
    return codes.value(nationId);
}

QString footText(int strength)
{
    if (strength >= 18)
        return QStringLiteral("Very Strong");
    if (strength >= 15)
        return QStringLiteral("Strong");
    if (strength >= 12)
        return QStringLiteral("Fairly Strong");
    if (strength >= 9)
        return QStringLiteral("Reasonable");
    if (strength >= 5)
        return QStringLiteral("Weak");
    return QStringLiteral("Very Weak");
}

QString positionText(const std::array<uint8_t, 15> &r)
{
    // Positions the game lists: rating 15 or more. Sides in the game's order R, L, C.
    const auto has = [&](int index) { return r[size_t(index)] >= 15; };
    struct Group {
        QString name;
        QString sides; // empty for GK and DM
    };
    QList<Group> groups;
    const auto sides = [&](int right, int left, int centre) {
        QString s;
        if (right >= 0 && has(right))
            s += QLatin1Char('R');
        if (left >= 0 && has(left))
            s += QLatin1Char('L');
        if (centre >= 0 && has(centre))
            s += QLatin1Char('C');
        return s;
    };
    if (has(PosGK))
        groups.append({QStringLiteral("GK"), {}});
    if (const QString s = sides(PosDR, PosDL, PosDC); !s.isEmpty())
        groups.append({QStringLiteral("D"), s});
    if (const QString s = sides(PosWBR, PosWBL, -1); !s.isEmpty())
        groups.append({QStringLiteral("WB"), s});
    if (has(PosDM))
        groups.append({QStringLiteral("DM"), {}});
    if (const QString s = sides(PosMR, PosML, PosMC); !s.isEmpty())
        groups.append({QStringLiteral("M"), s});
    if (const QString s = sides(PosAMR, PosAML, PosAMC); !s.isEmpty())
        groups.append({QStringLiteral("AM"), s});
    if (has(PosST))
        groups.append({QStringLiteral("ST"), QStringLiteral("C")});
    // Neighbouring groups with the same sides are joined: "D/WB (R)".
    QStringList parts;
    for (int i = 0; i < groups.size();) {
        QString names = groups[i].name;
        int j = i + 1;
        // The striker slot is never joined ("AM (C), ST (C)").
        while (!groups[i].sides.isEmpty() && groups[i].name != QLatin1String("ST") && j < groups.size()
               && groups[j].name != QLatin1String("ST") && groups[j].sides == groups[i].sides) {
            names += QLatin1Char('/') + groups[j].name;
            ++j;
        }
        parts << (groups[i].sides.isEmpty() ? names : QStringLiteral("%1 (%2)").arg(names, groups[i].sides));
        i = j;
    }
    return parts.join(QStringLiteral(", "));
}

std::pair<int, int> drawRange(quint32 uniqueId, int attribute, int value, int width)
{
    if (width <= 0)
        return {value, value};
    // Where the true value sits: anywhere from the low to the high end.
    const int below = std::min(width, int(std::floor(unit(uniqueId, attribute, 2) * (width + 1))));
    int lo = value - below;
    int hi = lo + width;
    if (lo < 1) {
        hi += 1 - lo;
        lo = 1;
    }
    if (hi > 20) {
        lo -= hi - 20;
        hi = 20;
    }
    return {std::max(1, lo), hi};
}

int rangedAttributeCount(int knowledge)
{
    if (knowledge >= kFullKnowledge)
        return 0;
    // Measured: 58-62 % -> 18-20 ranges, 66-68 % -> 17-19, 70 % -> 16, 76 % -> 11.
    const double count = 20.0 - std::max(0, knowledge - 60) * 0.55;
    return std::clamp(int(std::lround(count)), 0, 20);
}

Knowledge knowledgeOf(const SavePlayer &p, const SaveData &save, quint32 ownClubUid)
{
    if (ownClubUid && (p.clubUid == ownClubUid || p.fieldingClubUid == ownClubUid))
        return Knowledge::OwnClub;
    if (p.worldReputation >= kFamousReputation)
        return Knowledge::Famous;
    const int k = save.knowledgeByPindex.value(p.pindex, -1);
    if (k >= kFullKnowledge)
        return Knowledge::Scouted;
    if (k >= kPartialKnowledge)
        return Knowledge::PartlyScouted;
    return k >= 0 ? Knowledge::TooLittle : Knowledge::None;
}

void buildTable(const SaveData &save, const Options &options, ImportTable *out)
{
    out->headers = {QStringLiteral("Unique ID"), QStringLiteral("Player"), QStringLiteral("Age"),
                    QStringLiteral("Club"), QStringLiteral("Nation"), QStringLiteral("Position"),
                    QStringLiteral("Height"), QStringLiteral("Left Foot"), QStringLiteral("Right Foot"),
                    QStringLiteral("Wage"), QStringLiteral("Transfer Value"), QStringLiteral("Playing Time"),
                    QStringLiteral("Rating")};
    for (const VisibleAttr &a : kVisible)
        out->headers << QString::fromLatin1(a.column);
    out->rows.clear();
    out->malformedRows = 0;
    out->knownUids.clear();
    out->naturalPositionsByUid.clear();
    // Only the career's side of the game: a men's career leaves out every
    // women's club and the other way round.
    const bool women = womensCareer(save);
    const quint32 ownClub = quint32(findOwnClub(save, women));

    for (const SavePlayer &p : save.players) {
        if (!p.personFound || p.uniqueId == 0 || p.name.isEmpty())
            continue;
        if (p.women != women)
            continue;
        int ranged = 0;
        int knowledge = 100;
        bool ownSquad = !options.attributeMasking; // without masking: real data for all
        const Knowledge how = knowledgeOf(p, save, ownClub);
        const bool scouted = how == Knowledge::Scouted || how == Knowledge::PartlyScouted;
        if (options.attributeMasking) {
            ownSquad = how == Knowledge::OwnClub;
            switch (how) {
            case Knowledge::OwnClub:
            case Knowledge::Famous:
            case Knowledge::Scouted:
                break;
            case Knowledge::PartlyScouted:
                knowledge = save.knowledgeByPindex.value(p.pindex);
                ranged = rangedAttributeCount(knowledge);
                break;
            case Knowledge::TooLittle:
            case Knowledge::None:
                continue;
            }
        } else if (p.clubUid == 0) {
            continue; // without masking: every player with a club
        }

        // Which attributes show as ranges: the game's order for the player's
        // kind, shuffled a little per player (never by value).
        bool asRange[SaveAttrCount] = {};
        if (ranged > 0) {
            const bool keeper = p.positions[PosGK] >= 15;
            const int *order = keeper ? kGoalkeeperRangeOrder : kOutfieldRangeOrder;
            std::vector<std::pair<double, int>> scored;
            for (int i = 0; i < kVisibleCount; ++i)
                scored.push_back({i + unit(p.uniqueId, order[i], 3) * 10.0, order[i]});
            std::sort(scored.begin(), scored.end());
            for (int i = 0; i < ranged && i < int(scored.size()); ++i)
                asRange[scored[size_t(i)].second] = true;
        }

        QStringList row;
        // Keep natural positions separate from user edits. Only carry the
        // information for players whose positions are fully known.
        if (!options.attributeMasking || how == Knowledge::OwnClub
            || how == Knowledge::Famous || how == Knowledge::Scouted) {
            auto natural = p.positions;
            for (auto &value : natural)
                value = value == 20 ? 20 : 0;
            QStringList positions = parsePositionString(positionText(natural)).values();
            positions.sort();
            out->naturalPositionsByUid.insert(QString::number(p.uniqueId), positions);
        }
        row.reserve(out->headers.size());
        row << QString::number(p.uniqueId) << p.name << (p.age >= 0 ? QString::number(p.age) : QString())
            << p.clubShortName << nationCode(p.nationId) << positionText(p.positions)
            << (p.heightCm > 0 ? QStringLiteral("%1 cm").arg(p.heightCm) : QString())
            << footText(p.leftFoot) << footText(p.rightFoot);
        // Contract and valuation, as the game shows them: the own squad's wage
        // exactly and its valuation as a narrow range; for everyone else known
        // (and without masking) wage and valuation as ranges, wider the less
        // the scouting knows.
        const double valueEuros = double(p.transferValue) * kEurosPerPound;
        const double wageEuros = double(p.weeklyWage) * kEurosPerPound;
        if (ownSquad) {
            row << (p.weeklyWage >= 0 ? wageText(p.weeklyWage) : QString())
                << (p.transferValue <= 0 ? QString()
                    : options.attributeMasking ? moneyRangeText(p.uniqueId, 0, valueEuros, 1.1, 1.35)
                                               : moneyText(valueEuros)); // real data: exact
        } else {
            const bool partly = knowledge < kFullKnowledge;
            row << (p.weeklyWage >= 0 ? moneyRangeText(p.uniqueId, 1, wageEuros, 1.1, partly ? 1.7 : 1.4,
                                                        QStringLiteral(" p/w"))
                                      : QString())
                << (p.transferValue > 0
                        ? moneyRangeText(p.uniqueId, 2, valueEuros, partly ? 1.5 : 1.2, partly ? 3.0 : 1.7)
                        : QString());
        }
        row << playingTimeText(p.squadStatus, p.positions[PosGK] >= 15);
        // Season statistics are public: the average rating of everyone imported.
        row << (p.averageRating > 0 ? QString::number(p.averageRating, 'f', 2) : QString());
        for (const VisibleAttr &a : kVisible) {
            const int value = p.attributes[size_t(a.saveIndex)];
            if (asRange[a.saveIndex]) {
                const auto [lo, hi] =
                    drawRange(p.uniqueId, a.saveIndex, value, rangeWidth(p.uniqueId, a.saveIndex, knowledge));
                row << attrText(lo, hi);
            } else {
                row << QString::number(value);
            }
        }
        out->rows.append(std::move(row));
        if (scouted)
            out->knownUids << QString::number(p.uniqueId);
    }
    out->gameDate = save.gameDate;
}

bool buildTable(const QString &savePath, const Options &options, ImportTable *out, ImportError *error,
                const std::function<void(int, int)> &progress)
{
    SaveData save;
    QString message;
    if (!readSave(savePath, &save, &message, [&](int percent) {
            if (progress)
                progress(percent, 100);
        })) {
        if (error)
            *error = {ImportErrorCode::SaveUnreadable, message, message};
        return false;
    }
    buildTable(save, options, out);
    return true;
}

} // namespace fm::Fm27SaveImport
