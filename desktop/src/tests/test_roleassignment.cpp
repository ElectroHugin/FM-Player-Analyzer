#include "core/Database.h"
#include "core/Definitions.h"
#include "core/Player.h"
#include "core/RoleAssignment.h"

#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <vector>

using namespace fm;

// Backlog #37 / P7: additive default-role assignment. Roles are only ever
// added (manual roles survive), and a player who gains a position is topped up.

namespace {

Player makePlayer(const QString &uid, const QString &positions, const QStringList &roles = {})
{
    Player p;
    p.uid = uid;
    p.name = uid;
    p.positionRaw = positions;
    p.assignedRoles = roles;
    return p;
}

QStringList sorted(QStringList list)
{
    std::sort(list.begin(), list.end());
    return list;
}

} // namespace

class TestRoleAssignment : public QObject
{
    Q_OBJECT

private:
    Definitions m_defs;

    QStringList defaultsOf(const QString &position) const
    {
        return sorted(m_defs.positionToRoleMapping().value(position));
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(m_defs.load(QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json")),
                 qPrintable(m_defs.errorString()));
        QVERIFY(!defaultsOf(QStringLiteral("D (C)")).isEmpty());
    }

    void defaultRolesForSinglePosition()
    {
        RoleAssignment::DefaultRoles roles(m_defs);
        QCOMPARE(roles.forPosition(QStringLiteral("D (C)")), defaultsOf(QStringLiteral("D (C)")));
        // Cached lookups return the same list.
        QCOMPARE(roles.forPosition(QStringLiteral("D (C)")), defaultsOf(QStringLiteral("D (C)")));
    }

    void defaultRolesUniteAllPositions()
    {
        RoleAssignment::DefaultRoles roles(m_defs);
        QStringList expected = defaultsOf(QStringLiteral("AM (R)")) + defaultsOf(QStringLiteral("AM (L)"))
                               + defaultsOf(QStringLiteral("ST (C)"));
        expected.removeDuplicates();
        QCOMPARE(roles.forPosition(QStringLiteral("AM (RL), ST (C)")), sorted(expected));
    }

    void unknownPositionHasNoDefaults()
    {
        RoleAssignment::DefaultRoles roles(m_defs);
        QVERIFY(roles.forPosition(QString()).isEmpty());
        QVERIFY(roles.forPosition(QStringLiteral("Unknown")).isEmpty());
    }

    void missingRoleAdditionsOnlyAdds()
    {
        const QString manual = QStringLiteral("AF-A"); // not a D (C) default
        QVERIFY(!defaultsOf(QStringLiteral("D (C)")).contains(manual));

        std::vector<Player> players = {
            makePlayer(QStringLiteral("complete"), QStringLiteral("D (C)"),
                       defaultsOf(QStringLiteral("D (C)"))),
            makePlayer(QStringLiteral("manual"), QStringLiteral("D (C)"), {manual}),
            makePlayer(QStringLiteral("nopos"), QString(), {manual}),
        };
        const auto additions = RoleAssignment::missingRoleAdditions(players, m_defs);
        QCOMPARE(additions.size(), size_t(1));
        QCOMPARE(additions[0].first, 1);
        QCOMPARE(additions[0].second, sorted(defaultsOf(QStringLiteral("D (C)")) << manual));
        // Pure: nothing changed in place.
        QCOMPARE(players[1].assignedRoles, QStringList{manual});
    }

    void gainedPositionIsToppedUp()
    {
        // M (R) -> M/AM (R): the AM (R) roles must be added to the existing ones.
        std::vector<Player> players = {makePlayer(QStringLiteral("w"), QStringLiteral("M/AM (R)"),
                                                  defaultsOf(QStringLiteral("M (R)")))};
        const auto additions = RoleAssignment::missingRoleAdditions(players, m_defs);
        QCOMPARE(additions.size(), size_t(1));
        for (const QString &role : defaultsOf(QStringLiteral("AM (R)")))
            QVERIFY2(additions[0].second.contains(role), qPrintable(role));
        for (const QString &role : defaultsOf(QStringLiteral("M (R)")))
            QVERIFY2(additions[0].second.contains(role), qPrintable(role));
    }

    void autoAssignPersistsAndIsIdempotent()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("roleassignment_test"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));

        std::vector<Player> players = {
            makePlayer(QStringLiteral("a"), QStringLiteral("D (C)")),
            makePlayer(QStringLiteral("b"), QStringLiteral("GK"), defaultsOf(QStringLiteral("GK"))),
        };
        QVERIFY(db.upsertPlayers(players));

        QString error;
        const QStringList changed = RoleAssignment::autoAssignMissingRoles(db, players, m_defs, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(changed, QStringList{QStringLiteral("a")});
        QCOMPARE(players[0].assignedRoles, defaultsOf(QStringLiteral("D (C)")));

        const auto reloaded = db.loadPlayers();
        QCOMPARE(sorted(reloaded[0].assignedRoles), defaultsOf(QStringLiteral("D (C)")));

        QVERIFY(RoleAssignment::autoAssignMissingRoles(db, players, m_defs, &error).isEmpty());
        QVERIFY(error.isEmpty());
    }

    void clearStalePrimaryRole()
    {
        Player p = makePlayer(QStringLiteral("p"), QStringLiteral("DM"), {QStringLiteral("DM-S")});
        p.primaryRole = QStringLiteral("DM-S");
        QVERIFY(!RoleAssignment::clearStalePrimaryRole(p));
        QCOMPARE(p.primaryRole, QStringLiteral("DM-S"));

        p.primaryRole = QStringLiteral("BWM-D");
        QVERIFY(RoleAssignment::clearStalePrimaryRole(p));
        QVERIFY(p.primaryRole.isEmpty());

        QVERIFY(!RoleAssignment::clearStalePrimaryRole(p)); // empty stays empty
    }
};

QTEST_GUILESS_MAIN(TestRoleAssignment)
#include "test_roleassignment.moc"
