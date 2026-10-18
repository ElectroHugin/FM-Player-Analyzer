#include <QtTest>

#include "core/Constants.h"
#include "core/Definitions.h"
#include "core/PitchGeometry.h"

using namespace fm;

namespace {

// The FM24 tables as they stood before the geometry moved into one place
// (legacy constants.py); the geometry must reproduce them exactly.
const QHash<QString, QPair<QString, int>> &legacySlotPositions()
{
    static const QHash<QString, QPair<QString, int>> map = {
        {"DL", {"Defense", 0}},   {"WBL", {"Defense", 0}},  {"DCL", {"Defense", 1}},
        {"DC", {"Defense", 2}},   {"DCR", {"Defense", 3}},  {"DR", {"Defense", 4}},
        {"WBR", {"Defense", 4}},
        {"DML", {"Defensive Midfield", 0}},  {"DMCL", {"Defensive Midfield", 1}},
        {"DMC", {"Defensive Midfield", 2}},  {"DMCR", {"Defensive Midfield", 3}},
        {"DMR", {"Defensive Midfield", 4}},
        {"ML", {"Midfield", 0}},  {"MCL", {"Midfield", 1}},  {"MC", {"Midfield", 2}},
        {"MCR", {"Midfield", 3}}, {"MR", {"Midfield", 4}},
        {"AML", {"Attacking Midfield", 0}},  {"AMCL", {"Attacking Midfield", 1}},
        {"AMC", {"Attacking Midfield", 2}},  {"AMCR", {"Attacking Midfield", 3}},
        {"AMR", {"Attacking Midfield", 4}},
        {"STL", {"Strikers", 0}}, {"STC", {"Strikers", 1}}, {"ST", {"Strikers", 1}},
        {"STR", {"Strikers", 2}},
    };
    return map;
}

const QHash<QString, QStringList> &legacySlotGamePositions()
{
    static const QHash<QString, QStringList> map = {
        {"GK", {"GK"}},
        {"DL", {"D (L)", "WB (L)"}}, {"WBL", {"WB (L)"}}, {"DCL", {"D (C)"}},
        {"DC", {"D (C)"}},           {"DCR", {"D (C)"}},  {"DR", {"D (R)", "WB (R)"}},
        {"WBR", {"WB (R)"}},
        {"DML", {"WB (L)"}}, {"DMCL", {"DM"}}, {"DMC", {"DM"}}, {"DMCR", {"DM"}},
        {"DMR", {"WB (R)"}},
        {"ML", {"M (L)"}}, {"MCL", {"M (C)"}}, {"MC", {"M (C)"}}, {"MCR", {"M (C)"}},
        {"MR", {"M (R)"}},
        {"AML", {"AM (L)"}}, {"AMCL", {"AM (C)"}}, {"AMC", {"AM (C)"}},
        {"AMCR", {"AM (C)"}}, {"AMR", {"AM (R)"}},
        {"STL", {"ST (C)"}}, {"STC", {"ST (C)"}}, {"ST", {"ST (C)"}}, {"STR", {"ST (C)"}},
    };
    return map;
}

const QHash<QString, int> &legacyStratumOrder()
{
    static const QHash<QString, int> order = {
        {"GK", 0},       {"Defense", 1},            {"Defensive Midfield", 2},
        {"Midfield", 3}, {"Attacking Midfield", 4}, {"Strikers", 5},
    };
    return order;
}

// The natural role sorter as Definitions built it before (GK roles detected by
// the "GK"/"SK" substring, legacy tables above, including the old DL/DR
// wing-back eligibility).
QHash<QString, QPair<int, int>> legacyNaturalSorter(const Definitions &defs)
{
    QHash<QString, QStringList> roleToPositions;
    for (auto it = defs.positionToRoleMapping().constBegin();
         it != defs.positionToRoleMapping().constEnd(); ++it) {
        for (const QString &role : it.value())
            roleToPositions[role].append(it.key());
    }
    QHash<QString, QPair<int, int>> sorter;
    for (const QString &role : defs.validRoles()) {
        if (role.contains(QLatin1String("GK")) || role.contains(QLatin1String("SK"))) {
            sorter.insert(role, {0, 0});
            continue;
        }
        const QStringList positions = roleToPositions.value(role);
        QPair<int, int> best(-1, -1);
        for (auto slotIt = legacySlotPositions().constBegin();
             slotIt != legacySlotPositions().constEnd(); ++slotIt) {
            const QStringList valid = legacySlotGamePositions().value(slotIt.key());
            bool matches = false;
            for (const QString &position : positions)
                matches = matches || valid.contains(position);
            if (!matches)
                continue;
            const QPair<int, int> current(legacyStratumOrder().value(slotIt.value().first, 99),
                                          slotIt.value().second);
            if (current.first > best.first
                || (current.first == best.first && current.second < best.second))
                best = current;
        }
        sorter.insert(role, best.first < 0 ? QPair<int, int>(99, 99) : best);
    }
    return sorter;
}

QStringList definitionsFiles()
{
    return {QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json"),
            QStringLiteral(LEGACY_DIR)
                + QStringLiteral("/../desktop/src/app/resources/definitions.json")};
}

} // namespace

class TestPitchGeometry : public QObject
{
    Q_OBJECT

private slots:
    void fm24TablesUnchanged()
    {
        const auto &positions = masterPositionMap(QStringLiteral("fm24"));
        QCOMPARE(positions.size(), legacySlotPositions().size());
        for (auto it = legacySlotPositions().constBegin(); it != legacySlotPositions().constEnd();
             ++it) {
            QVERIFY2(positions.contains(it.key()), qPrintable(it.key()));
            QCOMPARE(positions.value(it.key()).stratum, it.value().first);
            QCOMPARE(positions.value(it.key()).column, it.value().second);
        }
        // Deliberate change (Edmund, 2026-10-18): DL/DR need D (L)/D (R); a pure
        // wing-back plays only on WBL/WBR (DML/DMR), as in the game.
        QHash<QString, QStringList> expected = legacySlotGamePositions();
        expected.insert(QStringLiteral("DL"), {QStringLiteral("D (L)")});
        expected.insert(QStringLiteral("DR"), {QStringLiteral("D (R)")});
        QCOMPARE(tacticalSlotToGamePositions(QStringLiteral("fm24")), expected);
        QCOMPARE(stratumOrder(QStringLiteral("fm24")), legacyStratumOrder());

        // Unknown ids and the wrappers fall back to FM24.
        QCOMPARE(tacticalSlotToGamePositions(QStringLiteral("fm99")), expected);
        QCOMPARE(tacticalSlotToGamePositions(), expected);
        QCOMPARE(stratumOrder(), legacyStratumOrder());
    }

    void drawingGrid()
    {
        const PitchGeometry &g = pitchGeometry(QStringLiteral("fm24"));
        QCOMPARE(g.drawingRow(QStringLiteral("Strikers")), 1);
        QCOMPARE(g.drawingRow(QStringLiteral("Attacking Midfield")), 2);
        QCOMPARE(g.drawingRow(QStringLiteral("Midfield")), 3);
        QCOMPARE(g.drawingRow(QStringLiteral("Defensive Midfield")), 4);
        QCOMPARE(g.drawingRow(QStringLiteral("Defense")), 5);
        QCOMPARE(g.drawingRow(QStringLiteral("GK")), -1);
        // Strikers sit one column further right on the 5-column grid.
        QCOMPARE(g.slot(QStringLiteral("STL"))->displayColumn, 1);
        QCOMPARE(g.slot(QStringLiteral("STC"))->displayColumn, 2);
        QCOMPARE(g.slot(QStringLiteral("ST"))->displayColumn, 2);
        QCOMPARE(g.slot(QStringLiteral("STR"))->displayColumn, 3);
        QCOMPARE(g.slot(QStringLiteral("DL"))->displayColumn, 0);
        QCOMPARE(g.slot(QStringLiteral("AMR"))->displayColumn, 4);
        QCOMPARE(g.slot(QStringLiteral("GK"))->gamePositions, QStringList{"GK"});
        QVERIFY(!g.slot(QStringLiteral("XX")));
    }

    void editorSlots()
    {
        const PitchGeometry &g = pitchGeometry(QStringLiteral("fm24"));
        QStringList editorKeys;
        for (const PitchRow &row : g.rows) {
            for (const PitchSlot &slot : row.pitchSlots) {
                if (slot.inEditor)
                    editorKeys << slot.key;
            }
        }
        editorKeys.sort();
        QStringList expected{"DL",  "DCL",  "DC",  "DCR",  "DR",  "DML", "DMCL", "DMC",
                             "DMCR", "DMR", "ML",  "MCL",  "MC",  "MCR", "MR",   "AML",
                             "AMCL", "AMC", "AMCR", "AMR", "STL", "STC", "STR"};
        expected.sort();
        QCOMPARE(editorKeys, expected);
        QCOMPARE(g.slot(QStringLiteral("DML"))->label, QStringLiteral("DML/WBL"));
        QCOMPARE(g.slot(QStringLiteral("DMR"))->label, QStringLiteral("DMR/WBR"));

        // DML/DMR: only wing-backs (the editor used to offer DM roles too).
        QCOMPARE(g.slot(QStringLiteral("DML"))->rolePositions, QStringList{"WB (L)"});
        QCOMPARE(g.slot(QStringLiteral("DMR"))->rolePositions, QStringList{"WB (R)"});
        // DL/DR: the editor offers the roles of D (L)/D (R), and only players
        // with that position may fill the slot.
        QCOMPARE(g.slot(QStringLiteral("DL"))->rolePositions, QStringList{"D (L)"});
        QCOMPARE(g.slot(QStringLiteral("DR"))->rolePositions, QStringList{"D (R)"});
        QCOMPARE(g.slot(QStringLiteral("DL"))->gamePositions, QStringList{"D (L)"});
        QCOMPARE(g.slot(QStringLiteral("DR"))->gamePositions, QStringList{"D (R)"});
        QCOMPARE(g.slot(QStringLiteral("DMC"))->rolePositions, QStringList{"DM"});
    }

    void editorRolesForDmlAreWingBacks()
    {
        for (const QString &file : definitionsFiles()) {
            Definitions defs;
            QVERIFY2(defs.load(file), qPrintable(file + QStringLiteral(": ") + defs.errorString()));
            const auto &posMap = defs.positionToRoleMapping();
            const QStringList wingBackRoles = posMap.value(QStringLiteral("WB (L)"));
            QVERIFY(!wingBackRoles.isEmpty());
            QStringList offered;
            for (const QString &position :
                 pitchGeometry(defs.fmVersionId()).slot(QStringLiteral("DML"))->rolePositions)
                offered << posMap.value(position);
            QVERIFY(!offered.isEmpty());
            for (const QString &role : offered)
                QVERIFY2(wingBackRoles.contains(role), qPrintable(role));
            for (const QString &dmRole : posMap.value(QStringLiteral("DM"))) {
                if (!wingBackRoles.contains(dmRole))
                    QVERIFY2(!offered.contains(dmRole), qPrintable(dmRole));
            }
        }
    }

    void naturalSorterUnchanged()
    {
        for (const QString &file : definitionsFiles()) {
            Definitions defs;
            QVERIFY2(defs.load(file), qPrintable(file));
            QCOMPARE(defs.naturalRoleSorter(), legacyNaturalSorter(defs));
        }
    }
};

QTEST_GUILESS_MAIN(TestPitchGeometry)
#include "test_pitchgeometry.moc"
