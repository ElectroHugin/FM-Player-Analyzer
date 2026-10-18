#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace fm {

// Tactical geometry of the pitch per FM version: which slots exist, where they
// sit, which game positions may fill them and which roles the tactic editor
// offers there. The single source for the Best XI, the tactic editor and every
// pitch drawing.

// Tactical geometry: slot key -> (stratum name, column index).
struct SlotPosition {
    QString stratum;
    int column = 0;
};

struct PitchSlot {
    QString key;               // tactic slot key, e.g. "DML"
    QString label;             // editor label, e.g. "DML/WBL"
    int column = 0;            // column within its row (strikers 0-2, others 0-4)
    int displayColumn = 0;     // column on the 5-column drawing grid (0-4)
    QStringList gamePositions; // game positions eligible to fill it (Best XI etc.)
    QStringList rolePositions; // game positions whose roles the tactic editor offers
    bool inEditor = true;      // false: alias of another slot (WBL, WBR, ST)
};

struct PitchRow {
    QString stratum;    // e.g. "Defensive Midfield"
    QString shortLabel; // e.g. "DM"
    int order = 0;      // vertical order, GK = 0 ... Strikers = 5
    QList<PitchSlot> pitchSlots;
};

struct PitchGeometry {
    QList<PitchRow> rows;     // outfield rows, own goal to attack (Defense first)
    PitchSlot goalkeeper;     // slot "GK"
    QString goalkeeperStratum; // "GK"

    const PitchSlot *slot(const QString &key) const;
    const PitchRow *row(const QString &stratum) const;
    // 1 = top row of a drawing (strikers) ... rows.size() = defense; -1 if unknown.
    int drawingRow(const QString &stratum) const;

    // Lookup tables derived from rows (built once per version).
    QHash<QString, SlotPosition> slotPositions;
    QHash<QString, QStringList> slotGamePositions;
    QHash<QString, int> stratumOrders;
};

// Geometry of an FM version; unknown/empty ids fall back to the default version.
const PitchGeometry &pitchGeometry(const QString &versionId);

// Lookup tables of pitchGeometry(versionId).
const QHash<QString, SlotPosition> &masterPositionMap(const QString &versionId);

// Tactical slot -> game positions eligible to fill it (e.g. "DL" -> ["D (L)", "WB (L)"]).
const QHash<QString, QStringList> &tacticalSlotToGamePositions(const QString &versionId);

// Vertical ordering of strata on the pitch (GK=0 ... Strikers=5).
const QHash<QString, int> &stratumOrder(const QString &versionId);

// Default-version (FM24) wrappers of the three tables above.
const QHash<QString, SlotPosition> &masterPositionMap();
const QHash<QString, QStringList> &tacticalSlotToGamePositions();
const QHash<QString, int> &stratumOrder();

} // namespace fm
