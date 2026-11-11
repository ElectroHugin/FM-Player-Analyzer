#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include "core/AppConfig.h"
#include "core/BuiltinRatingProfiles.h"
#include "core/RatingSnapshot.h"
#include "core/RatingProfileLibrary.h"
using namespace fm;
class TestBuiltinRatingProfiles : public QObject {
    Q_OBJECT
private slots:
    void definitionsAndResearch() {
        QList<Definitions> definitions;
        for (const auto &game : {QStringLiteral("fm24"), QStringLiteral("fm27")}) {
            Definitions d; QVERIFY(d.load(QStringLiteral(DEFINITIONS_DIR) + (game == "fm24" ? "/definitions.json" : "/definitions_fm27.json"), game));
            definitions.append(d);
        }
        QTemporaryDir temp; AppConfig config(temp.filePath("defaults.ini"));
        const auto classic = classicRatingProfile(definitions, config, nullptr); QVERIFY(classic);
        QFile file(QStringLiteral(HARVEST_DATA)); QVERIFY(file.open(QIODevice::ReadOnly));
        auto measurements = QJsonDocument::fromJson(file.readAll()).object(); QString error;
        const auto harvest = harvestgreenRatingProfile(definitions, config, measurements, &error); QVERIFY2(harvest, qPrintable(error));
        QCOMPARE(harvest->attributes.size(), kAttrCount);
        int active = 0;
        for (const auto &curve : harvest->attributes) {
            if (curve.weight > 0) ++active;
            QCOMPARE(curve.contribution(18), curve.contribution(19));
            QCOMPARE(curve.contribution(18), curve.contribution(20));
        }
        QCOMPARE(active, 18);
        QCOMPARE(harvest->attributeEvidence.value("Pace"), QStringLiteral("measured"));
        QCOMPARE(harvest->attributeEvidence.value("Passing"), QStringLiteral("noise-filtered"));
        QCOMPARE(harvest->attributeEvidence.value("Penalty Taking"), QStringLiteral("not-investigated"));
        auto annotated = *harvest; annotated.attributeEvidence["Pace"] = QStringLiteral("legacy");
        QCOMPARE(annotated.revision(), harvest->revision());
        QVERIFY(harvest->attributes.value("Crossing").weight > 4);
        QVERIFY(harvest->attributes.value("Vision").weight > 4);
        QCOMPARE(harvest->attributes.value("Passing").weight, 0.0);
        QCOMPARE(harvest->attributes.value("Penalty Taking").weight, 0.0);
        const auto restored = RatingProfile::fromJson(harvest->toJson(), &error); QVERIFY2(restored, qPrintable(error));
        QCOMPARE(restored->revision(), harvest->revision());
        for (const auto &d : definitions) {
            const auto old = RatingSnapshot::compile(*classic, d, &error); QVERIFY2(old, qPrintable(error));
            if (d.fmVersionId() != QStringLiteral("fm27")) {
                QVERIFY(!RatingSnapshot::compile(*harvest, d, &error));
                QCOMPARE(error, QStringLiteral("incompatible-game"));
                continue;
            }
            const auto current = RatingSnapshot::compile(*harvest, d, &error); QVERIFY2(current, qPrintable(error));
            Player player;
            for (int a = 0; a < kAttrCount; ++a) player.attrLo[a] = player.attrHi[a] = 1 + a % 20;
            for (const auto &role : d.gkRoles()) {
                QCOMPARE(current->evaluate(player, role).normalized, old->evaluate(player, role).normalized);
                QCOMPARE(current->evaluate(player, role).absolute, old->evaluate(player, role).absolute);
            }
        }
        measurements.insert("cutoffPercentagePoints", 6);
        QVERIFY(!harvestgreenRatingProfile(definitions, config, measurements, nullptr));
    }
    void oldLibraryAndBuiltinPreservation() {
        RatingProfile profile; profile.id = "personal"; profile.name = "Personal"; profile.compatibleGames = {QStringLiteral("fm24")};
        profile.attributes.insert("Pace", RatingCurve::linear(19));
        RatingProfileLibrary library; QVERIFY(library.insert(profile).isEmpty());
        auto json = QJsonDocument::fromJson(library.toJson()).object();
        for (const auto &key : {QStringLiteral("profiles"), QStringLiteral("originals"), QStringLiteral("snapshots")}) {
            auto section = json.value(key).toObject();
            for (auto it = section.begin(); it != section.end(); ++it) { auto p = it->toObject(); p.remove("rolePolicies"); p.remove("attributeEvidence"); it.value() = p; }
            json.insert(key, section);
        }
        QVERIFY(RatingProfileLibrary::fromJson(QJsonDocument(json).toJson(), nullptr));
        QVERIFY(library.seedBuiltins({profile}).isEmpty());
        auto edited = profile; edited.attributes["Pace"].weight = 30;
        QVERIFY(library.update(edited, RatingProfileLibrary::documentRevision(profile)).isEmpty());
        QVERIFY(library.seedBuiltins({profile}).isEmpty());
        QCOMPARE(library.profiles().value("personal").attributes.value("Pace").weight, 30.0);
        QVERIFY(library.remove("personal").isEmpty());
        QVERIFY(library.seedBuiltins({profile}).isEmpty()); QVERIFY(library.profiles().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestBuiltinRatingProfiles)
#include "test_builtinratingprofiles.moc"
