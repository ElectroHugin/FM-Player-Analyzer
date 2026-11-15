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
        const auto plateauAdvice = TrainingAdvice::adviseForRole(p, QStringLiteral("AF-A"), *snapshot, TrainingAdvice::AgeWindows{});
        QVERIFY(plateauAdvice.valid);
        QVERIFY(plateauAdvice.focus.empty());
        QVERIFY(TrainingAdvice::adviseFocusesForRole(p, QStringLiteral("AF-A"), *snapshot, TrainingAdvice::AgeWindows{}).focus.empty());
        auto linear = profile;
        linear.attributes[QStringLiteral("Pace")] = RatingCurve::linear(1);
        snapshot = RatingSnapshot::compile(linear, definitions, nullptr);
        // Three equally likely original values map to 19,20,20: retain duplicate mass at cap.
        QVERIFY(std::abs(snapshot->trainingGains(p, QStringLiteral("AF-A")).value(idx(Attr::Pace))
                         - 200.0 / 57) < 1e-12);
        const auto advice = TrainingAdvice::adviseForRole(p, QStringLiteral("AF-A"), *snapshot, TrainingAdvice::AgeWindows{});
        QCOMPARE(advice.focus.size(), size_t(1));
        QVERIFY(std::abs(advice.focus.front().priority - 200.0 / 57) < 1e-12);
    }
    void roleShareKeepsScaleAndRewardsRoleAttributes() {
        auto base = profile;
        base.attributes.insert(QStringLiteral("Acceleration"), RatingCurve::linear(10));
        base.attributes.insert(QStringLiteral("Stamina"), RatingCurve::linear(4));
        const auto off = RatingSnapshot::compile(base, definitions, nullptr);
        auto zero = base; zero.roleShare = 0;
        QCOMPARE(RatingSnapshot::compile(zero, definitions, nullptr)->cacheKey(), off->cacheKey());
        QVERIFY(!zero.toJson().contains(QStringLiteral("roleShare")));
        const auto role = QStringLiteral("AF-A");
        const auto weights = definitions.roleWeights(role);
        QVERIFY(!weights.key.isEmpty());
        double previous = -1;
        for (const double share : {0.0, 0.2, 0.33, 0.5, 0.6}) {
            auto shared = base; shared.roleShare = share;
            QVERIFY(shared.validate().isEmpty());
            const auto restored = RatingProfile::fromJson(shared.toJson(), nullptr);
            QVERIFY(restored && restored->roleShare == share);
            const auto snapshot = RatingSnapshot::compile(shared, definitions, nullptr);
            Player worst, best, specialist;
            for (int a = 0; a < kAttrCount; ++a) {
                worst.attrLo[a] = worst.attrHi[a] = 1;
                best.attrLo[a] = best.attrHi[a] = 20;
                specialist.attrLo[a] = specialist.attrHi[a] = 10;
            }
            for (const auto &name : weights.key)
                if (const int a = attrIndexByName(name); a >= 0) specialist.attrLo[a] = specialist.attrHi[a] = 18;
            QCOMPARE(*snapshot->evaluate(worst, role).normalized, 0.0);
            QVERIFY(std::abs(*snapshot->evaluate(best, role).normalized - 100.0) < 1e-9);
            const double value = *snapshot->evaluate(specialist, role).normalized;
            QVERIFY(value > previous);
            previous = value;
        }
        auto invalid = base; invalid.roleShare = 0.61;
        QVERIFY(!invalid.validate().isEmpty());
    }
    void missingAttributesUseAvailableWeight() {
        auto base = profile;
        base.attributes[QStringLiteral("Pace")] = RatingCurve::linear(8);
        base.attributes.insert(QStringLiteral("Acceleration"), RatingCurve::linear(8));
        base.attributes.insert(QStringLiteral("Natural Fitness"), RatingCurve::linear(2));
        const auto snapshot = RatingSnapshot::compile(base, definitions, nullptr);
        const auto role = QStringLiteral("AF-A");
        Player p;
        p.attrLo[idx(Attr::Pace)] = p.attrHi[idx(Attr::Pace)] = 20;
        p.attrLo[idx(Attr::Acceleration)] = p.attrHi[idx(Attr::Acceleration)] = 20;
        auto score = snapshot->evaluate(p, role);
        QVERIFY(score.normalized);
        QVERIFY(std::abs(*score.normalized - 100.0) < 1e-9);
        QVERIFY(score.coverage < 1.0 && score.coverage > 0.7);
        QCOMPARE(snapshot->missingAttributes(p, role), QList<int>{idx(Attr::NaturalFitness)});
        p.attrLo[idx(Attr::Acceleration)] = p.attrHi[idx(Attr::Acceleration)] = 0;
        score = snapshot->evaluate(p, role);
        QVERIFY(!score.normalized);
        QCOMPARE(score.status, CurveScore::Status::MissingAttribute);
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
