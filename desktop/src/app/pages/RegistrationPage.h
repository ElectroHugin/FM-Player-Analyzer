#pragma once

#include "PageBase.h"

#include "core/Registration.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <vector>

class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QTableWidgetItem;

namespace fm {

// Squad-registration assistant: proposes the league squad list (Premier
// League) or UEFA list A from the club's players, respecting the home-grown /
// club-trained quotas and never leaving a place empty that an eligible player
// could take. Recomputes live, so flag changes via the context menu show up
// immediately.
//
// Each player who needs a slot has a checkbox: the user ticks who is really
// registered in the game and saves that as the squad list (Best XI then works
// with it). The ticks start as the proposal and afterwards show the saved
// list. Goalkeepers and outfield players are listed separately, in pitch order
// by default (goalkeeper to striker, right before left).
class RegistrationPage : public PageBase
{
    Q_OBJECT

public:
    explicit RegistrationPage(AppContext &context, QWidget *parent = nullptr);

    void refresh() override;
    void releaseStoreRows() override;

private:
    // What the assistant advises for one player of the pool.
    enum class Advice { List, Exempt, LeaveOut };
    struct Row {
        Registration::RankedPlayer entry;
        Advice advice = Advice::LeaveOut;
        QString note;  // why he is left out
        int order = 0; // position in the default (pitch) order
    };

    // First team + second team, without retired players.
    std::vector<const Player *> pool() const;
    // Favorite tactics (up to two); the first tactic when none is set.
    QStringList rankingTactics(bool *usedFallback) const;
    Registration::Competition competition() const;
    void fillTables();
    void fillTable(QTableWidget *table, const std::vector<Row> &rows);
    // Counts, quota cost, warnings and button states for the current ticks.
    void updateSelection();
    void onItemChanged(QTableWidgetItem *item);
    // Remembers the ticks as unsaved edits (or forgets them when they are back
    // at what a fresh page would show).
    void storePendingTicks();
    void adoptProposal();
    void saveList();

    // Fixed head above the scrolling part.
    QWidget *m_controls = nullptr; // competition row + counters
    QComboBox *m_competitionCombo = nullptr;
    QLabel *m_countsLabel = nullptr;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_content = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_usageHint = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_costLabel = nullptr;
    QLabel *m_warningLabel = nullptr;
    QGroupBox *m_keeperBox = nullptr;
    QTableWidget *m_keeperTable = nullptr;
    QGroupBox *m_outfieldBox = nullptr;
    QTableWidget *m_outfieldTable = nullptr;
    QPushButton *m_adoptButton = nullptr;
    QPushButton *m_saveButton = nullptr;

    // State of the last refresh. The players point into the store, so they are
    // dropped in releaseStoreRows() and rebuilt by refresh().
    std::vector<const Player *> m_pool;
    Registration::Proposal m_proposal;
    Registration::Quota m_quota;
    QStringList m_tactics;
    bool m_tacticFallback = false;

    QSet<QString> m_proposedUids;  // listed in the current proposal
    QSet<QString> m_savedUids;     // on the saved list (database)
    QSet<QString> m_checkableUids; // pool players who need a slot
    QSet<QString> m_checkedUids;   // currently ticked
    // Unsaved ticks per competition (combo value), kept across the live
    // refreshes a flag change triggers; reset when another database is opened.
    QHash<int, QSet<QString>> m_pendingTicks;
    QString m_pendingDb;

    bool m_updating = false;
};

} // namespace fm
