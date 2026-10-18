#include "PitchGeometry.h"

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

// FM24 (legacy constants.py / new_tactic.py). Wing-back game positions: DL/DR
// may be filled by a wing-back, DML/DMR ("WBL"/"WBR" in the game) only by one;
// the tactic editor therefore offers just wing-back roles on DML/DMR and just
// full-back roles on DL/DR.
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
        makeSlot(QStringLiteral("DL"), 0, {dl, wbl}, {dl}),
        makeSlot(QStringLiteral("WBL"), 0, {wbl}, {}, {}, /*inEditor*/ false),
        makeSlot(QStringLiteral("DCL"), 1, {dc}),
        makeSlot(QStringLiteral("DC"), 2, {dc}),
        makeSlot(QStringLiteral("DCR"), 3, {dc}),
        makeSlot(QStringLiteral("DR"), 4, {dr, wbr}, {dr}),
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

} // namespace fm
