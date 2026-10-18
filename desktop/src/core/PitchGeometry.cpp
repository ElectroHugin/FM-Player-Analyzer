#include "PitchGeometry.h"

namespace fm {

const QHash<QString, SlotPosition> &masterPositionMap()
{
    static const QHash<QString, SlotPosition> map = {
        // Defense (5 slots, indices 0-4)
        {QStringLiteral("DL"), {QStringLiteral("Defense"), 0}},
        {QStringLiteral("WBL"), {QStringLiteral("Defense"), 0}},
        {QStringLiteral("DCL"), {QStringLiteral("Defense"), 1}},
        {QStringLiteral("DC"), {QStringLiteral("Defense"), 2}},
        {QStringLiteral("DCR"), {QStringLiteral("Defense"), 3}},
        {QStringLiteral("DR"), {QStringLiteral("Defense"), 4}},
        {QStringLiteral("WBR"), {QStringLiteral("Defense"), 4}},
        // Defensive Midfield
        {QStringLiteral("DML"), {QStringLiteral("Defensive Midfield"), 0}},
        {QStringLiteral("DMCL"), {QStringLiteral("Defensive Midfield"), 1}},
        {QStringLiteral("DMC"), {QStringLiteral("Defensive Midfield"), 2}},
        {QStringLiteral("DMCR"), {QStringLiteral("Defensive Midfield"), 3}},
        {QStringLiteral("DMR"), {QStringLiteral("Defensive Midfield"), 4}},
        // Midfield
        {QStringLiteral("ML"), {QStringLiteral("Midfield"), 0}},
        {QStringLiteral("MCL"), {QStringLiteral("Midfield"), 1}},
        {QStringLiteral("MC"), {QStringLiteral("Midfield"), 2}},
        {QStringLiteral("MCR"), {QStringLiteral("Midfield"), 3}},
        {QStringLiteral("MR"), {QStringLiteral("Midfield"), 4}},
        // Attacking Midfield
        {QStringLiteral("AML"), {QStringLiteral("Attacking Midfield"), 0}},
        {QStringLiteral("AMCL"), {QStringLiteral("Attacking Midfield"), 1}},
        {QStringLiteral("AMC"), {QStringLiteral("Attacking Midfield"), 2}},
        {QStringLiteral("AMCR"), {QStringLiteral("Attacking Midfield"), 3}},
        {QStringLiteral("AMR"), {QStringLiteral("Attacking Midfield"), 4}},
        // Strikers (3 slots, indices 0-2); ST and STC share the central slot
        {QStringLiteral("STL"), {QStringLiteral("Strikers"), 0}},
        {QStringLiteral("STC"), {QStringLiteral("Strikers"), 1}},
        {QStringLiteral("ST"), {QStringLiteral("Strikers"), 1}},
        {QStringLiteral("STR"), {QStringLiteral("Strikers"), 2}},
    };
    return map;
}

const QHash<QString, QStringList> &tacticalSlotToGamePositions()
{
    static const QHash<QString, QStringList> map = {
        {QStringLiteral("GK"), {QStringLiteral("GK")}},
        // Defense
        {QStringLiteral("DL"), {QStringLiteral("D (L)"), QStringLiteral("WB (L)")}},
        {QStringLiteral("WBL"), {QStringLiteral("WB (L)")}},
        {QStringLiteral("DCL"), {QStringLiteral("D (C)")}},
        {QStringLiteral("DC"), {QStringLiteral("D (C)")}},
        {QStringLiteral("DCR"), {QStringLiteral("D (C)")}},
        {QStringLiteral("DR"), {QStringLiteral("D (R)"), QStringLiteral("WB (R)")}},
        {QStringLiteral("WBR"), {QStringLiteral("WB (R)")}},
        // Defensive Midfield
        {QStringLiteral("DML"), {QStringLiteral("WB (L)")}},
        {QStringLiteral("DMCL"), {QStringLiteral("DM")}},
        {QStringLiteral("DMC"), {QStringLiteral("DM")}},
        {QStringLiteral("DMCR"), {QStringLiteral("DM")}},
        {QStringLiteral("DMR"), {QStringLiteral("WB (R)")}},
        // Midfield
        {QStringLiteral("ML"), {QStringLiteral("M (L)")}},
        {QStringLiteral("MCL"), {QStringLiteral("M (C)")}},
        {QStringLiteral("MC"), {QStringLiteral("M (C)")}},
        {QStringLiteral("MCR"), {QStringLiteral("M (C)")}},
        {QStringLiteral("MR"), {QStringLiteral("M (R)")}},
        // Attacking Midfield
        {QStringLiteral("AML"), {QStringLiteral("AM (L)")}},
        {QStringLiteral("AMCL"), {QStringLiteral("AM (C)")}},
        {QStringLiteral("AMC"), {QStringLiteral("AM (C)")}},
        {QStringLiteral("AMCR"), {QStringLiteral("AM (C)")}},
        {QStringLiteral("AMR"), {QStringLiteral("AM (R)")}},
        // Strikers: all striker slots map to the central striker game position
        {QStringLiteral("STL"), {QStringLiteral("ST (C)")}},
        {QStringLiteral("STC"), {QStringLiteral("ST (C)")}},
        {QStringLiteral("ST"), {QStringLiteral("ST (C)")}},
        {QStringLiteral("STR"), {QStringLiteral("ST (C)")}},
    };
    return map;
}

const QHash<QString, int> &stratumOrder()
{
    static const QHash<QString, int> order = {
        {QStringLiteral("GK"), 0},
        {QStringLiteral("Defense"), 1},
        {QStringLiteral("Defensive Midfield"), 2},
        {QStringLiteral("Midfield"), 3},
        {QStringLiteral("Attacking Midfield"), 4},
        {QStringLiteral("Strikers"), 5},
    };
    return order;
}

} // namespace fm
