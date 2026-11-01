#include "Navigation.h"

#include <QSet>
#include <QtTest>

using namespace fm;

namespace {

// Page ids MainWindow::createPage builds (everything else is a placeholder).
const QSet<QString> &creatablePages()
{
    static const QSet<QString> pages = {
        QStringLiteral("dashboard"),         QStringLiteral("assign_roles"),
        QStringLiteral("role_analysis"),     QStringLiteral("squad_matrix"),
        QStringLiteral("transfers"),         QStringLiteral("player_profile"),
        QStringLiteral("player_comparison"), QStringLiteral("dwrs_development"),
        QStringLiteral("edit_player"),       QStringLiteral("best_xi"),
        QStringLiteral("tactic_explorer"),   QStringLiteral("national_dashboard"),
        QStringLiteral("national_squad_selection"), QStringLiteral("national_callup"),
        QStringLiteral("training_plan"),     QStringLiteral("registration"),
        QStringLiteral("national_squad_matrix"), QStringLiteral("national_best_xi"),
        QStringLiteral("new_role"),          QStringLiteral("new_tactic"),
        QStringLiteral("settings"),
    };
    return pages;
}

QStringList pageIds(const QList<Navigation::Section> &sections)
{
    QStringList ids;
    for (const auto &section : sections)
        for (const auto &group : section.groups)
            for (const auto &tab : group.tabs)
                ids << tab.pageId;
    return ids;
}

} // namespace

class TestNavigation : public QObject
{
    Q_OBJECT

private slots:
    void everyTabHasAPage_data()
    {
        QTest::addColumn<bool>("national");
        QTest::newRow("club") << false;
        QTest::newRow("national") << true;
    }
    void everyTabHasAPage()
    {
        QFETCH(bool, national);
        const auto sidebar = Navigation::sidebar(national);
        const QStringList ids = pageIds(sidebar);
        QSet<QString> seen;
        for (const QString &id : ids) {
            QVERIFY2(!seen.contains(id), qPrintable(id + QStringLiteral(" twice")));
            seen.insert(id);
            QVERIFY2(creatablePages().contains(Navigation::hostPage(id)), qPrintable(id));
        }
        QSet<QString> groupIds;
        for (const auto &section : sidebar) {
            QVERIFY(!section.groups.isEmpty());
            for (const auto &group : section.groups) {
                QVERIFY(!group.tabs.isEmpty());
                QVERIFY(!groupIds.contains(group.id));
                groupIds.insert(group.id);
            }
        }
        // Context menus and the player search jump to these in both modes.
        for (const char *id : {"player_profile", "player_comparison", "edit_player",
                               "assign_roles", "settings"})
            QVERIFY2(seen.contains(QLatin1String(id)), id);
    }

    void clubSidebarCoversEveryClubPage()
    {
        const QStringList ids = pageIds(Navigation::sidebar(false));
        for (const char *id : {"dashboard", "best_xi", "best_xi_youth", "tactic_explorer",
                               "squad_matrix", "role_analysis", "dwrs_development",
                               "training_plan", "signing_advisor", "transfers", "registration",
                               "scouting_matrix", "scouting_roles", "new_role", "new_tactic"})
            QVERIFY2(ids.contains(QLatin1String(id)), id);
    }

    void registrationTabAlwaysUnderSquad()
    {
        // Listed even without active rules, so it can always be found.
        const auto sidebar = Navigation::sidebar(false);
        const Navigation::Group *group =
            Navigation::findGroup(sidebar, QStringLiteral("registration"));
        QVERIFY(group);
        QCOMPARE(group->id, QStringLiteral("squad"));
        QVERIFY(!pageIds(Navigation::sidebar(true)).contains(QStringLiteral("registration")));
    }

    void sharedPagesHaveOneHost()
    {
        QCOMPARE(Navigation::hostPage(QStringLiteral("best_xi_youth")), QStringLiteral("best_xi"));
        QCOMPARE(Navigation::hostPage(QStringLiteral("signing_advisor")), QStringLiteral("best_xi"));
        QCOMPARE(Navigation::hostPage(QStringLiteral("scouting_matrix")),
                 QStringLiteral("squad_matrix"));
        QCOMPARE(Navigation::hostPage(QStringLiteral("scouting_roles")),
                 QStringLiteral("role_analysis"));
        QCOMPARE(Navigation::hostPage(QStringLiteral("dashboard")), QStringLiteral("dashboard"));
    }

    void findGroupLocatesTabs()
    {
        const auto club = Navigation::sidebar(false);
        QCOMPARE(Navigation::findGroup(club, QStringLiteral("best_xi_youth"))->id,
                 QStringLiteral("lineup"));
        QCOMPARE(Navigation::findGroup(club, QStringLiteral("scouting_roles"))->id,
                 QStringLiteral("scouting"));
        QVERIFY(!Navigation::findGroup(club, QStringLiteral("national_callup")));
        const auto national = Navigation::sidebar(true);
        QCOMPARE(Navigation::findGroup(national, QStringLiteral("tactic_explorer"))->id,
                 QStringLiteral("national_lineup"));
        QVERIFY(!Navigation::findGroup(national, QStringLiteral("training_plan")));
    }
};

QTEST_GUILESS_MAIN(TestNavigation)
#include "test_navigation.moc"
