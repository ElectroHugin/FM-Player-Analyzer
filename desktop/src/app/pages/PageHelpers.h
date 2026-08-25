#pragma once

#include "../AppContext.h"
#include "core/TrainingAdvice.h"
#include "core/Utils.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace fm {

// Tactic names with the (club or national) favorite tactics first — the
// legacy ordering used by Best XI, Gap Analysis and the national pages.
inline QStringList favoritesFirstTactics(AppContext &context, bool national)
{
    const QString key1 = national ? QStringLiteral("national_fav_tactic_1")
                                  : QStringLiteral("favorite_tactic_1");
    const QString key2 = national ? QStringLiteral("national_fav_tactic_2")
                                  : QStringLiteral("favorite_tactic_2");
    const QString fav1 = context.database().setting(key1);
    const QString fav2 = context.database().setting(key2);
    QStringList tactics = context.definitions().tacticNames();
    std::sort(tactics.begin(), tactics.end());
    QStringList ordered;
    if (!fav1.isEmpty() && tactics.contains(fav1))
        ordered << fav1;
    if (!fav2.isEmpty() && tactics.contains(fav2) && fav2 != fav1)
        ordered << fav2;
    for (const QString &tactic : tactics) {
        if (!ordered.contains(tactic))
            ordered << tactic;
    }
    return ordered;
}

// Training age windows from config ([Training] section).
inline TrainingAdvice::AgeWindows trainingWindows(AppContext &context)
{
    AppConfig &c = context.config();
    TrainingAdvice::AgeWindows w;
    w.explosivePeak = c.trainingSetting(QStringLiteral("explosive_peak_age"));
    w.explosiveLocked = c.trainingSetting(QStringLiteral("explosive_locked_age"));
    w.strengthPeak = c.trainingSetting(QStringLiteral("strength_peak_age"));
    w.strengthLocked = c.trainingSetting(QStringLiteral("strength_locked_age"));
    w.technicalPeak = c.trainingSetting(QStringLiteral("technical_peak_age"));
    w.technicalLocked = c.trainingSetting(QStringLiteral("technical_locked_age"));
    return w;
}

// Roles a player may train for: intersection of the roles he can play and the
// roles the given tactic actually uses, ordered naturally. "Can play" = his
// assigned roles (which the user curates on the role pages); only when he has
// none do we fall back to the roles his positions allow. Usually a handful.
inline QStringList trainingRoleChoices(AppContext &context, const Player &player,
                                       const QString &tactic)
{
    QSet<QString> playerRoles;
    if (!player.assignedRoles.isEmpty()) {
        for (const QString &role : player.assignedRoles)
            playerRoles.insert(role);
    } else {
        const auto posMap = context.definitions().positionToRoleMapping();
        for (const QString &pos : parsePositionString(player.positionRaw)) {
            for (const QString &role : posMap.value(pos))
                playerRoles.insert(role);
        }
    }
    const auto tacticRoles = context.definitions().tacticRoles().value(tactic);
    QSet<QString> tacticRoleSet;
    for (auto it = tacticRoles.constBegin(); it != tacticRoles.constEnd(); ++it)
        tacticRoleSet.insert(it.value());

    QStringList result;
    for (const QString &role : playerRoles) {
        if (tacticRoleSet.contains(role))
            result << role;
    }
    return context.definitions().sortRolesNaturally(result);
}

// The role with the best current DWRS among the choices (empty if none).
inline QString bestTrainingRole(AppContext &context, const Player &player,
                                const QStringList &choices)
{
    QString best;
    double bestValue = -1.0;
    for (const QString &role : choices) {
        const double v = context.ratings().value(role).value(player.uid, 0.0);
        if (v > bestValue) {
            bestValue = v;
            best = role;
        }
    }
    return best;
}

// Effective training role: the player's saved choice if still valid, else the
// best-DWRS role among the choices.
inline QString effectiveTrainingRole(AppContext &context, const Player &player,
                                     const QStringList &choices)
{
    if (!player.trainingRole.isEmpty() && choices.contains(player.trainingRole))
        return player.trainingRole;
    return bestTrainingRole(context, player, choices);
}

// Uids of the saved national squad (ids live in the DB, uids in the store).
inline QSet<QString> nationalSquadUids(AppContext &context)
{
    QSet<QString> uids;
    for (const Player &player : context.store().players()) {
        if (player.inNationalSquad)
            uids.insert(player.uid);
    }
    return uids;
}

} // namespace fm
