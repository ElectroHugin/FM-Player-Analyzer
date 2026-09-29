#pragma once

#include "Attributes.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>

namespace fm {

// Squad-registration status the user maintains by hand (FM exports none of it).
// Stored in the player_registration table, not part of the FM-import upsert.
struct PlayerRegistration {
    // Manual override of the automatic, age-based U21 decision.
    enum class U21 : int8_t { Auto = -1, No = 0, Yes = 1 };

    bool homeGrown = false;   // trained in the club's association (e.g. England/Wales)
    bool clubTrained = false; // trained at the club itself; implies homeGrown
    U21 u21 = U21::Auto;
    bool leagueListed = false; // on the saved league squad list
    bool uefaListed = false;   // on the saved UEFA list A

    bool isDefault() const { return *this == PlayerRegistration{}; }
    bool operator==(const PlayerRegistration &) const = default;
};

// One player, fully typed. Attribute values are 1-20; FM masks unscoutend
// attributes as ranges like "12-15", stored losslessly as lo/hi (lo == hi for
// exact values, 0 = missing). The DWRS engine uses (lo + hi) / 2.0, matching
// the legacy range-mean behavior.
struct Player {
    int id = 0;               // rowid in the new DB (0 = not yet persisted)
    QString uid;              // FM UID; newgens carry the "r-" prefix

    QString name;
    int age = 0;
    QString club;
    QString nationality;        // 3-letter code as exported by FM
    QString secondNationality;
    QString positionRaw;        // e.g. "AM (RL), ST (C)" — canonical source
    QString personality;
    QString mediaHandling;
    QString agreedPlayingTime;
    QString wageRaw;
    QString transferValueRaw;
    double transferValue = 0.0; // parsed; "Not for Sale" -> kUnbuyableValue
    double averageRating = 0.0; // 0.0 = none (FM exports "-" when unrated)
    QString heightRaw;          // e.g. "191 cm" as exported
    int heightCm = 0;           // parsed; 0 = unknown
    QString leftFoot;           // FM exports text: "Very Strong", "Weak", ...
    QString rightFoot;
    QString preferredFoot;
    QString preferredSide;      // "", "Left", "Right"
    QString primaryRole;

    std::array<uint8_t, kAttrCount> attrLo{};
    std::array<uint8_t, kAttrCount> attrHi{};

    QStringList assignedRoles;
    QStringList naturalPositions;
    // App-managed role the training advisor optimizes for (empty = auto-pick the
    // player's best-DWRS role among those his positions and the tactic allow).
    // Stored in the training_roles table, not part of the FM-import upsert.
    QString trainingRole;
    PlayerRegistration registration;
    // Value of the per-database upload counter at the last import that
    // contained this player. 0 = never stamped (legacy row / pre-tracking).
    // Freshness is derived as (current counter - lastSeenUpdate); see Freshness.
    int lastSeenUpdate = 0;
    bool inNationalSquad = false;
    bool onShortlist = false;
    bool transferStatus = false; // listed for transfer
    bool loanStatus = false;     // listed for loan
    QString newClub;             // planned destination (Transfers page)

    // Mean attribute value used by the DWRS engine; 0.0 if missing.
    double attrMean(int attrIndex) const
    {
        return (attrLo[attrIndex] + attrHi[attrIndex]) / 2.0;
    }
    bool hasAttr(int attrIndex) const { return attrLo[attrIndex] != 0; }

    // Parsed set of individual game positions (from positionRaw).
    QSet<QString> parsedPositions() const;

    // The one goalkeeper test used everywhere (squad builder, talent age cap,
    // call-up, edit page). FM writes the keeper position as "GK" and no other
    // position code contains those letters, so a substring test is exact —
    // and cheap enough for per-player loops over 35k rows.
    bool isGoalkeeper() const { return positionRaw.contains(QLatin1String("GK")); }
};

} // namespace fm
