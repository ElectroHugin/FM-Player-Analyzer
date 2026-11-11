#include <QtTest>
#include <QSqlQuery>
#include "core/AppConfig.h"
#include "core/BuiltinRatingProfiles.h"
#include "core/ClassicRatingSnapshot.h"
#include "core/Database.h"
#include "core/DwrsEngine.h"
#include "core/ProfileStore.h"
#include "core/RatingSelectionStore.h"
using namespace fm;
class TestRatingSelection : public QObject {
    Q_OBJECT
private slots:
    void preserveCustomWeightsAndOneTimeDecision() {
        QTemporaryDir temp; Database db(Database::uniqueConnectionName("selection")); QVERIFY(db.open(temp.filePath("save.db")));
        Definitions definitions; QVERIFY(definitions.load(QStringLiteral(FM27_DEFINITIONS), "fm27"));
        AppConfig config(temp.filePath("settings.ini")); QVERIFY(config.setRoleMultiplier("key", 2.1));
        DwrsEngine engine(definitions, config); auto profile = classicRatingProfile({definitions}, config, nullptr); QVERIFY(profile);
        const auto legacy = ClassicRatingSnapshot::capture(engine); QVERIFY(legacy);
        RatingSelectionStore store(db); QString error;
        const auto initial = store.initialize(*profile, definitions, true, legacy, &error); QVERIFY2(initial, qPrintable(error));
        QCOMPARE(initial->migrationDecision, QStringLiteral("pending")); QCOMPARE(initial->profile.keyFactor, 2.1);
        profile->keyFactor = 1.5;
        const auto repeat = store.initialize(*profile, definitions, true, legacy, &error); QVERIFY2(repeat, qPrintable(error));
        QCOMPARE(repeat->generation, initial->generation); QCOMPARE(repeat->profile.keyFactor, 2.1);
        const auto kept = store.keepPrevious(initial->generation, &error); QVERIFY2(kept, qPrintable(error));
        QCOMPARE(kept->migrationDecision, QStringLiteral("kept")); QVERIFY(kept->switches.isEmpty());
        QVERIFY(!store.keepPrevious(kept->generation, &error)); QCOMPARE(error, QStringLiteral("migration-already-decided"));
        QCOMPARE(store.load(nullptr)->legacyBasis, legacy->toJson());
    }
    void activationConflictsRenameAndRollback() {
        QTemporaryDir temp; Database db(Database::uniqueConnectionName("selection")); QVERIFY(db.open(temp.filePath("save.db")));
        Definitions definitions; QVERIFY(definitions.load(QStringLiteral(FM27_DEFINITIONS), "fm27"));
        AppConfig config(temp.filePath("settings.ini")); auto profile = classicRatingProfile({definitions}, config, nullptr); QVERIFY(profile);
        RatingSelectionStore store(db); QString error;
        const auto initial = store.initialize(*profile, definitions, false, {}, &error); QVERIFY2(initial, qPrintable(error));
        QCOMPARE(initial->migrationDecision, QStringLiteral("not-needed"));
        auto changed = *profile; changed.attributes["Pace"].weight *= 2;
        const auto selected = store.activate(changed, definitions, initial->generation, "2027-02-01", &error); QVERIFY2(selected, qPrintable(error));
        QCOMPARE(selected->switches.size(), 1);
        QVERIFY(!store.activate(*profile, definitions, initial->generation, {}, &error)); QCOMPARE(error, QStringLiteral("selection-conflict"));
        changed.name = "Renamed";
        const auto renamed = store.activate(changed, definitions, selected->generation, "2027-02-01", &error); QVERIFY2(renamed, qPrintable(error));
        QCOMPARE(renamed->switches.size(), 1); QCOMPARE(renamed->computationKey, selected->computationKey);
        ProfileStore documents(db); QVERIFY(documents.hasDocument("dwrs_rating_basis_" + initial->computationKey));
        QSqlQuery query(db.handle());
        QVERIFY(query.exec("CREATE TRIGGER reject_rating_choice BEFORE UPDATE ON profile_documents WHEN NEW.name='dwrs_rating_selection' BEGIN SELECT RAISE(ABORT, 'test failure'); END"));
        changed.attributes["Pace"].weight *= 2;
        QVERIFY(!store.activate(changed, definitions, renamed->generation, {}, &error));
        QCOMPARE(store.load(nullptr)->generation, renamed->generation);
        QVERIFY(query.exec("SELECT COUNT(*) FROM profile_documents WHERE name LIKE 'dwrs_rating_basis_%'")); QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 2);
    }
    void corruptStateIsNotOverwritten() {
        QTemporaryDir temp; Database db(Database::uniqueConnectionName("selection")); QVERIFY(db.open(temp.filePath("save.db")));
        ProfileStore documents(db); QVERIFY(documents.ensureTables());
        QVERIFY(documents.writeDocument(RatingSelectionStore::documentName(), "broken", false));
        Definitions definitions; QVERIFY(definitions.load(QStringLiteral(FM27_DEFINITIONS), "fm27"));
        AppConfig config(temp.filePath("settings.ini")); const auto profile = classicRatingProfile({definitions}, config, nullptr); QVERIFY(profile);
        RatingSelectionStore store(db); QString error;
        QVERIFY(!store.initialize(*profile, definitions, false, {}, &error)); QCOMPARE(error, QStringLiteral("invalid-rating-selection"));
        QByteArray bytes; QVERIFY(documents.readDocument(RatingSelectionStore::documentName(), bytes)); QCOMPARE(bytes, QByteArray("broken"));
    }
};
QTEST_GUILESS_MAIN(TestRatingSelection)
#include "test_ratingselection.moc"
