#include <QtTest>

#include "core/AppConfig.h"
#include "core/Constants.h"
#include "core/Definitions.h"
#include "core/DwrsEngine.h"
#include "core/TalentEngine.h"

using namespace fm;

class TestDwrs : public QObject
{
    Q_OBJECT

    Definitions defs;
    QTemporaryDir tempDir;
    std::unique_ptr<AppConfig> config;
    std::unique_ptr<DwrsEngine> engine;

    static Player playerWithAllAttrs(int value)
    {
        Player p;
        p.uid = QStringLiteral("t");
        for (int i = 0; i < kAttrCount; ++i) {
            p.attrLo[i] = static_cast<uint8_t>(value);
            p.attrHi[i] = static_cast<uint8_t>(value);
        }
        return p;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(defs.load(QStringLiteral(LEGACY_DIR)
                          + QStringLiteral("/config/definitions.json")));
        config = std::make_unique<AppConfig>(tempDir.filePath(QStringLiteral("config.ini")));
        engine = std::make_unique<DwrsEngine>(defs, *config);
    }

    void perfectPlayerIsHundredPercent()
    {
        const Player best = playerWithAllAttrs(20);
        for (const QString &role : defs.validRoles()) {
            const auto [abs, norm] = engine->calculate(best, role);
            QVERIFY2(qFuzzyCompare(norm + 1.0, 101.0),
                     qPrintable(QStringLiteral("%1: %2").arg(role).arg(norm)));
            QVERIFY(abs > 0.0);
        }
    }

    void worstPlayerIsZeroPercent()
    {
        const Player worst = playerWithAllAttrs(1);
        for (const QString &role : defs.validRoles()) {
            const auto [abs, norm] = engine->calculate(worst, role);
            QCOMPARE(norm, 0.0);
        }
    }

    // FM24 never imported Communication/Eccentricity: although its sweeper
    // keeper lists them, they must not move the rating.
    void fm24IgnoresFm27GoalkeeperAttributes()
    {
        Player low = playerWithAllAttrs(12);
        Player high = low;
        low.attrLo[idx(Attr::Communication)] = low.attrHi[idx(Attr::Communication)] = 1;
        high.attrLo[idx(Attr::Communication)] = high.attrHi[idx(Attr::Communication)] = 20;
        high.attrLo[idx(Attr::Eccentricity)] = high.attrHi[idx(Attr::Eccentricity)] = 20;
        const QString role = QStringLiteral("SK-A");
        QVERIFY(defs.roleWeights(role).key.contains(QStringLiteral("Communication"))
                || defs.roleWeights(role).preferable.contains(QStringLiteral("Communication")));
        QCOMPARE(engine->calculate(low, role).second, engine->calculate(high, role).second);
    }

    void ratingIsMonotonicInAttributes()
    {
        const QString role = QStringLiteral("CD-D");
        double previous = -1.0;
        for (int v = 1; v <= 20; ++v) {
            const auto [abs, norm] = engine->calculate(playerWithAllAttrs(v), role);
            QVERIFY(abs > previous);
            previous = abs;
        }
    }

    void gkRoleIgnoresOutfieldAttributes()
    {
        // Changing a pure outfield attribute (Finishing) must not affect a GK
        // role: it is neither a goalkeeping tier nor on the role's lists.
        Player p = playerWithAllAttrs(10);
        const QString gkRole = QStringLiteral("GK-D");
        const auto [absBefore, normBefore] = engine->calculate(p, gkRole);
        const int finishing = attrIndexByName(QStringLiteral("Finishing"));
        p.attrLo[finishing] = 20;
        p.attrHi[finishing] = 20;
        const auto [absAfter, normAfter] = engine->calculate(p, gkRole);
        QCOMPARE(absAfter, absBefore);
        // But it must affect an outfield role that rates Finishing.
        const auto [outBefore, n1] = engine->calculate(playerWithAllAttrs(10),
                                                       QStringLiteral("AF-A"));
        const auto [outAfter, n2] = engine->calculate(p, QStringLiteral("AF-A"));
        QVERIFY(outAfter > outBefore);
    }

    void gkRoleRatesAllGoalkeepingTiers()
    {
        // Scheme 2: every goalkeeping attribute the app imports takes part,
        // not only the legacy six (Positioning used to be ignored).
        const auto &tiers = gkStatCategories(defaultFmVersionId());
        QCOMPARE(tiers.size(), 13);
        QCOMPARE(tiers.value(QStringLiteral("Positioning")), QStringLiteral("Top Importance"));
        for (auto it = tiers.constBegin(); it != tiers.constEnd(); ++it)
            QVERIFY2(attrIndexByName(it.key()) >= 0, qPrintable(it.key()));

        for (const QString &attr : tiers.keys()) {
            Player p = playerWithAllAttrs(10);
            const auto [before, n1] = engine->calculate(p, QStringLiteral("GK-D"));
            const int idx = attrIndexByName(attr);
            p.attrLo[idx] = 20;
            p.attrHi[idx] = 20;
            const auto [after, n2] = engine->calculate(p, QStringLiteral("GK-D"));
            QVERIFY2(after > before, qPrintable(attr));
        }
    }

    void gkRoleListsCountOutsideTiers()
    {
        // A role-listed outfield attribute (SK-D prefers First Touch) counts
        // through the "Preferable" category — before scheme 2 it was ignored.
        Player p = playerWithAllAttrs(10);
        const QString role = QStringLiteral("SK-D");
        QVERIFY(defs.roleWeights(role).preferable.contains(QStringLiteral("First Touch")));
        const auto [before, n1] = engine->calculate(p, role);
        const int firstTouch = attrIndexByName(QStringLiteral("First Touch"));
        p.attrLo[firstTouch] = 20;
        p.attrHi[firstTouch] = 20;
        const auto [after, n2] = engine->calculate(p, role);
        QVERIFY(after > before);
        // GK-D does not list First Touch, so it stays unaffected.
        const auto [gkBefore, n3] = engine->calculate(playerWithAllAttrs(10), QStringLiteral("GK-D"));
        const auto [gkAfter, n4] = engine->calculate(p, QStringLiteral("GK-D"));
        QCOMPARE(gkAfter, gkBefore);
    }

    void outfieldRoleListsOutsideTiersStayIgnored()
    {
        // Outfield weight tables have no Key/Preferable category: a key
        // attribute outside the outfield tiers (WCB-S keys Heading) keeps not
        // counting, exactly as in the legacy formula.
        const QString role = QStringLiteral("WCB-S");
        QVERIFY(defs.roleWeights(role).key.contains(QStringLiteral("Heading")));
        QVERIFY(!globalStatCategories(defaultFmVersionId()).contains(QStringLiteral("Heading")));
        Player p = playerWithAllAttrs(10);
        const auto [before, n1] = engine->calculate(p, role);
        const int heading = attrIndexByName(QStringLiteral("Heading"));
        p.attrLo[heading] = 20;
        p.attrHi[heading] = 20;
        const auto [after, n2] = engine->calculate(p, role);
        QCOMPARE(after, before);
    }

    void keyAttributesRaiseRating()
    {
        // A key attribute of the role must move the rating more than a
        // non-key attribute of the same importance category.
        const QString role = QStringLiteral("AF-A");
        const RoleWeights rw = defs.roleWeights(role);
        QVERIFY(!rw.key.isEmpty());

        // Find a key attribute and a non-key attribute in the same category.
        const auto &categories = globalStatCategories(defaultFmVersionId());
        QString keyAttr, plainAttr;
        for (const QString &candidate : rw.key) {
            if (!categories.contains(candidate))
                continue;
            const QString category = categories.value(candidate);
            for (auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
                if (it.value() == category && !rw.key.contains(it.key())
                    && !rw.preferable.contains(it.key())) {
                    keyAttr = candidate;
                    plainAttr = it.key();
                    break;
                }
            }
            if (!keyAttr.isEmpty())
                break;
        }
        if (keyAttr.isEmpty())
            QSKIP("No suitable key/plain attribute pair in this role");

        Player base = playerWithAllAttrs(10);
        const auto [absBase, n0] = engine->calculate(base, role);

        Player keyBoost = base;
        const int ki = attrIndexByName(keyAttr);
        keyBoost.attrLo[ki] = keyBoost.attrHi[ki] = 15;
        const auto [absKey, n1] = engine->calculate(keyBoost, role);

        Player plainBoost = base;
        const int pi = attrIndexByName(plainAttr);
        plainBoost.attrLo[pi] = plainBoost.attrHi[pi] = 15;
        const auto [absPlain, n2] = engine->calculate(plainBoost, role);

        QVERIFY(absKey > absBase);
        QVERIFY(absKey - absBase > absPlain - absBase);
    }

    void rangeValuesUseMean()
    {
        const QString role = QStringLiteral("CD-D");
        Player exact = playerWithAllAttrs(10);
        Player ranged = playerWithAllAttrs(10);
        const int pace = attrIndexByName(QStringLiteral("Pace"));
        exact.attrLo[pace] = exact.attrHi[pace] = 14; // 14
        ranged.attrLo[pace] = 12;
        ranged.attrHi[pace] = 16; // mean 14
        const auto [absExact, n1] = engine->calculate(exact, role);
        const auto [absRanged, n2] = engine->calculate(ranged, role);
        QCOMPARE(absRanged, absExact);
    }

    void talentScore()
    {
        // Direct formula check: 60 + 2*(20-17) + (15+14-20)/4 + 3 = 71.25
        const double talent = TalentEngine::calculateTalentScore(
            defs, 60.0, 17.0, 15.0, 14.0, QStringLiteral("Model Citizen"), 20.0);
        QCOMPARE(talent, 71.25);
        // Bad personality: -5.
        const double bad = TalentEngine::calculateTalentScore(
            defs, 60.0, 17.0, 15.0, 14.0, QStringLiteral("Slack"), 20.0);
        QCOMPARE(bad, 63.25);
        // Masked ranges count as 0 (legacy parity).
        Player p;
        p.age = 17;
        p.personality = QStringLiteral("Balanced");
        const int det = idx(Attr::Determination);
        p.attrLo[det] = 12;
        p.attrHi[det] = 15; // masked -> 0
        const double withRange = TalentEngine::talentForPlayer(defs, p, 50.0, 20.0);
        QCOMPARE(withRange, 50.0 + 2.0 * 3.0 + (0.0 + 0.0 - 20.0) / 4.0);
    }

    void batchMatchesScalar()
    {
        std::vector<Player> players;
        for (int v = 5; v <= 15; ++v) {
            Player p = playerWithAllAttrs(v);
            p.uid = QString::number(v);
            p.assignedRoles = {QStringLiteral("CD-D"), QStringLiteral("AF-A")};
            players.push_back(p);
        }
        const auto batch = engine->calculateAllAssigned(
            players, {QStringLiteral("CD-D"), QStringLiteral("AF-A")});
        for (auto it = batch.roleRows.constBegin(); it != batch.roleRows.constEnd(); ++it) {
            const auto &result = batch.roleResults[it.key()];
            for (size_t j = 0; j < it.value().size(); ++j) {
                const auto [abs, norm] = engine->calculate(players[it.value()[j]], it.key());
                QCOMPARE(result.absolute[j], abs);
                QCOMPARE(result.normalized[j], norm);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestDwrs)
#include "test_dwrs.moc"
