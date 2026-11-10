#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/DwrsEngine.h"
#include "core/ClassicRatingSnapshot.h"
#include <cmath>

using namespace fm;

// Frozen references must be generated only while DwrsEngine.cpp is identical
// to v2.2.3 (9490f54). The opt-in capture refuses to overwrite a fixture.
class TestClassicReference : public QObject {
    Q_OBJECT
private slots:
    void snapshotIsolationAndValidation() {
        Definitions definitions;
        QVERIFY(definitions.load(QStringLiteral(DEFINITIONS_DIR) + QStringLiteral("/definitions.json")));
        QTemporaryDir temporary; AppConfig config(temporary.filePath("settings.ini"));
        DwrsEngine engine(definitions, config);
        const auto frozen = ClassicRatingSnapshot::capture(engine); QVERIFY(frozen);
        Player player;
        for (int a = 0; a < kAttrCount; ++a) player.attrLo[a] = player.attrHi[a] = 1 + a % 20;
        const auto before = frozen->evaluate(player, "AF-A"); QVERIFY(before.normalized);
        QVERIFY(config.setRoleMultiplier("key", 3.0)); engine.reloadConfig();
        const auto changed = ClassicRatingSnapshot::capture(engine); QVERIFY(changed);
        QVERIFY(changed->revision() != frozen->revision());
        QCOMPARE(frozen->evaluate(player, "AF-A").normalized, before.normalized);
        QCOMPARE(frozen->evaluate(player, "AF-A").absolute, before.absolute);
        auto json = frozen->toJson(); json.insert("version", 1.5);
        QVERIFY(!ClassicRatingSnapshot::fromJson(json, nullptr));
        json = frozen->toJson(); auto roles = json.value("roles").toObject();
        auto plan = roles.value("AF-A").toObject(); auto counts = plan.value("counts").toArray();
        counts[0] = counts[0].toInt() + 1; plan.insert("counts", counts); roles.insert("AF-A", plan); json.insert("roles", roles);
        QVERIFY(!ClassicRatingSnapshot::fromJson(json, nullptr));
        json = frozen->toJson(); roles = json.value("roles").toObject();
        for (auto it = roles.begin(); it != roles.end(); ++it) {
            auto empty = it->toObject(); auto weights = empty.value("weights").toArray();
            for (int i = 0; i < weights.size(); ++i) weights[i] = 0;
            empty.insert("weights", weights); it.value() = empty;
        }
        json.insert("roles", roles);
        const auto empty = ClassicRatingSnapshot::fromJson(json, nullptr); QVERIFY(empty);
        QCOMPARE(empty->evaluate(player, "AF-A").status, CurveScore::Status::NoVariation);
        QVERIFY(!empty->evaluate(player, "AF-A").normalized);
    }
    void everyRoleBothGames()
    {
        const QString fixturePath = QStringLiteral(CLASSIC_FIXTURE);
        const bool capture = qEnvironmentVariableIsSet("FM_CAPTURE_CLASSIC_223");
        QFile fixture(fixturePath);
        QJsonObject expected;
        if (capture) {
            QVERIFY2(!fixture.exists(), "Refusing to overwrite the frozen Classic reference");
        } else {
            QVERIFY(fixture.open(QIODevice::ReadOnly));
            expected = QJsonDocument::fromJson(fixture.readAll()).object();
            QCOMPARE(expected.value(QStringLiteral("sourceCommit")).toString(),
                     QStringLiteral("9490f54f63b57a8b617d1eeb1acd5826613b6399"));
        }
        QJsonArray results;
        int referenceIndex = 0;
        const auto expectedRows = expected.value(QStringLiteral("ratings")).toArray();
        for (const auto &version : {QStringLiteral("fm24"), QStringLiteral("fm27")}) {
            Definitions definitions;
            QVERIFY(definitions.load(QStringLiteral(DEFINITIONS_DIR)
                                      + (version == QStringLiteral("fm24") ? QStringLiteral("/definitions.json")
                                                                             : QStringLiteral("/definitions_fm27.json")), version));
            for (bool custom : {false, true}) {
                QTemporaryDir temporary;
                AppConfig config(temporary.filePath(QStringLiteral("settings.ini")));
                if (custom) {
                    QVERIFY(config.setRoleMultiplier(QStringLiteral("key"), 1.7));
                    QVERIFY(config.setRoleMultiplier(QStringLiteral("preferable"), 1.1));
                    for (auto it = weightDefaults(version).begin(); it != weightDefaults(version).end(); ++it)
                        QVERIFY(config.setWeight(version, it.key(), it.value() * 0.7 + 0.3));
                    for (auto it = gkWeightDefaults(version).begin(); it != gkWeightDefaults(version).end(); ++it)
                        QVERIFY(config.setGkWeight(version, it.key(), it.value() * 0.8 + 0.2));
                }
                DwrsEngine engine(definitions, config);
                const auto snapshot = ClassicRatingSnapshot::capture(engine); QVERIFY(snapshot);
                const auto restored = ClassicRatingSnapshot::fromJson(snapshot->toJson(), nullptr); QVERIFY(restored);
                QCOMPARE(restored->revision(), snapshot->revision());
                for (int sample = 0; sample < 16; ++sample) {
                    Player player;
                    constexpr int levels[] = {1, 6, 12, 18, 19, 20};
                    for (int a = 0; a < kAttrCount; ++a) {
                        int low = sample < 6 ? levels[sample] : 1 + (a * 7 + sample * 3) % 20;
                        int high = low;
                        if (sample >= 8) high = std::min(20, low + (a + sample) % 6);
                        if (sample >= 12 && a % 4 == 0) low = high = 0;
                        if (sample == 15) low = high = 0;
                        player.attrLo[a] = static_cast<uint8_t>(low);
                        player.attrHi[a] = static_cast<uint8_t>(high);
                    }
                    for (const auto &role : definitions.validRoles()) {
                        const auto rating = engine.calculate(player, role);
                        const auto frozen = restored->evaluate(player, role); QVERIFY(frozen.normalized);
                        QCOMPARE(frozen.absolute, rating.first); QCOMPARE(*frozen.normalized, rating.second);
                        QJsonObject row{{QStringLiteral("game"), version}, {QStringLiteral("custom"), custom},
                                        {QStringLiteral("sample"), sample}, {QStringLiteral("role"), role},
                                        {QStringLiteral("absolute"), rating.first}, {QStringLiteral("normalized"), rating.second}};
                        if (!capture) {
                            QVERIFY(referenceIndex < expectedRows.size());
                            const auto reference = expectedRows[referenceIndex].toObject();
                            QCOMPARE(row.value(QStringLiteral("game")), reference.value(QStringLiteral("game")));
                            QCOMPARE(row.value(QStringLiteral("custom")), reference.value(QStringLiteral("custom")));
                            QCOMPARE(row.value(QStringLiteral("sample")), reference.value(QStringLiteral("sample")));
                            QCOMPARE(row.value(QStringLiteral("role")), reference.value(QStringLiteral("role")));
                            const double wanted = reference.value(QStringLiteral("absolute")).toDouble();
                            QVERIFY(std::abs(rating.first - wanted) <= 1e-12 * std::max(1.0, std::abs(wanted)));
                            QCOMPARE(rating.second, reference.value(QStringLiteral("normalized")).toDouble());
                        }
                        results.append(row);
                        ++referenceIndex;
                    }
                }
            }
        }
        if (capture) {
            QSaveFile file(fixturePath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = QJsonDocument(QJsonObject{
                {QStringLiteral("sourceCommit"), QStringLiteral("9490f54f63b57a8b617d1eeb1acd5826613b6399")},
                {QStringLiteral("ratings"), results}}).toJson();
            QCOMPARE(file.write(bytes), bytes.size());
            QVERIFY(file.commit());
        } else {
            QCOMPARE(referenceIndex, expectedRows.size());
        }
    }
};
QTEST_GUILESS_MAIN(TestClassicReference)
#include "test_classicreference.moc"
