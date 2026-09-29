#pragma once

#include "PageBase.h"

#include "core/Registration.h"

#include <QSet>
#include <QStringList>

#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace fm {

// Squad-registration assistant: proposes the league squad list (Premier
// League) or UEFA list A from the club's players, respecting the home-grown /
// club-trained quotas and never leaving a place empty that an eligible player
// could take. Recomputes live, so flag changes via the context menu show up
// immediately; the proposal can be saved as the registered list.
class RegistrationPage : public PageBase
{
    Q_OBJECT

public:
    explicit RegistrationPage(AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

private:
    // First team + second team, without retired players.
    std::vector<const Player *> pool() const;
    // Favorite tactics (up to two); the first tactic when none is set.
    QStringList rankingTactics(bool *usedFallback) const;
    Registration::Competition competition() const;
    void fillTable(const Registration::Proposal &proposal);
    void saveList();

    QLabel *m_hint = nullptr;
    QComboBox *m_competitionCombo = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_costLabel = nullptr;
    QLabel *m_warningLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_saveButton = nullptr;

    QSet<QString> m_proposedUids; // listed in the current proposal
    bool m_updating = false;
};

} // namespace fm
