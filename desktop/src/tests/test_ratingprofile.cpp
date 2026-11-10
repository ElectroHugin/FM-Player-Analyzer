#include <QtTest>
#include <QJsonDocument>
#include "core/RatingProfile.h"

using namespace fm;

class TestRatingProfile : public QObject {
    Q_OBJECT
    static RatingProfile profile()
    {
        RatingProfile p;
        p.id = QStringLiteral("personal-1");
        p.name = QStringLiteral("My profile");
        p.compatibleGames = {QStringLiteral("fm27")};
        p.attributes.insert(QStringLiteral("Pace"), RatingCurve::linear(10));
        p.rolePositions.insert(QStringLiteral("fm27/ip/CF"), QStringLiteral("fm27/ST (C)"));
        RatingOverride position;
        position.weight = 20;
        p.positions[QStringLiteral("fm27/ST (C)")].insert(QStringLiteral("Pace"), position);
        RatingOverride role;
        role.factor = 1.2;
        p.roles[QStringLiteral("fm27/ip/CF")].insert(QStringLiteral("Pace"), role);
        return p;
    }
private slots:
    void roundTrip()
    {
        const auto original = profile();
        QString error;
        const auto json = QJsonDocument::fromJson(QJsonDocument(original.toJson()).toJson()).object();
        const auto imported = RatingProfile::fromJson(json, &error);
        QVERIFY2(imported.has_value(), qPrintable(error));
        QCOMPARE(imported->toJson(), original.toJson());
        QCOMPARE(imported->revision(), original.revision());
    }
    void inheritedFieldsAndReset()
    {
        auto p = profile();
        auto effective = p.resolve(QStringLiteral("Pace"), QStringLiteral("fm27/ip/CF"), true, false);
        QCOMPARE(effective.curve.weight, 20.0);
        QCOMPARE(effective.factor, 1.2); // replacement, not 1.5 * 1.2
        QCOMPARE(effective.shapeOrigin, QStringLiteral("base"));
        QCOMPARE(effective.weightOrigin, QStringLiteral("position:fm27/ST (C)"));
        QCOMPARE(effective.factorOrigin, QStringLiteral("role:fm27/ip/CF"));
        p.roles[QStringLiteral("fm27/ip/CF")][QStringLiteral("Pace")].weight = 0;
        QCOMPARE(p.resolve(QStringLiteral("Pace"), QStringLiteral("fm27/ip/CF"), true, false).curve.weight, 0.0);
        p.roles.clear();
        effective = p.resolve(QStringLiteral("Pace"), QStringLiteral("fm27/ip/CF"), true, false);
        QCOMPARE(effective.curve.weight, 20.0);
        QCOMPARE(effective.factor, 1.5);
        effective = p.resolve(QStringLiteral("Pace"), QStringLiteral("fm27/oop/CF"), false, true);
        QCOMPARE(effective.curve.weight, 10.0); // the other phase does not inherit IP overrides
        QCOMPARE(effective.factor, 1.2);
    }
    void revisionSeparatesMetadata()
    {
        auto p = profile();
        const auto original = p.revision();
        p.name = QStringLiteral("Renamed");
        p.description = QStringLiteral("New description");
        QCOMPARE(p.revision(), original);
        p.attributes[QStringLiteral("Pace")].weight += 1;
        QVERIFY(p.revision() != original);
    }
    void rejectsMalformedInput()
    {
        auto original = profile().toJson();
        QString error;
        auto json = original;
        json.insert(QStringLiteral("version"), 2);
        QVERIFY(!RatingProfile::fromJson(json, &error));
        QCOMPARE(error, QStringLiteral("unsupported-format"));
        json = original;
        json.insert(QStringLiteral("execute"), QStringLiteral("ignored instructions must not run"));
        QVERIFY(!RatingProfile::fromJson(json, &error));
        json = original;
        json.insert(QStringLiteral("keyFactor"), QStringLiteral("1.5"));
        QVERIFY(!RatingProfile::fromJson(json, &error));
        json = original;
        json.insert(QStringLiteral("preferableFactor"), -1);
        QVERIFY(!RatingProfile::fromJson(json, &error));
        auto p = profile();
        p.roles[QStringLiteral("fm27/ip/CF")][QStringLiteral("Unknown")].weight = 1;
        QCOMPARE(p.validate(), QStringLiteral("unknown-override-attribute"));
        p = profile();
        p.roles[QStringLiteral("unscoped-role")][QStringLiteral("Pace")].weight = 1;
        QCOMPARE(p.validate(), QStringLiteral("invalid-scope"));
    }
};
QTEST_GUILESS_MAIN(TestRatingProfile)
#include "test_ratingprofile.moc"
