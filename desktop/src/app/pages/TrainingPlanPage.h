#pragma once

#include "PageBase.h"

#include <QString>

class QComboBox;
class QLabel;
class QTableWidget;

namespace fm {

struct Player;

// Squad-wide training plan: first team and second team shown separately, each a
// freely sortable table (name, age, position, …) of players with a selectable
// training role (positions ∩ tactic) and the training methods that would most
// raise their DWRS in that role. The chosen role is persisted per player.
class TrainingPlanPage : public PageBase
{
    Q_OBJECT

public:
    explicit TrainingPlanPage(AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

private:
    struct Section {
        QLabel *title = nullptr;
        QTableWidget *table = nullptr;
        QString club;
        int sortColumn = 0;
        bool sortDescending = false;
    };

    QTableWidget *makeTable();
    void fillSection(Section &section);
    QString focusText(const Player &player, const QString &role) const;

    QComboBox *m_tacticCombo = nullptr;
    QLabel *m_hint = nullptr;
    Section m_first;
    Section m_second;
    bool m_updating = false;
};

} // namespace fm
