#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/NationalCallup.h"
#include "core/Player.h"
#include "core/SquadBuilder.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <vector>

using namespace fm;

namespace {

QString definitionsPath()
{
    return QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json");
}

// A player carrying one assigned role plus a rating for it, so
// bestDwrsForPlayer has something to rank on.
struct Backing {
    std::vector<Player> players;
    RoleRatings ratings;

    void add(const QString &uid, const QString &position, double rating,
             const QString &role = QStringLiteral("CM-S"), int age = 25)
    {
        Player p;
        p.uid = uid;
        p.name = uid;
        p.age = age;
        p.positionRaw = position;
        p.assignedRoles = {role};
        players.push_back(p);
        ratings[role].insert(uid, rating);
    }
};

std::vector<const Player *> pointers(const std::vector<Player> &players)
{
    std::vector<const Player *> out;
    out.reserve(players.size());
    for (const Player &p : players)
        out.push_back(&p);
    return out;
}

std::vector<const Player *> pick(const std::vector<const Player *> &pool,
                                 const QStringList &uids)
{
    std::vector<const Player *> out;
    for (const Player *p : pool)
        if (uids.contains(p->uid))
            out.push_back(p);
    return out;
}

} // namespace

class TestNationalCallup : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Definitions> m_definitions;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<SquadBuilder> m_builder;
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        m_definitions = std::make_unique<Definitions>();
        QVERIFY2(m_definitions->load(definitionsPath()),
                 qPrintable(m_definitions->errorString()));
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
        m_builder = std::make_unique<SquadBuilder>(*m_definitions, *m_config);
    }

    void isGoalkeeperDetection()
    {
        Player gk;
        gk.positionRaw = QStringLiteral("GK");
        QVERIFY(NationalCallup::isGoalkeeper(gk));
        Player field;
        field.positionRaw = QStringLiteral("D/WB (R), M (R)");
        QVERIFY(!NationalCallup::isGoalkeeper(field));
    }

    // With an empty tactic every player lands in the depth tier, so the
    // recommendation reduces to the pure N/G shaping logic — exactly what this
    // module owns on top of SquadBuilder.
    void exactGoalkeepersAndTotal()
    {
        Backing b;
        for (int i = 0; i < 5; ++i)
            b.add(QStringLiteral("gk%1").arg(i), QStringLiteral("GK"), 90.0 - i,
                  QStringLiteral("GK-D"));
        for (int i = 0; i < 20; ++i)
            b.add(QStringLiteral("of%1").arg(i), QStringLiteral("M (C)"), 80.0 - i);
        const auto pool = pointers(b.players);

        const auto rec = NationalCallup::recommend(*m_builder, pool, {}, {}, b.ratings,
                                                   /*N*/ 23, /*G*/ 3, /*current*/ {});

        QCOMPARE(static_cast<int>(rec.recommended.size()), 23);
        QCOMPARE(rec.goalkeepersRecommended, 3);
        int gkCount = 0;
        for (const Player *p : rec.recommended)
            if (NationalCallup::isGoalkeeper(*p))
                ++gkCount;
        QCOMPARE(gkCount, 3);
        // Goalkeepers are listed first, best rating first.
        QCOMPARE(rec.recommended[0]->uid, QStringLiteral("gk0"));
        QCOMPARE(rec.recommended[1]->uid, QStringLiteral("gk1"));
        QCOMPARE(rec.recommended[2]->uid, QStringLiteral("gk2"));
        // The two weakest keepers did not make the squad.
        QVERIFY(!rec.recommendedUids.contains(QStringLiteral("gk3")));
        QVERIFY(!rec.recommendedUids.contains(QStringLiteral("gk4")));
    }

    void tooFewGoalkeepersFillsWithOutfield()
    {
        Backing b;
        b.add(QStringLiteral("gk0"), QStringLiteral("GK"), 90.0, QStringLiteral("GK-D"));
        b.add(QStringLiteral("gk1"), QStringLiteral("GK"), 88.0, QStringLiteral("GK-D"));
        for (int i = 0; i < 20; ++i)
            b.add(QStringLiteral("of%1").arg(i), QStringLiteral("M (C)"), 80.0 - i);
        const auto pool = pointers(b.players);

        const auto rec = NationalCallup::recommend(*m_builder, pool, {}, {}, b.ratings,
                                                   /*N*/ 10, /*G*/ 3, /*current*/ {});
        // Only 2 keepers exist: take both, fill the rest with outfielders, still N.
        QCOMPARE(rec.goalkeepersRecommended, 2);
        QCOMPARE(static_cast<int>(rec.recommended.size()), 10);
    }

    void invitesAndDropsDiffAgainstCurrent()
    {
        Backing b;
        b.add(QStringLiteral("gk0"), QStringLiteral("GK"), 90.0, QStringLiteral("GK-D"));
        b.add(QStringLiteral("keep"), QStringLiteral("M (C)"), 85.0);   // strong, stays
        b.add(QStringLiteral("newcomer"), QStringLiteral("M (C)"), 84.0); // strong, invited
        b.add(QStringLiteral("weak"), QStringLiteral("M (C)"), 10.0);   // too weak, dropped
        const auto pool = pointers(b.players);

        // Current squad: keep + weak + an injured player who is NOT in the pool.
        Player injured;
        injured.uid = QStringLiteral("injured");
        injured.name = QStringLiteral("injured");
        injured.positionRaw = QStringLiteral("M (C)");
        std::vector<const Player *> current = pick(pool, {QStringLiteral("keep"),
                                                          QStringLiteral("weak")});
        current.push_back(&injured);

        // N=3, G=1 -> gk0 + two best outfielders (keep, newcomer). weak drops.
        const auto rec = NationalCallup::recommend(*m_builder, pool, {}, {}, b.ratings,
                                                   /*N*/ 3, /*G*/ 1, current);

        QVERIFY(rec.recommendedUids.contains(QStringLiteral("keep")));
        QVERIFY(rec.recommendedUids.contains(QStringLiteral("newcomer")));
        QVERIFY(!rec.recommendedUids.contains(QStringLiteral("weak")));

        const auto uidsOf = [](const std::vector<const Player *> &v) {
            QSet<QString> s;
            for (const Player *p : v)
                s.insert(p->uid);
            return s;
        };
        const QSet<QString> invites = uidsOf(rec.invites);
        const QSet<QString> drops = uidsOf(rec.drops);

        // Invited: gk0 and newcomer (recommended, not currently called up).
        QVERIFY(invites.contains(QStringLiteral("newcomer")));
        QVERIFY(invites.contains(QStringLiteral("gk0")));
        QVERIFY(!invites.contains(QStringLiteral("keep"))); // already in squad
        // Dropped: weak (surplus) and injured (absent from pool but still in squad).
        QVERIFY(drops.contains(QStringLiteral("weak")));
        QVERIFY(drops.contains(QStringLiteral("injured")));
        QVERIFY(!drops.contains(QStringLiteral("keep")));
    }
};

QTEST_APPLESS_MAIN(TestNationalCallup)
#include "test_nationalcallup.moc"
