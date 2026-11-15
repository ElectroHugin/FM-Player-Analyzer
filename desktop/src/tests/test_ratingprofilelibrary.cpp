#include <QtTest>
#include "core/RatingProfileLibrary.h"

using namespace fm;
class TestRatingProfileLibrary : public QObject {
    Q_OBJECT
    static RatingProfile profile() {
        RatingProfile p;
        p.id = QStringLiteral("classic-test");
        p.name = QStringLiteral("Example");
        p.compatibleGames = {QStringLiteral("fm27")};
        p.attributes.insert(QStringLiteral("Pace"), RatingCurve::linear(5));
        return p;
    }
private slots:
    void conflictsAndRestore() {
        RatingProfileLibrary library;
        auto p = profile();
        QVERIFY(library.insert(p).isEmpty());
        QCOMPARE(library.insert(p), QStringLiteral("id-conflict"));
        const auto baseline = p.revision();
        const auto expected = RatingProfileLibrary::documentRevision(p);
        p.attributes[QStringLiteral("Pace")].weight = 20;
        QCOMPARE(library.update(p, QStringLiteral("wrong")), QStringLiteral("concurrent-edit"));
        QVERIFY(library.update(p, expected).isEmpty());
        QVERIFY(library.customized(p.id));
        QVERIFY(library.formulaSnapshot(baseline));
        QVERIFY(library.restore(p.id).isEmpty());
        QVERIFY(!library.customized(p.id));
        QVERIFY(library.formulaSnapshot(p.revision()));
        QString copyId;
        QCOMPARE(library.duplicate(p.id, p.name, &copyId), QStringLiteral("name-conflict"));
        QVERIFY(library.duplicate(p.id, QStringLiteral("Copy"), &copyId).isEmpty());
        QVERIFY(copyId != p.id);
        QVERIFY(library.remove(p.id).isEmpty());
        QVERIFY(library.formulaSnapshot(baseline));
    }
    void persistenceAndConcurrentWriters() {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("profiles.json"));
        RatingProfileLibrary library;
        QVERIFY(library.insert(profile()).isEmpty());
        QVERIFY(library.save(path).isEmpty());
        QString error;
        auto first = RatingProfileLibrary::load(path, &error);
        auto second = RatingProfileLibrary::load(path, &error);
        QVERIFY(first && second);
        QCOMPARE(first->toJson(), library.toJson());
        QString copyId;
        QVERIFY(first->duplicate(profile().id, QStringLiteral("Copy"), &copyId).isEmpty());
        QVERIFY(first->save(path).isEmpty());
        QCOMPARE(second->save(path), QStringLiteral("library-conflict"));
        auto final = RatingProfileLibrary::load(path, &error);
        QVERIFY(final);
        QCOMPARE(final->profiles().size(), 2);
        QVERIFY(!RatingProfileLibrary::fromJson("not json", &error));
        QCOMPARE(error, QStringLiteral("invalid-json"));
    }
};
QTEST_GUILESS_MAIN(TestRatingProfileLibrary)
#include "test_ratingprofilelibrary.moc"
