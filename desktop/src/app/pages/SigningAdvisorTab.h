#pragma once

#include "core/GapAnalysis.h"
#include "core/SigningAdvisor.h"

#include <QWidget>

#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;

namespace fm {

class AppContext;

// Best XI tab "Signing advisor": for which position does buying really make
// the line-up better, given the market the budget affords? Every promising
// market player is tried in the squad and the teams are solved again
// (SigningAdvisor); the table ranks the positions by the measured XI gain and
// lists the best signings per position. The legacy gap analysis (median /
// displacement flags) sits below, collapsed.
class SigningAdvisorTab : public QWidget
{
    Q_OBJECT

public:
    explicit SigningAdvisorTab(AppContext &context, QWidget *parent = nullptr);

    // The squad Best XI plans with (injuries and competition already applied)
    // for the tactic. Recomputes now if visible, otherwise when shown.
    void setSquad(const QString &tactic, std::vector<const Player *> squad);
    // Drops every Player* (store reload while the page is hidden).
    void clear();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void loadFilters();
    void saveFilters();
    std::vector<const Player *> marketPool() const;
    void recompute();
    void fillNeeds();
    void showOptions();
    void fillGapTables(const SquadResult &squad);
    QString moveText(const SquadMove &move) const;

    AppContext &m_context;
    QString m_tactic;
    std::vector<const Player *> m_squad;
    SigningAnalysis m_analysis;
    bool m_dirty = false;
    bool m_loading = false;
    QTimer *m_debounce = nullptr;

    QDoubleSpinBox *m_budgetSpin = nullptr;
    QCheckBox *m_noLimitCheck = nullptr;
    QSpinBox *m_maxAgeSpin = nullptr;
    QCheckBox *m_shortlistOnlyCheck = nullptr;
    QToolButton *m_hintToggle = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_summary = nullptr;
    QTableWidget *m_needsTable = nullptr;
    QLabel *m_optionsTitle = nullptr;
    QTableWidget *m_optionsTable = nullptr;

    QToolButton *m_gapToggle = nullptr;
    QWidget *m_gapBody = nullptr;
    QLabel *m_gapCaption = nullptr;
    QLabel *m_xiGapStatus = nullptr;
    QTableWidget *m_xiGapTable = nullptr;
    QLabel *m_bGapStatus = nullptr;
    QTableWidget *m_bGapTable = nullptr;
};

} // namespace fm
