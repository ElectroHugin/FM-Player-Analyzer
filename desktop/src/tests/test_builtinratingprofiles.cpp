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
            // Goalkeepers use the own FM27 weights with a 20 % role share for
            // the role differences. Acceptance values from the proposal
            // (torhueter-fm27-vorschlag.md, section 8), real FM27 attributes.
            QCOMPARE(harvest->goalkeeperRoleShare, 0.20);
            const auto keeper = [](const QList<QPair<const char *, int>> &values) {
                Player player;
                for (int a = 0; a < kAttrCount; ++a) player.attrLo[a] = player.attrHi[a] = 10;
                for (const auto &[name, value] : values) {
                    const int a = attrIndexByName(QString::fromUtf8(name));
                    player.attrLo[a] = player.attrHi[a] = value;
                }
                return player;
            };
            const struct { Player player; QList<double> expected; } cases[] = {
                {keeper({{"Anticipation", 16}, {"Handling", 17}, {"Reflexes", 19}, {"Positioning", 17}, {"Concentration", 15}, {"Communication", 16}, {"One vs One", 19}, {"Agility", 17}, {"Aerial Reach", 15}, {"Command of Area", 13}, {"Composure", 12}, {"Kicking", 12}, {"Throwing", 16}, {"Bravery", 17}, {"Decisions", 14}, {"Passing", 9}, {"Rushing Out (Tendency)", 12}, {"Acceleration", 8}}),
                 {76.8, 80.2, 81.5, 80.2, 81.4, 78.0}},
                {keeper({{"Anticipation", 17}, {"Handling", 15}, {"Reflexes", 15}, {"Positioning", 14}, {"Concentration", 13}, {"Communication", 13}, {"One vs One", 13}, {"Agility", 11}, {"Aerial Reach", 12}, {"Command of Area", 13}, {"Composure", 17}, {"Kicking", 20}, {"Throwing", 17}, {"Bravery", 17}, {"Decisions", 15}, {"Passing", 16}, {"Rushing Out (Tendency)", 16}, {"Acceleration", 8}}),
                 {73.6, 71.9, 69.6, 69.8, 69.4, 69.3}},
                {keeper({{"Anticipation", 15}, {"Handling", 16}, {"Reflexes", 17}, {"Positioning", 17}, {"Concentration", 14}, {"Communication", 10}, {"One vs One", 14}, {"Agility", 16}, {"Aerial Reach", 16}, {"Command of Area", 9}, {"Composure", 13}, {"Kicking", 15}, {"Throwing", 8}, {"Bravery", 11}, {"Decisions", 13}, {"Passing", 7}, {"Rushing Out (Tendency)", 3}, {"Acceleration", 12}}),
                 {68.2, 70.2, 72.0, 69.1, 72.2, 66.7}}};
            const QStringList roles{"BGK", "GK-IP", "NGK", "GK-OOP", "LHK", "SK"};
            for (const auto &item : cases)
                for (int r = 0; r < roles.size(); ++r) {
                    const auto score = current->evaluate(item.player, roles[r]);
                    QVERIFY2(score.normalized, qPrintable(roles[r]));
                    QVERIFY2(std::abs(*score.normalized - item.expected[r]) < 0.05,
                             qPrintable(QStringLiteral("%1: %2 vs %3").arg(roles[r]).arg(*score.normalized).arg(item.expected[r])));
                }
            // Without the goalkeeper role share every phase falls back to the base part.
            auto baseOnly = *harvest; baseOnly.goalkeeperRoleShare = 0;
            QVERIFY(!baseOnly.toJson().contains(QStringLiteral("goalkeeperRoleShare")));
            const auto plain = RatingSnapshot::compile(baseOnly, d, &error); QVERIFY2(plain, qPrintable(error));
            QCOMPARE(plain->evaluate(cases[0].player, "NGK").normalized, current->evaluate(cases[0].player, "NGK").normalized);
            QVERIFY(plain->evaluate(cases[1].player, "BGK").normalized < current->evaluate(cases[1].player, "BGK").normalized);
            QCOMPARE(restored->goalkeeperRoleShare, 0.20);
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
