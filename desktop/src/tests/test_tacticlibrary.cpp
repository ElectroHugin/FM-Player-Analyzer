// Tactics per save: edit, rename, delete, survive shipped updates, exchange
// as files, keep favorites and bridges consistent.

#include "core/Database.h"
#include "core/Definitions.h"
#include "core/TacticLibrary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace fm;

namespace {

const QString kControlPress = QString::fromUtf8("Control Press \xc2\xb7 4-2-3-1");
const QString kCounterSurge = QString::fromUtf8("Counter Surge \xc2\xb7 3-4-2-1");
const QString kOldName = QStringLiteral("4-2-3-1 - Bavarian Blitz");

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject tactics(const QJsonObject &root)
{
    return root.value(QLatin1String("tactic_roles")).toObject();
}

// A user copy of the shipped data from before the current revision.
QJsonObject olderCopy(const QJsonObject &shipped)
{
    QJsonObject user = shipped;
    user.insert(QStringLiteral("data_revision"),
                shipped.value(QLatin1String("data_revision")).toInt() - 1);
    return user;
}

} // namespace

class TestTacticLibrary : public QObject
{
    Q_OBJECT

private:
    QJsonObject m_shipped;
    QJsonObject m_shipped24;

    Definitions definitionsOf(const QJsonObject &root, const QString &version = QStringLiteral("fm27"))
    {
        Definitions defs;
        defs.loadFromJson(QJsonDocument(root).toJson(), version);
        return defs;
    }

private slots:
    void initTestCase()
    {
        m_shipped = readJson(QStringLiteral(DEFINITIONS_FM27));
        QVERIFY(tactics(m_shipped).contains(kControlPress));
        QVERIFY(tactics(m_shipped).contains(kCounterSurge));
        m_shipped24 = readJson(QStringLiteral(DEFINITIONS_FM24));
        QVERIFY(!tactics(m_shipped24).isEmpty());
    }

    void editRenameDelete()
    {
        QJsonObject root = m_shipped;
        const QJsonObject surge = tactics(root).value(kCounterSurge).toObject();
        QVERIFY(TacticLibrary::renameTactic(root, kCounterSurge, QStringLiteral("Mein Konter")));
        QVERIFY(!tactics(root).contains(kCounterSurge));
        QCOMPARE(tactics(root).value(QStringLiteral("Mein Konter")).toObject(), surge);
        QVERIFY(root.value(QLatin1String("tactic_layouts")).toObject().contains(
            QStringLiteral("Mein Konter")));
        // Name taken / missing.
        QVERIFY(!TacticLibrary::renameTactic(root, kControlPress, QStringLiteral("Mein Konter")));
        QVERIFY(!TacticLibrary::renameTactic(root, QStringLiteral("gibt es nicht"),
                                             QStringLiteral("x")));
        QVERIFY(TacticLibrary::removeTactic(root, kControlPress));
        QVERIFY(!TacticLibrary::removeTactic(root, kControlPress));
        const QJsonArray deleted = root.value(QLatin1String("deleted_tactics")).toArray();
        QVERIFY(deleted.contains(kControlPress));
        QVERIFY(deleted.contains(kCounterSurge));
        // The edited definitions load and know only the renamed tactic.
        const Definitions defs = definitionsOf(root);
        QCOMPARE(defs.tacticNames(), QStringList{QStringLiteral("Mein Konter")});
        QVERIFY(defs.isTwoPhaseTactic(QStringLiteral("Mein Konter")));
    }

    void updateKeepsEditsAndDeletions()
    {
        // The user edited Control Press, deleted Counter Surge and made a
        // role and a tactic with it. A newer shipped revision arrives.
        QJsonObject user = olderCopy(m_shipped);
        QJsonObject edited = tactics(user).value(kControlPress).toObject();
        QJsonObject oop = edited.value(QLatin1String("oop")).toObject();
        oop.insert(QStringLiteral("DMC"), QStringLiteral("PDM"));
        edited.insert(QStringLiteral("oop"), oop);
        TacticLibrary::putTactic(user, QStringLiteral("fm27"), kControlPress, edited);
        TacticLibrary::removeTactic(user, kCounterSurge);

        QJsonObject roles = user.value(QLatin1String("player_roles")).toObject();
        QJsonObject midfield = roles.value(QLatin1String("Defensive Midfield")).toObject();
        midfield.insert(QStringLiteral("MYDM"), QStringLiteral("Mein Sechser"));
        roles.insert(QStringLiteral("Defensive Midfield"), midfield);
        user.insert(QStringLiteral("player_roles"), roles);
        QJsonObject phases = user.value(QLatin1String("role_phases")).toObject();
        phases.insert(QStringLiteral("MYDM"), QStringLiteral("oop"));
        user.insert(QStringLiteral("role_phases"), phases);
        user.insert(QStringLiteral("custom_roles"), QJsonArray{QStringLiteral("MYDM")});
        QJsonObject own = edited;
        oop.insert(QStringLiteral("DMC"), QStringLiteral("MYDM"));
        own.insert(QStringLiteral("oop"), oop);
        TacticLibrary::putTactic(user, QStringLiteral("fm27"), QStringLiteral("Eigene"), own);

        QVERIFY(Definitions::applyShippedUpdate(m_shipped, user));
        const QJsonObject after = tactics(user);
        QCOMPARE(after.value(kControlPress).toObject(), edited);
        QVERIFY(!after.contains(kCounterSurge));
        QVERIFY(after.contains(QStringLiteral("Eigene")));
        QVERIFY(user.value(QLatin1String("custom_roles")).toArray().contains(QStringLiteral("MYDM")));
        const Definitions defs = definitionsOf(user);
        QCOMPARE(defs.rolePhase(QStringLiteral("MYDM")), QStringLiteral("oop"));
        // The lists survive, so the next update behaves the same.
        user.insert(QStringLiteral("data_revision"),
                    m_shipped.value(QLatin1String("data_revision")).toInt() - 1);
        QVERIFY(Definitions::applyShippedUpdate(m_shipped, user));
        QVERIFY(!tactics(user).contains(kCounterSurge));
        QCOMPARE(tactics(user).value(kControlPress).toObject(), edited);
    }

    void renamedTemplateFollows()
    {
        const QJsonObject blitz = tactics(m_shipped).value(kControlPress).toObject();
        const auto userWithBlitz = [&] {
            QJsonObject user = olderCopy(m_shipped);
            QJsonObject t = tactics(user);
            t.remove(kControlPress);
            t.remove(kCounterSurge);
            t.insert(kOldName, blitz);
            user.insert(QStringLiteral("tactic_roles"), t);
            return user;
        };
        // Untouched: becomes Control Press, favorites/bridges may follow.
        {
            QJsonObject user = userWithBlitz();
            QList<QPair<QString, QString>> renamed;
            QVERIFY(Definitions::applyShippedUpdate(m_shipped, user, &renamed));
            QVERIFY(!tactics(user).contains(kOldName));
            QVERIFY(tactics(user).contains(kControlPress));
            QCOMPARE(renamed.size(), 1);
            QCOMPARE(renamed.first().first, kOldName);
            QCOMPARE(renamed.first().second, kControlPress);
        }
        // Edited by the user: stays his, under the old name.
        {
            QJsonObject user = userWithBlitz();
            user.insert(QStringLiteral("edited_tactics"), QJsonArray{kOldName});
            QList<QPair<QString, QString>> renamed;
            QVERIFY(Definitions::applyShippedUpdate(m_shipped, user, &renamed));
            QVERIFY(tactics(user).contains(kOldName));
            QVERIFY(renamed.isEmpty());
        }
        // Deleted by the user: the new name stays away as well.
        {
            QJsonObject user = userWithBlitz();
            TacticLibrary::removeTactic(user, kOldName);
            QVERIFY(Definitions::applyShippedUpdate(m_shipped, user));
            QVERIFY(!tactics(user).contains(kOldName));
            QVERIFY(!tactics(user).contains(kControlPress));
            QVERIFY(tactics(user).contains(kCounterSurge));
        }
    }

    void exportImportRoundTrip()
    {
        // Profile A with an own role used by an own tactic.
        QJsonObject a = m_shipped;
        QJsonObject roles = a.value(QLatin1String("player_roles")).toObject();
        QJsonObject dm = roles.value(QLatin1String("Defensive Midfield")).toObject();
        dm.insert(QStringLiteral("MYDM"), QStringLiteral("Mein Sechser"));
        roles.insert(QStringLiteral("Defensive Midfield"), dm);
        a.insert(QStringLiteral("player_roles"), roles);
        QJsonObject phases = a.value(QLatin1String("role_phases")).toObject();
        phases.insert(QStringLiteral("MYDM"), QStringLiteral("oop"));
        a.insert(QStringLiteral("role_phases"), phases);
        QJsonObject weights = a.value(QLatin1String("role_specific_weights")).toObject();
        weights.insert(QStringLiteral("MYDM"),
                       QJsonObject{{QStringLiteral("key"), QJsonArray{QStringLiteral("Tackling")}},
                                   {QStringLiteral("preferable"), QJsonArray{}}});
        a.insert(QStringLiteral("role_specific_weights"), weights);
        QJsonObject mapping = a.value(QLatin1String("position_to_role_mapping")).toObject();
        QJsonArray dmRoles = mapping.value(QLatin1String("DM")).toArray();
        dmRoles.append(QStringLiteral("MYDM"));
        mapping.insert(QStringLiteral("DM"), dmRoles);
        a.insert(QStringLiteral("position_to_role_mapping"), mapping);
        QJsonObject own = tactics(a).value(kControlPress).toObject();
        QJsonObject oop = own.value(QLatin1String("oop")).toObject();
        oop.insert(QStringLiteral("DMC"), QStringLiteral("MYDM"));
        own.insert(QStringLiteral("oop"), oop);
        TacticLibrary::putTactic(a, QStringLiteral("fm27"), QStringLiteral("Eigene"), own);
        const Definitions defsA = definitionsOf(a);
        const QByteArray file = TacticLibrary::exportTactic(defsA, QStringLiteral("Eigene"));
        QVERIFY(!file.isEmpty());

        // Profile B: shipped data only.
        const Definitions defsB = definitionsOf(m_shipped);
        TacticLibrary::ImportCheck check = TacticLibrary::checkImport(file, defsB);
        QCOMPARE(check.status, TacticLibrary::ImportCheck::Ok);
        QCOMPARE(check.name, QStringLiteral("Eigene"));
        QVERIFY(!check.nameTaken);
        QCOMPARE(check.rolesToAdd.size(), 1);
        QCOMPARE(check.rolesToAdd.first().id, QStringLiteral("MYDM"));
        QVERIFY(check.differingRoles.isEmpty());
        QJsonObject b = m_shipped;
        TacticLibrary::applyImport(b, QStringLiteral("fm27"), check, check.name);
        const Definitions after = definitionsOf(b);
        QCOMPARE(after.twoPhaseTactics().value(QStringLiteral("Eigene")).oop,
                 defsA.twoPhaseTactics().value(QStringLiteral("Eigene")).oop);
        QCOMPARE(after.twoPhaseTactics().value(QStringLiteral("Eigene")).ip,
                 defsA.twoPhaseTactics().value(QStringLiteral("Eigene")).ip);
        QCOMPARE(after.tacticLayouts().value(QStringLiteral("Eigene")),
                 defsA.tacticLayouts().value(QStringLiteral("Eigene")));
        QCOMPARE(after.rolePhase(QStringLiteral("MYDM")), QStringLiteral("oop"));
        QCOMPARE(after.roleWeights(QStringLiteral("MYDM")).key, QStringList{QStringLiteral("Tackling")});
        QVERIFY(after.positionToRoleMapping().value(QStringLiteral("DM")).contains(
            QStringLiteral("MYDM")));

        // Again into B: name conflict, role now known.
        check = TacticLibrary::checkImport(file, after);
        QCOMPARE(check.status, TacticLibrary::ImportCheck::Ok);
        QVERIFY(check.nameTaken);
        QVERIFY(check.rolesToAdd.isEmpty());
        QCOMPARE(TacticLibrary::uniqueName(check.name, after.tacticNames()),
                 QStringLiteral("Eigene (2)"));
    }

    void importRejectsBadFiles()
    {
        const Definitions fm27 = definitionsOf(m_shipped);
        const Definitions fm24 = definitionsOf(m_shipped24, QStringLiteral("fm24"));
        const QByteArray file = TacticLibrary::exportTactic(fm27, kControlPress);

        QCOMPARE(TacticLibrary::checkImport("kein json", fm27).status,
                 TacticLibrary::ImportCheck::InvalidFile);
        QCOMPARE(TacticLibrary::checkImport("{\"format\": \"etwas\"}", fm27).status,
                 TacticLibrary::ImportCheck::InvalidFile);
        // FM27 tactic into an FM24 profile.
        const TacticLibrary::ImportCheck wrong = TacticLibrary::checkImport(file, fm24);
        QCOMPARE(wrong.status, TacticLibrary::ImportCheck::WrongFmVersion);
        QCOMPARE(wrong.fileFmVersion, QStringLiteral("fm27"));
        // Newer format.
        QJsonObject obj = QJsonDocument::fromJson(file).object();
        obj.insert(QStringLiteral("format_version"), TacticLibrary::kExchangeFormatVersion + 1);
        QCOMPARE(TacticLibrary::checkImport(QJsonDocument(obj).toJson(), fm27).status,
                 TacticLibrary::ImportCheck::TooNew);
        // Unknown role without definition.
        obj = QJsonDocument::fromJson(file).object();
        QJsonObject tactic = obj.value(QLatin1String("tactic")).toObject();
        QJsonObject ip = tactic.value(QLatin1String("ip")).toObject();
        ip.insert(QStringLiteral("STC"), QStringLiteral("NIX"));
        tactic.insert(QStringLiteral("ip"), ip);
        obj.insert(QStringLiteral("tactic"), tactic);
        const auto missing = TacticLibrary::checkImport(QJsonDocument(obj).toJson(), fm27);
        QCOMPARE(missing.status, TacticLibrary::ImportCheck::MissingRoles);
        QCOMPARE(missing.unknownRoles, QStringList{QStringLiteral("NIX")});
        // Ten players only.
        ip.remove(QStringLiteral("STC"));
        tactic.insert(QStringLiteral("ip"), ip);
        obj.insert(QStringLiteral("tactic"), tactic);
        QCOMPARE(TacticLibrary::checkImport(QJsonDocument(obj).toJson(), fm27).status,
                 TacticLibrary::ImportCheck::InvalidFile);
    }

    void fm24RoundTrip()
    {
        const Definitions fm24 = definitionsOf(m_shipped24, QStringLiteral("fm24"));
        const QString name = fm24.tacticNames().first();
        const QByteArray file = TacticLibrary::exportTactic(fm24, name);
        const auto check = TacticLibrary::checkImport(file, fm24);
        QCOMPARE(check.status, TacticLibrary::ImportCheck::Ok);
        QVERIFY(check.nameTaken);
        QVERIFY(check.rolesToAdd.isEmpty());
        QJsonObject root = m_shipped24;
        TacticLibrary::applyImport(root, QStringLiteral("fm24"), check, QStringLiteral("Kopie"));
        const Definitions after = definitionsOf(root, QStringLiteral("fm24"));
        QCOMPARE(after.tacticRoles().value(QStringLiteral("Kopie")),
                 fm24.tacticRoles().value(name));
    }

    void favoritesAndBridgesFollow()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("tl"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        db.setSetting(QStringLiteral("favorite_tactic_1"), QStringLiteral("A"));
        db.setSetting(QStringLiteral("national_fav_tactic_2"), QStringLiteral("A"));
        db.setSetting(QStringLiteral("favorite_tactic_2"), QStringLiteral("B"));
        db.setSetting(QStringLiteral("bridges/A"),
                      QStringLiteral("{\"u1\":[\"DL\",\"DCL\"],\"u2\":[\"AMC\",\"STC\"]}"));
        TacticLibrary::renameTacticReferences(db, QStringLiteral("A"), QStringLiteral("C"));
        QCOMPARE(db.setting(QStringLiteral("favorite_tactic_1")), QStringLiteral("C"));
        QCOMPARE(db.setting(QStringLiteral("national_fav_tactic_2")), QStringLiteral("C"));
        QCOMPARE(db.setting(QStringLiteral("favorite_tactic_2")), QStringLiteral("B"));
        QVERIFY(db.setting(QStringLiteral("bridges/A")).isEmpty());
        QVERIFY(!db.setting(QStringLiteral("bridges/C")).isEmpty());

        // After an edit without AMC, the bridge over AMC goes.
        TacticLibrary::pruneBridges(db, QStringLiteral("C"), {QStringLiteral("DL")},
                                    {QStringLiteral("DCL"), QStringLiteral("STC")});
        const QJsonObject bridges =
            QJsonDocument::fromJson(db.setting(QStringLiteral("bridges/C")).toUtf8()).object();
        QCOMPARE(bridges.keys(), QStringList{QStringLiteral("u1")});

        TacticLibrary::removeTacticReferences(db, QStringLiteral("C"));
        QVERIFY(db.setting(QStringLiteral("favorite_tactic_1")).isEmpty());
        QVERIFY(db.setting(QStringLiteral("bridges/C")).isEmpty());
        QCOMPARE(db.setting(QStringLiteral("favorite_tactic_2")), QStringLiteral("B"));
        db.close();
    }
};

QTEST_GUILESS_MAIN(TestTacticLibrary)
#include "test_tacticlibrary.moc"
