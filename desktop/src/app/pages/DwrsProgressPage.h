#pragma once

#include "PageBase.h"

#include "core/Development.h"

#include <QStringList>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace fm {

struct Player;
class LineChartWidget;
class ThemeManager;

// DWRS development of the own squad (national mode: the saved national squad),
// measured in uploads instead of calendar days (core/Development). One table
// answers "who is improving, who stalls" at a glance — sorted by the change
// over the chosen window, with a sparkline per player. By default every
// player's numbers refer to the role he plays in the Best XI of the favorite
// tactic. The chart beside it follows the selection: nothing selected = squad
// trend, one player = his role (more on request), several = comparison.
class DwrsProgressPage : public PageBase
{
    Q_OBJECT

public:
    DwrsProgressPage(AppContext &context, ThemeManager &theme, QWidget *parent = nullptr);

    void refresh() override;

private:
    // Where a player stands in the Best XI of the favorite tactic.
    enum class Spot { StartingXi, BTeam, Squad, Injured };
    struct LineupRole {
        QString role; // empty: no role of the tactic
        Spot spot = Spot::Squad;
    };

    // One table row: a player in the role his numbers refer to.
    struct Row {
        QString uid;
        int playerId = 0;
        QString name;
        int age = 0;
        bool youth = false;
        QString role;
        bool inLineup = false; // role comes from the Best XI (m_lineup)
        Spot spot = Spot::Squad;
        const Development::Track *track = nullptr; // points into m_timeline
        double change = 0.0;      // genuine change within the window
        double totalChange = 0.0; // genuine change over the whole history
    };

    std::vector<const Player *> poolPlayers() const;
    void reloadTimeline();      // DB read: history of the pool -> m_timeline
    void reloadLineup();        // Best XI of the favorite tactic -> m_lineup
    void fillRoleCombo();
    void rebuildRows();         // filters -> m_rows, table, KPIs
    void rebuildTable();
    void rebuildKpis();
    void rebuildChart();
    int window() const;         // uploads in the window, 0 = whole history
    QStringList selectedUids() const;
    // The role picked in the combo; empty for "Best XI" and "best role".
    QString chosenRole() const;
    // Role whose numbers a player shows: his Best XI role, the chosen role or
    // his best one. *fromLineup tells whether the Best XI decided it.
    QString basisRole(const Player &player, bool *fromLineup = nullptr) const;
    QList<QPointF> trackPoints(const Development::Track &track) const;

    ThemeManager &m_theme;

    QToolButton *m_explainToggle = nullptr;
    QLabel *m_explain = nullptr;
    QButtonGroup *m_windowGroup = nullptr;
    QButtonGroup *m_groupGroup = nullptr;
    QComboBox *m_roleCombo = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_notice = nullptr;

    QLabel *m_avgValue = nullptr;
    QLabel *m_avgCaption = nullptr;
    QLabel *m_riserValue = nullptr;
    QLabel *m_riserCaption = nullptr;
    QLabel *m_fallerValue = nullptr;
    QLabel *m_fallerCaption = nullptr;
    QLabel *m_snapshotValue = nullptr;
    QLabel *m_snapshotCaption = nullptr;

    QTableWidget *m_table = nullptr;
    QLabel *m_chartTitle = nullptr;
    LineChartWidget *m_chart = nullptr;
    QPushButton *m_clearSelection = nullptr;
    QPushButton *m_moreRoles = nullptr;

    Development::Timeline m_timeline;
    QHash<QString, LineupRole> m_lineup; // uid -> Best XI role
    QString m_lineupTactic;
    QStringList m_snapshotLabels; // hover text per snapshot
    std::vector<Row> m_rows;
    bool m_updating = false;
};

} // namespace fm
