#pragma once

#include <QPointer>
#include <QWidget>

class QComboBox;
class QToolButton;

namespace fm {

class Definitions;

// Two-phase role versions (FM27) double the role lists: every role exists
// with ball (IP) and without ball (OOP). This small segmented switch sits
// next to a role combo box and shows only the roles of one phase, so the
// list stays short and the phases are never mixed. Entries without a role
// (for example "any" or "all") stay visible. The switch hides itself when
// the combo holds no phase-split roles (older versions).
//
// The switch follows the combo on its own: it re-filters whenever the combo
// is refilled and jumps to the other phase when the code selects a role of
// that phase.
class RolePhaseSwitch : public QWidget
{
    Q_OBJECT
public:
    RolePhaseSwitch(const Definitions &definitions, QComboBox *combo, QWidget *parent = nullptr);

    // Creates a switch and puts it into the combo's layout, right before the
    // combo. Works for combos in (nested) box layouts.
    static RolePhaseSwitch *attach(const Definitions &definitions, QComboBox *combo);

    QString phase() const { return m_phase; }
    void setPhase(const QString &phase);

private:
    void scheduleRefresh();
    void refresh();
    void syncToCurrent();
    QString phaseOf(int row) const;

    const Definitions &m_definitions;
    QPointer<QComboBox> m_combo;
    QToolButton *m_ipButton = nullptr;
    QToolButton *m_oopButton = nullptr;
    QString m_phase = QStringLiteral("ip");
    bool m_refreshPending = false;
    bool m_applying = false;
};

} // namespace fm
