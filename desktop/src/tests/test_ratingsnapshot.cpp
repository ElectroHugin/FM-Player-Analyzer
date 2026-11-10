#include <QtTest>
#include <QtConcurrent>
#include "core/RatingSnapshot.h"
#include "core/Definitions.h"
#include "core/TrainingAdvice.h"
#include <cmath>

using namespace fm;
class TestRatingSnapshot : public QObject {
    Q_OBJECT
    Definitions definitions;
    RatingProfile profile;
private slots:
    void initTestCase() {
        QVERIFY(definitions.load(QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json")));
        profile.id = QStringLiteral("test");
        profile.name = QStringLiteral("Test");
        profile.compatibleGames = {QStringLiteral("fm24")};
        profile.attributes.insert(QStringLiteral("Pace"), *harvestgreenCurve({-40, -26.8, 0, 19.4}, 4));
    }
    void compiledMatchesPureEvaluation() {
        QString error;
        auto snapshot = RatingSnapshot::compile(profile, definitions, &error);
        QVERIFY2(snapshot, qPrintable(error));
        for (int low = 1; low <= 20; ++low) {
            for (int high = low; high <= 20; ++high) {
                Player p;
                p.attrLo[idx(Attr::Pace)] = low;
                p.attrHi[idx(Attr::Pace)] = high;
                auto result = snapshot->evaluate(p, QStringLiteral("AF-A"));
                auto reference = evaluateCurves({{profile.attributes.value(QStringLiteral("Pace")), low, high, 1.2}});
                QVERIFY(result.normalized && reference.normalized);
                QVERIFY(std::abs(*result.normalized - *reference.normalized) < 1e-12);
                QVERIFY(std::abs(result.absolute - reference.absolute) < 1e-12);
            }
        }
    }
    void plateauAndCappedRangeTraining() {
        auto snapshot = RatingSnapshot::compile(profile, definitions, nullptr);
        Player p;
        p.attrLo[idx(Attr::Pace)] = 18;
        p.attrHi[idx(Attr::Pace)] = 20;
        QCOMPARE(snapshot->trainingGains(p, QStringLiteral("AF-A")).value(idx(Attr::Pace)), 0.0);
        p.age = 18;
        const auto plateauAdvice = TrainingAdvice::adviseForRole(p, QStringLiteral("AF-A"), *snapshot, {});
        QVERIFY(plateauAdvice.valid);
        QVERIFY(plateauAdvice.focus.empty());
        QVERIFY(TrainingAdvice::adviseFocusesForRole(p, QStringLiteral("AF-A"), *snapshot, {}).focus.empty());
        auto linear = profile;
        linear.attributes[QStringLiteral("Pace")] = RatingCurve::linear(1);
        snapshot = RatingSnapshot::compile(linear, definitions, nullptr);
        // Three equally likely original values map to 19,20,20: retain duplicate mass at cap.
        QVERIFY(std::abs(snapshot->trainingGains(p, QStringLiteral("AF-A")).value(idx(Attr::Pace))
                         - 200.0 / 57) < 1e-12);
        const auto advice = TrainingAdvice::adviseForRole(p, QStringLiteral("AF-A"), *snapshot, {});
        QCOMPARE(advice.focus.size(), size_t(1));
        QVERIFY(std::abs(advice.focus.front().priority - 200.0 / 57) < 1e-12);
    }
    void snapshotIsIndependentAndCacheTracksDefinitions() {
        auto source = profile;
        auto defs = definitions;
        auto before = RatingSnapshot::compile(source, defs, nullptr);
        auto root = defs.root();
        auto roles = root.value(QStringLiteral("role_specific_weights")).toObject();
        auto role = roles.value(QStringLiteral("AF-A")).toObject();
        role.insert(QStringLiteral("preferable"), QJsonArray{});
        roles.insert(QStringLiteral("AF-A"), role);
        root.insert(QStringLiteral("role_specific_weights"), roles);
        defs.setRoot(root);
        auto after = RatingSnapshot::compile(source, defs, nullptr);
        QVERIFY(before && after);
        QVERIFY(before->cacheKey() != after->cacheKey());
        source.name = QStringLiteral("Renamed");
        QCOMPARE(RatingSnapshot::compile(source, definitions, nullptr)->cacheKey(), before->cacheKey());
        source.attributes[QStringLiteral("Pace")].weight = 0;
        Player p;
        p.attrLo[idx(Attr::Pace)] = p.attrHi[idx(Attr::Pace)] = 20;
        QCOMPARE(*before->evaluate(p, QStringLiteral("AF-A")).normalized, 100.0);
        QVERIFY(!RatingSnapshot::compile(source, definitions, nullptr)->evaluate(p, QStringLiteral("AF-A")).normalized);
    }
    void largeBatchConcurrentReaders() {
        auto snapshot = RatingSnapshot::compile(profile, definitions, nullptr);
        std::vector<Player> players(80000);
        for (auto &p : players) p.attrLo[idx(Attr::Pace)] = p.attrHi[idx(Attr::Pace)] = 12;
        auto first = QtConcurrent::run([&] { return snapshot->evaluateRole(players, QStringLiteral("AF-A")); });
        auto second = QtConcurrent::run([&] { return snapshot->evaluateRole(players, QStringLiteral("AF-A")); });
        const auto a = first.result();
        const auto b = second.result();
        QCOMPARE(a.size(), players.size());
        QCOMPARE(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i) QCOMPARE(a[i].normalized, b[i].normalized);
    }
    void rejectsIncompatibleBindings() {
        auto p = profile;
        QString error;
        p.roles[QStringLiteral("fm24/both/nonexistent")][QStringLiteral("Pace")].weight = 1;
        QVERIFY(!RatingSnapshot::compile(p, definitions, &error));
        QVERIFY(error.startsWith(QStringLiteral("unknown-role:")));
        p = profile;
        p.rolePositions[QStringLiteral("fm24/both/AF-A")] = QStringLiteral("fm24/GK");
        QVERIFY(!RatingSnapshot::compile(p, definitions, &error));
        QVERIFY(error.startsWith(QStringLiteral("incompatible-role-position:")));
        p = profile;
        p.compatibleGames = {QStringLiteral("fm27")};
        QVERIFY(!RatingSnapshot::compile(p, definitions, &error));
        QCOMPARE(error, QStringLiteral("incompatible-game"));
    }
};
QTEST_GUILESS_MAIN(TestRatingSnapshot)
#include "test_ratingsnapshot.moc"
