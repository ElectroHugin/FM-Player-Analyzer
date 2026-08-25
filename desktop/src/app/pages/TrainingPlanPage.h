#pragma once

#include "PageBase.h"

class QComboBox;
class QLabel;
class QTableWidget;

namespace fm {

struct Player;

// Squad-wide training plan: every club player with a selectable training role
// (intersection of his positions and the current tactic) and the ranked
// attributes that would most improve his DWRS in that role. The chosen role is
// persisted per player.
class TrainingPlanPage : public PageBase
{
    Q_OBJECT

public:
    explicit TrainingPlanPage(AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

private:
    void rebuild();
    QString focusText(const Player &player, const QString &role) const;

    QComboBox *m_tacticCombo = nullptr;
    QLabel *m_hint = nullptr;
    QTableWidget *m_table = nullptr;
    bool m_updating = false;
};

} // namespace fm
