#pragma once

#include "PageBase.h"

#include <QHash>
#include <QList>

#include <vector>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QTableWidget;
class QTabWidget;
class QToolButton;

namespace fm {

struct Player;
class SigningAdvisorTab;
class TacticPitchWidget;
class ThemeManager;
struct SquadResult;
struct DevelopmentSquads;
struct DepthOption;

// Best XI calculator: starting XI, B-team, depth options plus the youth /
// second-team development squads and surplus lists.
// Port of legacy best_position.py.
//
// With registration rules active the first team can be limited to the players
// who may play in a competition (its saved squad list plus those eligible
// without a slot); the development squads always look at the whole club.
//
// Players marked as long-term injured are left out of every squad unless the
// user plans them in; a table shows how the optimal line-up closes the gap.
//
// The signing advisor (SigningAdvisorTab) starts from the same squad. The
// page serves three navigation targets that share its controls: "best_xi"
// (first team), "best_xi_youth" (youth & second team) and "signing_advisor".
class BestXiPage : public PageBase
{
    Q_OBJECT

public:
    BestXiPage(AppContext &context, ThemeManager &theme, QWidget *parent = nullptr);

    void refresh() override;
    void releaseStoreRows() override;
    void showSection(const QString &pageId) override;

private:
    // How the team selection works, shown under the tactic choice.
    static QString algorithmHint();
    void rebuild();
    // Who replaces the long-term injured: slot by slot, all fit vs without them.
    void fillInjuryBox(const std::vector<const Player *> &injured,
                       const SquadResult &allFit, const SquadResult &withoutInjured,
                       const QStringList &slotOrder, const QHash<QString, QString> &positions);
    void fillSurplusTable(QTableWidget *table, const std::vector<const Player *> &players,
                          bool includeTalent);
    void fillDepthTable(const QHash<QString, QList<DepthOption>> &options);

    ThemeManager &m_theme;

    QLabel *m_heading = nullptr;
    QComboBox *m_tacticCombo = nullptr;
    QLabel *m_competitionLabel = nullptr;
    QComboBox *m_competitionCombo = nullptr; // only with registration rules
    QLabel *m_registrationNote = nullptr;    // who is left out, or why nobody is
    QLabel *m_hint = nullptr;         // algorithm explanation, shown on demand
    QToolButton *m_hintToggle = nullptr;
    QLabel *m_clubNotice = nullptr;   // "choose your club first"
    QGroupBox *m_injuryBox = nullptr; // only while someone is marked injured
    QLabel *m_injuryNote = nullptr;   // names (clickable) + what it costs
    QCheckBox *m_planInjuredCheck = nullptr;
    QTableWidget *m_injuryTable = nullptr;
    QTabWidget *m_tabs = nullptr;

    TacticPitchWidget *m_xiPitch = nullptr;
    TacticPitchWidget *m_bTeamPitch = nullptr;
    QGroupBox *m_depthBox = nullptr;
    QTableWidget *m_depthTable = nullptr;
    QLabel *m_depthEmpty = nullptr;

    QLabel *m_secondXiTitle = nullptr;
    TacticPitchWidget *m_secondXiPitch = nullptr;
    TacticPitchWidget *m_youthXiPitch = nullptr;
    QGroupBox *m_loanBox = nullptr;
    QTableWidget *m_loanTable = nullptr;
    QGroupBox *m_sellBox = nullptr;
    QTableWidget *m_sellTable = nullptr;

    SigningAdvisorTab *m_signingTab = nullptr;

    bool m_updating = false;
};

} // namespace fm
