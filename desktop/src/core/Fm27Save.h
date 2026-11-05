#pragma once

// Reader for Football Manager 2027 save files (.fm).
//
// The container, name-pool, player-record and person-block layouts follow
// fmsave by Randal S. Olson (https://github.com/rhiever/fmsave, MIT license,
// see THIRD_PARTY_NOTICES.md). The scouting knowledge ("fog_of_war") was
// measured for this app. Nothing here writes to the save.
//
// The reader returns everything it decodes, hidden values included: the
// visibility rules (what the user may see) live in Fm27Visibility, which is
// the only consumer that may turn these records into importable rows.

#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QList>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>

namespace fm::fm27save {

// One named frame of the save's trailer directory.
struct DirectoryEntry {
    QString name;
    QString extension; // ".dat", ".cmt", ".apm", ...
    quint64 frameOffset = 0;
    quint64 compressedSize = 0;
    quint64 decompressedSize = 0;
};

// The container: a 26-byte header, zstd frames and a trailer directory.
class Container {
public:
    bool open(const QString &path, QString *error);
    // Decompressed bytes of one section ("game_db", "fog_of_war", ...).
    bool readSection(const QString &name, QByteArray *out, QString *error) const;
    bool hasSection(const QString &name) const;

    QString saveName() const { return m_saveName; }
    const QList<DirectoryEntry> &entries() const { return m_entries; }

private:
    QString m_path;
    QString m_saveName;
    qint64 m_fileSize = 0;
    QList<DirectoryEntry> m_entries;
};

// Attribute order inside the 54-byte block of a player record (two of the 54
// bytes are the foot strengths, see kLeftFootByte/kRightFootByte).
enum SaveAttr : int {
    Crossing, Dribbling, Finishing, Heading, LongShots, Marking, OffTheBall, Passing,
    PenaltyTaking, Tackling, Vision, Handling, AerialReach, CommandOfArea, Communication,
    Kicking, Throwing, Anticipation, Decisions, OneOnOnes, Positioning, Reflexes, FirstTouch,
    Technique, Flair, Corners, Teamwork, WorkRate, LongThrows, Eccentricity, RushingOut,
    Punching, Acceleration, FreeKickTaking, Strength, Stamina, Pace, JumpingReach, Leadership,
    Dirtiness, Balance, Bravery, Consistency, Aggression, Agility, ImportantMatches,
    InjuryProneness, Versatility, NaturalFitness, Determination, Composure, Concentration,
    SaveAttrCount
};

// Position ratings (1-20) in stored order.
enum SavePosition : int {
    PosGK, PosSW, PosDL, PosDC, PosDR, PosDM, PosML, PosMC, PosMR, PosAML, PosAMC, PosAMR,
    PosST, PosWBL, PosWBR, SavePositionCount
};

enum SavePersonality : int {
    Adaptability, Ambition, Loyalty, Pressure, Professionalism, Sportsmanship, Temperament,
    Controversy, SavePersonalityCount
};

struct SavePlayer {
    quint32 uid = 0;      // save-local id
    quint32 uniqueId = 0; // database "Unique ID" (as the game's screens show it); 0 = not found
    quint32 pindex = 0;   // person index other sections refer to (stored as pindex + 1)
    qint64 recordOffset = 0;

    quint16 homeReputation = 0;
    quint16 currentReputation = 0;
    quint16 worldReputation = 0;
    quint32 teamId = 0xFFFFFFFF;

    std::array<uint8_t, SavePositionCount> positions{};
    std::array<uint8_t, SaveAttrCount> attributes{}; // 1-20 scale
    uint8_t leftFoot = 0;  // 1-20
    uint8_t rightFoot = 0; // 1-20
    int heightCm = 0;

    // Person block (empty name / invalid date when it did not validate).
    bool personFound = false;
    QString name;
    QDate birthDate;
    int age = -1;
    quint16 nationId = 0;
    std::array<uint8_t, SavePersonalityCount> personality{};

    // Club the player's team belongs to (0 / empty when not resolved).
    quint32 clubUid = 0;
    quint32 clubUniqueId = 0;
    QString clubName;
    QString clubShortName;
    // The club fielding the player's team: differs from clubUid for an
    // affiliated side (e.g. a reserve team), which plays for its parent.
    quint32 fieldingClubUid = 0;
};

struct SaveData {
    QString build;  // e.g. "27.0.0+2493426"
    QDate gameDate; // the save's clock
    QString managerName; // the user's manager, from the save summary
    QString clubName;    // the user's club as the summary names it
    QList<SavePlayer> players;
    // fog_of_war: the user's scouting knowledge (0-100) per pindex.
    QHash<quint32, int> knowledgeByPindex;
    bool knowledgeFound = false;
};

using ProgressCallback = std::function<void(int percent)>;

// Reads a save. Fails (with an English message in *error) when the file is
// no FM27 save, is damaged, or its layout does not fit the known builds.
bool readSave(const QString &path, SaveData *out, QString *error,
              const ProgressCallback &progress = {});

// Builds this reader was measured on; others are read but flagged.
bool isKnownBuild(const QString &build);

// --- Exposed for tests -------------------------------------------------------

// fog_of_war: count (u32) at 17, then per entry u8 knowledge + u32 (pindex + 1).
bool parseFogOfWar(const QByteArray &section, QHash<quint32, int> *out);

} // namespace fm::fm27save
