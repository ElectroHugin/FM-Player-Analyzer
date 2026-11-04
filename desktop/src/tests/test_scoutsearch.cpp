#include "core/Definitions.h"
#include "core/Player.h"
#include "core/ScoutSearch.h"
#include "core/Utils.h"

#include <QJsonObject>
#include <QtTest>

using namespace fm;

// Scouting search with several requirements (2.1.0): every criterion must be
// met; a bridge asks for a place with ball AND one without.

namespace {

Player makePlayer(const QString &uid, const QString &positions)
{
    Player p;
    p.uid = uid;
    p.name = uid;
    p.positionRaw = positions;
    return p;
}

Definitions phaseDefinitions()
{
    QJsonObject phases{{QStringLiteral("IF"), QStringLiteral("ip")},
                       {QStringLiteral("PFB"), QStringLiteral("oop")}};
    QJsonObject roles{{QStringLiteral("IF"), QStringLiteral("IF")},
                      {QStringLiteral("PFB"), QStringLiteral("PFB")},
                      {QStringLiteral("W"), QStringLiteral("W")}};
    Definitions defs;
    defs.setRoot(QJsonObject{
        {QStringLiteral("player_roles"), QJsonObject{{QStringLiteral("Attack"), roles}}},
        {QStringLiteral("role_phases"), phases},
    });
    return defs;
}

} // namespace

class TestScoutSearch : public QObject
{
    Q_OBJECT

private slots:
    void bridgeNeedsBothPlacesAndRoles()
    {
        const ScoutCriterion bridge = ScoutSearch::bridge(QString(), QStringLiteral("AMR"),
                                                          QStringLiteral("IF"),
                                                          QStringLiteral("DR"),
                                                          QStringLiteral("PFB"));
        QCOMPARE(bridge.places.size(), 2);
        QVERIFY(bridge.places[0].positions.contains(QStringLiteral("AM (R)")));
        QVERIFY(bridge.places[1].positions.contains(QStringLiteral("D (R)")));

        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("all"), 80}, {QStringLiteral("winger"), 85},
                                         {QStringLiteral("weak"), 15}};
        ratings[QStringLiteral("PFB")] = {{QStringLiteral("all"), 60}, {QStringLiteral("weak"), 70},
                                          {QStringLiteral("back"), 75}};
        const Player all = makePlayer(QStringLiteral("all"), QStringLiteral("D (R), AM (R)"));
        const Player winger = makePlayer(QStringLiteral("winger"), QStringLiteral("AM (R)"));
        const Player back = makePlayer(QStringLiteral("back"), QStringLiteral("D (R)"));
        const Player weak = makePlayer(QStringLiteral("weak"), QStringLiteral("D/AM (R)"));
        const QList<ScoutCriterion> criteria{bridge};
        QVERIFY(ScoutSearch::needsPositions(criteria));
        const auto meets = [&](const Player &p) {
            return ScoutSearch::meetsAll(p, parsePositionString(p.positionRaw), criteria, ratings,
                                         20, 100);
        };
        QVERIFY(meets(all));
        QVERIFY(!meets(winger)); // no D (R), no PFB rating
        QVERIFY(!meets(back));   // no AM (R)
        QVERIFY(!meets(weak));   // IF below the DWRS minimum
        QVERIFY(ScoutSearch::meetsAll(weak, parsePositionString(weak.positionRaw), criteria,
                                      ratings, 10, 100));
    }

    void criteriaAreAnded()
    {
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("a"), 70}, {QStringLiteral("b"), 70}};
        ratings[QStringLiteral("W")] = {{QStringLiteral("a"), 60}};
        const QList<ScoutCriterion> criteria{
            ScoutSearch::criterion(ScoutSearch::positionPlace(QString(), QStringLiteral("IF"))),
            ScoutSearch::criterion(ScoutSearch::positionPlace(QString(), QStringLiteral("W"))),
            ScoutSearch::criterion(ScoutSearch::positionPlace(QStringLiteral("ST (C)"), QString()))};
        QCOMPARE(ScoutSearch::requiredRoles(criteria),
                 QStringList({QStringLiteral("IF"), QStringLiteral("W")}));
        const Player a = makePlayer(QStringLiteral("a"), QStringLiteral("AM (R), ST"));
        const Player aNoStriker = makePlayer(QStringLiteral("a"), QStringLiteral("AM (R)"));
        const Player b = makePlayer(QStringLiteral("b"), QStringLiteral("ST (C)"));
        const auto meets = [&](const Player &p) {
            return ScoutSearch::meetsAll(p, parsePositionString(p.positionRaw), criteria, ratings,
                                         0, 100);
        };
        QVERIFY(meets(a));
        QVERIFY(!meets(aNoStriker));
        QVERIFY(!meets(b)); // no W rating
        // Without criteria everyone passes.
        QVERIFY(ScoutSearch::meetsAll(b, {}, {}, ratings, 0, 100));
    }

    void combinedValueWeighsThePhases()
    {
        const Definitions defs = phaseDefinitions();
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("p"), 90}};
        ratings[QStringLiteral("PFB")] = {{QStringLiteral("p"), 50}};
        ratings[QStringLiteral("W")] = {{QStringLiteral("p"), 70}};
        const Player p = makePlayer(QStringLiteral("p"), QStringLiteral("AM (R)"));
        const QList<ScoutCriterion> bridge{ScoutSearch::bridge(
            QString(), QStringLiteral("AMR"), QStringLiteral("IF"), QStringLiteral("DR"),
            QStringLiteral("PFB"))};
        QCOMPARE(ScoutSearch::combinedValue(p, bridge, ratings, defs, 0.5), 70.0);
        QCOMPARE(ScoutSearch::combinedValue(p, bridge, ratings, defs, 0.75), 80.0);
        // Share 100 % with only a role without ball: falls back to the plain mean.
        const QList<ScoutCriterion> oopOnly{ScoutSearch::criterion(
            ScoutSearch::positionPlace(QString(), QStringLiteral("PFB")))};
        QCOMPARE(ScoutSearch::combinedValue(p, oopOnly, ratings, defs, 1.0), 50.0);
        // A role of both phases counts fully.
        QList<ScoutCriterion> mixed = bridge;
        mixed.append(ScoutSearch::criterion(ScoutSearch::positionPlace(QString(), QStringLiteral("W"))));
        QCOMPARE(ScoutSearch::combinedValue(p, mixed, ratings, defs, 0.5), 70.0);
        // Unrated or no role at all: -1.
        const Player other = makePlayer(QStringLiteral("x"), QString());
        QCOMPARE(ScoutSearch::combinedValue(other, bridge, ratings, defs, 0.5), -1.0);
        const QList<ScoutCriterion> positionOnly{ScoutSearch::criterion(
            ScoutSearch::positionPlace(QStringLiteral("AM (R)"), QString()))};
        QCOMPARE(ScoutSearch::combinedValue(p, positionOnly, ratings, defs, 0.5), -1.0);
    }
};

QTEST_GUILESS_MAIN(TestScoutSearch)
#include "test_scoutsearch.moc"
