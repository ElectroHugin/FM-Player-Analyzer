#pragma once

// Club records of an FM27 save's game database and the teams they field.
// Layout after fmsave (MIT, see THIRD_PARTY_NOTICES.md): the counted club
// table bounds every club; each club stores its team list and a list of
// affiliated teams it fields.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

namespace fm::fm27save {

struct ClubInfo {
    quint32 uid = 0;      // save-local club id
    quint32 uniqueId = 0; // database Unique ID (what graphics packs use)
    QString name;         // long name, e.g. "Manchester City"
    QString shortName;    // the name the game's lists show, e.g. "Man City"
};

struct ClubTeams {
    QList<ClubInfo> clubs;
    QHash<quint32, int> clubByTeam;     // team id -> index in clubs (club that stores the team)
    QHash<quint32, int> fieldingByTeam; // affiliated team id -> index of the club fielding it

    // The club the game names for the team (a reserve side shows as "... II").
    const ClubInfo *clubForTeam(quint32 teamId) const
    {
        const auto it = clubByTeam.constFind(teamId);
        return it == clubByTeam.constEnd() ? nullptr : &clubs[*it];
    }
    // The club that fields the team: the parent for an affiliated team.
    const ClubInfo *fieldingClubForTeam(quint32 teamId) const
    {
        const auto it = fieldingByTeam.constFind(teamId);
        return it == fieldingByTeam.constEnd() ? clubForTeam(teamId) : &clubs[*it];
    }
};

bool readClubTeams(const QByteArray &db, ClubTeams *out, QString *error);

} // namespace fm::fm27save
