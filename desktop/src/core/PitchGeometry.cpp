#include "PitchGeometry.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace fm {

namespace {

PitchSlot makeSlot(const QString &key, int column, const QStringList &gamePositions,
                   const QStringList &rolePositions = {}, const QString &label = {},
                   bool inEditor = true)
{
    PitchSlot slot;
    slot.key = key;
    slot.label = label.isEmpty() ? key : label;
    slot.column = column;
    slot.displayColumn = column;
    slot.gamePositions = gamePositions;
    slot.rolePositions = rolePositions.isEmpty() ? gamePositions : rolePositions;
    slot.inEditor = inEditor;
    return slot;
}

void buildLookups(PitchGeometry &geometry)
{
    geometry.stratumOrders.insert(geometry.goalkeeperStratum, 0);
    geometry.slotGamePositions.insert(geometry.goalkeeper.key, geometry.goalkeeper.gamePositions);
    for (const PitchRow &row : std::as_const(geometry.rows)) {
        geometry.stratumOrders.insert(row.stratum, row.order);
        for (const PitchSlot &slot : row.pitchSlots) {
            geometry.slotPositions.insert(slot.key, {row.stratum, slot.column});
            geometry.slotGamePositions.insert(slot.key, slot.gamePositions);
        }
    }
}

// FM24 (legacy constants.py / new_tactic.py). As in the game, who may fill a
// slot depends on the slot's position, not on the role: DL/DR need D (L)/D (R)
// (wing-back roles are fine there for such a player), DML/DMR ("WBL"/"WBR" in
// the game) need WB (L)/WB (R) and take wing-back roles only. Decision
// 2026-10-18: a pure wing-back no longer fills DL/DR (legacy allowed it).
PitchGeometry buildFm24()
{
    const QString dl = QStringLiteral("D (L)"), dr = QStringLiteral("D (R)");
    const QString dc = QStringLiteral("D (C)"), dm = QStringLiteral("DM");
    const QString wbl = QStringLiteral("WB (L)"), wbr = QStringLiteral("WB (R)");
    const QString ml = QStringLiteral("M (L)"), mc = QStringLiteral("M (C)"),
                  mr = QStringLiteral("M (R)");
    const QString aml = QStringLiteral("AM (L)"), amc = QStringLiteral("AM (C)"),
                  amr = QStringLiteral("AM (R)");
    const QString st = QStringLiteral("ST (C)");

    PitchGeometry g;
    g.goalkeeperStratum = QStringLiteral("GK");
    g.goalkeeper = makeSlot(QStringLiteral("GK"), 0, {QStringLiteral("GK")});

    PitchRow defense{QStringLiteral("Defense"), QStringLiteral("DEF"), 1, {}};
    defense.pitchSlots = {
        makeSlot(QStringLiteral("DL"), 0, {dl}),
        makeSlot(QStringLiteral("WBL"), 0, {wbl}, {}, {}, /*inEditor*/ false),
        makeSlot(QStringLiteral("DCL"), 1, {dc}),
        makeSlot(QStringLiteral("DC"), 2, {dc}),
        makeSlot(QStringLiteral("DCR"), 3, {dc}),
        makeSlot(QStringLiteral("DR"), 4, {dr}),
        makeSlot(QStringLiteral("WBR"), 4, {wbr}, {}, {}, /*inEditor*/ false),
    };
    PitchRow defensiveMidfield{QStringLiteral("Defensive Midfield"), QStringLiteral("DM"), 2, {}};
    defensiveMidfield.pitchSlots = {
        makeSlot(QStringLiteral("DML"), 0, {wbl}, {}, QStringLiteral("DML/WBL")),
        makeSlot(QStringLiteral("DMCL"), 1, {dm}),
        makeSlot(QStringLiteral("DMC"), 2, {dm}),
        makeSlot(QStringLiteral("DMCR"), 3, {dm}),
        makeSlot(QStringLiteral("DMR"), 4, {wbr}, {}, QStringLiteral("DMR/WBR")),
    };
    PitchRow midfield{QStringLiteral("Midfield"), QStringLiteral("MID"), 3, {}};
    midfield.pitchSlots = {
        makeSlot(QStringLiteral("ML"), 0, {ml}),
        makeSlot(QStringLiteral("MCL"), 1, {mc}),
        makeSlot(QStringLiteral("MC"), 2, {mc}),
        makeSlot(QStringLiteral("MCR"), 3, {mc}),
        makeSlot(QStringLiteral("MR"), 4, {mr}),
    };
    PitchRow attackingMidfield{QStringLiteral("Attacking Midfield"), QStringLiteral("AM"), 4, {}};
    attackingMidfield.pitchSlots = {
        makeSlot(QStringLiteral("AML"), 0, {aml}),
        makeSlot(QStringLiteral("AMCL"), 1, {amc}),
        makeSlot(QStringLiteral("AMC"), 2, {amc}),
        makeSlot(QStringLiteral("AMCR"), 3, {amc}),
        makeSlot(QStringLiteral("AMR"), 4, {amr}),
    };
    // Three striker slots (0-2), drawn in the middle three grid columns; ST and
    // STC share the central slot.
    PitchRow strikers{QStringLiteral("Strikers"), QStringLiteral("ST"), 5, {}};
    strikers.pitchSlots = {
        makeSlot(QStringLiteral("STL"), 0, {st}),
        makeSlot(QStringLiteral("STC"), 1, {st}),
        makeSlot(QStringLiteral("ST"), 1, {st}, {}, {}, /*inEditor*/ false),
        makeSlot(QStringLiteral("STR"), 2, {st}),
    };
    for (PitchSlot &slot : strikers.pitchSlots)
        slot.displayColumn = slot.column + 1;

    g.rows = {defense, defensiveMidfield, midfield, attackingMidfield, strikers};
    buildLookups(g);
    return g;
}

} // namespace

const PitchSlot *PitchGeometry::slot(const QString &key) const
{
    if (key == goalkeeper.key)
        return &goalkeeper;
    for (const PitchRow &r : rows) {
        for (const PitchSlot &s : r.pitchSlots) {
            if (s.key == key)
                return &s;
        }
    }
    return nullptr;
}

const PitchRow *PitchGeometry::row(const QString &stratum) const
{
    for (const PitchRow &r : rows) {
        if (r.stratum == stratum)
            return &r;
    }
    return nullptr;
}

int PitchGeometry::drawingRow(const QString &stratum) const
{
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (rows.at(i).stratum == stratum)
            return static_cast<int>(rows.size() - i);
    }
    return -1;
}

const PitchGeometry &pitchGeometry(const QString &versionId)
{
    // Dispatch on the FM version like attributeMapping(): new releases get
    // another branch; unknown or empty ids fall back to the default version.
    Q_UNUSED(versionId); // only FM24 exists today
    static const PitchGeometry fm24 = buildFm24();
    return fm24;
}

const QHash<QString, SlotPosition> &masterPositionMap(const QString &versionId)
{
    return pitchGeometry(versionId).slotPositions;
}

const QHash<QString, QStringList> &tacticalSlotToGamePositions(const QString &versionId)
{
    return pitchGeometry(versionId).slotGamePositions;
}

const QHash<QString, int> &stratumOrder(const QString &versionId)
{
    return pitchGeometry(versionId).stratumOrders;
}

const QHash<QString, SlotPosition> &masterPositionMap()
{
    return masterPositionMap(QString());
}

const QHash<QString, QStringList> &tacticalSlotToGamePositions()
{
    return tacticalSlotToGamePositions(QString());
}

const QHash<QString, int> &stratumOrder()
{
    return stratumOrder(QString());
}

double drawingRowY(int row)
{
    static const double kRowY[] = {0.0, 0.085, 0.255, 0.42, 0.585, 0.75, 0.915};
    return kRowY[std::clamp(row, 1, 6)];
}

QHash<QString, QPointF> slotAnchors(const QString &versionId,
                                    const QHash<QString, QStringList> &layout,
                                    bool withGoalkeeper)
{
    const PitchGeometry &geometry = pitchGeometry(versionId);
    QHash<QString, QPointF> anchors;
    for (auto it = layout.constBegin(); it != layout.constEnd(); ++it) {
        const int row = geometry.drawingRow(it.key());
        if (row < 0)
            continue;
        QSet<int> occupied;
        for (const QString &slot : it.value()) {
            if (const PitchSlot *pitchSlot = geometry.slot(slot))
                occupied.insert(pitchSlot->displayColumn);
        }
        const bool closeUp = occupied.contains(1) && occupied.contains(3) && !occupied.contains(2);
        for (const QString &slot : it.value()) {
            if (slot == geometry.goalkeeper.key)
                continue;
            const PitchSlot *pitchSlot = geometry.slot(slot);
            if (!pitchSlot || pitchSlot->displayColumn < 0 || pitchSlot->displayColumn > 4)
                continue;
            const int column = pitchSlot->displayColumn;
            const double x = closeUp && (column == 1 || column == 3) ? (column == 1 ? 0.37 : 0.63)
                                                                      : 0.1 + 0.2 * column;
            anchors.insert(slot, QPointF(x, drawingRowY(row)));
        }
    }
    if (withGoalkeeper)
        anchors.insert(geometry.goalkeeper.key, QPointF(0.5, drawingRowY(6)));
    return anchors;
}

QList<QPointF> spreadMarkers(const QList<QPointF> &anchors, const QSizeF &size)
{
    QList<QPointF> placed = anchors;
    const double w = size.width();
    const double h = size.height();
    const double gap = 0.004;
    const auto clampX = [&](double x) { return std::clamp(x, w / 2, 1.0 - w / 2); };
    const auto clampY = [&](double y) { return std::clamp(y, h / 2, 1.0 - h / 2); };
    for (QPointF &p : placed)
        p = QPointF(clampX(p.x()), clampY(p.y()));
    // Pairwise relaxation: each overlapping pair moves apart along the axis
    // that needs the smaller shift, half each, until nothing overlaps.
    for (int round = 0; round < 60; ++round) {
        bool moved = false;
        for (qsizetype i = 0; i < placed.size(); ++i) {
            for (qsizetype j = i + 1; j < placed.size(); ++j) {
                QPointF &a = placed[i];
                QPointF &b = placed[j];
                const double dx = b.x() - a.x();
                const double dy = b.y() - a.y();
                const double overlapX = w + gap - std::abs(dx);
                const double overlapY = h + gap - std::abs(dy);
                if (overlapX <= 1e-9 || overlapY <= 1e-9)
                    continue;
                moved = true;
                // Ties (same spot) go sideways, the earlier marker to the left.
                if (overlapX <= overlapY * (w / h) || (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9)) {
                    const double sign = dx >= 0 ? 1.0 : -1.0;
                    a.setX(clampX(a.x() - sign * overlapX / 2));
                    b.setX(clampX(b.x() + sign * overlapX / 2));
                } else {
                    const double sign = dy >= 0 ? 1.0 : -1.0;
                    a.setY(clampY(a.y() - sign * overlapY / 2));
                    b.setY(clampY(b.y() + sign * overlapY / 2));
                }
            }
        }
        if (!moved)
            break;
    }
    return placed;
}

} // namespace fm
