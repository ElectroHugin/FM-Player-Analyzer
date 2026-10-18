#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace fm {

// Tactical geometry of the pitch: which slots exist, where they sit and which
// game positions may fill them.

// Tactical geometry: slot key -> (stratum name, column index).
struct SlotPosition {
    QString stratum;
    int column = 0;
};
const QHash<QString, SlotPosition> &masterPositionMap();

// Tactical slot -> game positions eligible to fill it (e.g. "DL" -> ["D (L)", "WB (L)"]).
const QHash<QString, QStringList> &tacticalSlotToGamePositions();

// Vertical ordering of strata on the pitch (GK=0 ... Strikers=5).
const QHash<QString, int> &stratumOrder();

} // namespace fm
