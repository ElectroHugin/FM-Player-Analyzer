#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/Definitions.h"
#include "core/DwrsEngine.h"
#include "core/Player.h"
#include "core/PlayerStore.h"
#include "core/RatingsUpdater.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <vector>

using namespace fm;

// Backlog #37 / P7: DWRS recalculation appends a history row only when a rating
// is new or moved by at least 1 %, and the incremental ratings patch matches a
// full rebuild.

namespace {

const QString kCd = QStringLiteral("CD-D");
const QString kBpd = QStringLiteral("BPD-D");

Player playerWithAllAttrs(const QString &uid, int value, const QStringList &roles)
{
    Player p;
    p.uid = uid;
    p.name = uid;
    p.age = 25;
    p.positionRaw = QStringLiteral("D (C)");
    p.assignedRoles = roles;
    for (int i = 0; i < kAttrCount; ++i) {
        p.attrLo[i] = static_cast<uint8_t>(value);
        p.attrHi[i] = static_cast<uint8_t>(value);
    }
    return p;
}

} // namespace

class TestRatingsUpdater : public QObject
{
    Q_OBJECT

private:
    Definitions m_defs;
    QTemporaryDir m_dir;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<DwrsEngine> m_engine;

private slots:
    void initTestCase()
    {
        QVERIFY2(m_defs.load(QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json")),
                 qPrintable(m_defs.errorString()));
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
        m_engine = std::make_unique<DwrsEngine>(m_defs, *m_config);
    }

    void appendGateKeepsHistoryCompact()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("ratingsupdater_gate"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));

        std::vector<Player> players = {
            playerWithAllAttrs(QStringLiteral("a"), 15, {kCd, kBpd}),
            playerWithAllAttrs(QStringLiteral("b"), 10, {kCd, QStringLiteral("NotARole")}),
        };
        QVERIFY(db.upsertPlayers(players));

        // First run: every valid (player, role) pair is new.
        int progressCalls = 0;
        auto r = RatingsUpdater::updateDwrsRatings(db, players, *m_engine, m_defs.validRoles(), {},
                                                   [&](int, int) { ++progressCalls; });
        QVERIFY2(r.success, qPrintable(r.error));
        QCOMPARE(r.computed, 3); // unknown role ignored
        QCOMPARE(r.inserted, 3);
        QCOMPARE(progressCalls, 2); // one batch per role
        QCOMPARE(db.latestDwrsRatings().size(), 3);

        // Unchanged attributes: nothing new to store.
        r = RatingsUpdater::updateDwrsRatings(db, players, *m_engine, m_defs.validRoles());
        QVERIFY(r.success);
        QCOMPARE(r.computed, 3);
        QCOMPARE(r.inserted, 0);

        const double before = db.latestDwrsRatings().value({players[1].id, kCd}).second;

        // A real improvement of "b" appends exactly his row.
        players[1] = playerWithAllAttrs(QStringLiteral("b"), 16, {kCd});
        players[1].id = db.loadPlayers()[1].id;
        r = RatingsUpdater::updateDwrsRatings(db, players, *m_engine, m_defs.validRoles(), {1});
        QVERIFY(r.success);
        QCOMPARE(r.computed, 1); // subset: only row 1
        QCOMPARE(r.inserted, 1);

        QVERIFY(db.latestDwrsRatings().value({players[1].id, kCd}).second > before + 1.0);
    }

    void emptySubsetIsANoOp()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("ratingsupdater_empty"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        const std::vector<Player> none;
        const auto r = RatingsUpdater::updateDwrsRatings(db, none, *m_engine, m_defs.validRoles());
        QVERIFY(r.success);
        QCOMPARE(r.computed, 0);
        QCOMPARE(r.inserted, 0);
    }

    void patchMatchesFullRebuild()
    {
        PlayerStore store;
        Player a = playerWithAllAttrs(QStringLiteral("a"), 12, {kCd, kBpd});
        a.id = 1;
        Player b = playerWithAllAttrs(QStringLiteral("b"), 12, {kCd});
        b.id = 2;
        store.reset({a, b});

        LatestRatings latest;
        latest.insert({1, kCd}, {10.0, 70.0});
        latest.insert({1, kBpd}, {10.0, 65.0});
        latest.insert({2, kCd}, {9.0, 60.0});
        RoleRatings ratings = RatingsUpdater::roleRatingsForAssigned(store, latest);

        // "a" loses BPD-D and is renamed (uid change after a re-import).
        Player changed = a;
        changed.uid = QStringLiteral("a2");
        changed.assignedRoles = {kCd};
        store.reset({changed, b});

        RatingsUpdater::patchRoleRatings(ratings, changed, QStringLiteral("a"), latest);
        const RoleRatings rebuilt = RatingsUpdater::roleRatingsForAssigned(store, latest);

        for (const QString &role : {kCd, kBpd})
            QCOMPARE(ratings.value(role), rebuilt.value(role));
        QCOMPARE(ratings.value(kCd).value(QStringLiteral("a2")), 70.0);
        QVERIFY(!ratings.value(kCd).contains(QStringLiteral("a")));
    }
};

QTEST_GUILESS_MAIN(TestRatingsUpdater)
#include "test_ratingsupdater.moc"
