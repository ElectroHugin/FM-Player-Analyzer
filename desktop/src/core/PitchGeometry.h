#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
#include <QSizeF>
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

// --- Normalized drawing positions (shared by every pitch drawing) ---
// x: 0 = left touchline ... 1 = right, y: 0 = opponent goal line ... 1 = own.
// Centre of each slot of one line-up: rows from PitchGeometry::drawingRow,
// five columns, two inside players without a middle one close up (DCL+DCR,
// DMCL+DMCR, STL+STR). layout: stratum -> slots (as Definitions::tacticLayouts);
// the goalkeeper ("GK") is always placed when withGoalkeeper is set.
QHash<QString, QPointF> slotAnchors(const QString &versionId,
                                    const QHash<QString, QStringList> &layout,
                                    bool withGoalkeeper = true);
// Vertical centre of a drawing row (1 = strikers ... 6 = goalkeeper).
double drawingRowY(int row);

// Combined view of a two-phase tactic: a player's marker sits at the
// geometric midpoint of his slot with and his slot without the ball,
// independent of the possession weighting.
inline QPointF combinedAnchor(const QPointF &ip, const QPointF &oop)
{
    return (ip + oop) / 2.0;
}

// Places cards of `size` (normalized) at their anchors and moves overlapping
// ones apart just far enough to be readable, keeping each as close to its
// anchor as possible and inside [0, 1]. Same order as anchors.
QList<QPointF> spreadMarkers(const QList<QPointF> &anchors, const QSizeF &size);

// Default-version (FM24) wrappers of the three tables above.
const QHash<QString, SlotPosition> &masterPositionMap();
const QHash<QString, QStringList> &tacticalSlotToGamePositions();
const QHash<QString, int> &stratumOrder();

} // namespace fm
