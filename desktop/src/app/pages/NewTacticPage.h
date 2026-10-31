#pragma once

#include "PageBase.h"

#include <QHash>

class QComboBox;
class QGroupBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QPushButton;

namespace fm {

// Editor for new tactical formations: pick a role per pitch slot (exactly one
// goalkeeper and ten outfield players); writes tactic_roles + tactic_layouts
// into definitions.json. Port of legacy new_tactic.py.
// Versions with role phases (FM27) get two independent line-ups, one with
// ball and one without, saved as {"ip": {...}, "oop": {...}}.
class NewTacticPage : public PageBase
{
    Q_OBJECT

public:
    explicit NewTacticPage(AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

private:
    // One line-up editor. Index 0 = without ball (the only one for FM24),
    // index 1 = with ball.
    struct PhaseEditor {
        QString phase; // "oop" or "ip"
        QGroupBox *box = nullptr;
        QWidget *templateRow = nullptr;
        QComboBox *templateCombo = nullptr;
        QLabel *countLabel = nullptr;
        QHash<QString, QComboBox *> slotCombos; // slot key -> role combo (incl. GK)
    };

    void buildPhaseEditor(PhaseEditor &editor, QWidget *parent);
    void refreshPhase(PhaseEditor &editor);
    void applyTemplate(PhaseEditor &editor);
    bool collectPhase(const PhaseEditor &editor, QJsonObject &roles,
                      QHash<QString, QString> &outfield);
    bool twoPhase() const;
    void updateFullName();
    QString fullName() const;
    void createTactic();
    void resetForm();
    void fillSlotCombo(QComboBox *combo, const QStringList &gamePositions,
                       const QString &phase);

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_shapeEdit = nullptr;
    QLabel *m_fullNameLabel = nullptr;
    QLabel *m_info = nullptr;
    PhaseEditor m_phases[2];
    QPushButton *m_createButton = nullptr;
};

} // namespace fm
