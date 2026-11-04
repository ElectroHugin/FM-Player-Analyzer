#pragma once

#include "PageBase.h"

#include <QHash>

class QComboBox;
class QGroupBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QPushButton;
class QPoint;

namespace fm {

class TacticPitchWidget;
class ThemeManager;

// Tactic editor: pick a role per pitch slot (exactly one goalkeeper and ten
// outfield players); writes tactic_roles + tactic_layouts into the
// definitions of the open save's profile. Port of legacy new_tactic.py.
// Above the editor the save's tactics can be opened for editing, renamed,
// duplicated, deleted, exported and imported (TacticLibrary, .fmpatactic).
// Versions with role phases (FM27) get two independent line-ups, one with
// ball and one without, saved as {"ip": {...}, "oop": {...}}.
class NewTacticPage : public PageBase
{
    Q_OBJECT

public:
    NewTacticPage(AppContext &context, ThemeManager &theme, QWidget *parent = nullptr);

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
        // slot key -> role combo (incl. GK); hidden, the pitch edits them.
        QHash<QString, QComboBox *> slotCombos;
        TacticPitchWidget *pitch = nullptr;
    };

    void buildPhaseEditor(PhaseEditor &editor, QWidget *parent);
    // Click on a pitch box: menu with the roles of that slot.
    void showSlotMenu(PhaseEditor &editor, const QString &slot, const QPoint &globalPos);
    void syncPitch(PhaseEditor &editor);
    void refreshPhase(PhaseEditor &editor);
    void applyTemplate(PhaseEditor &editor);
    bool collectPhase(const PhaseEditor &editor, QJsonObject &roles,
                      QHash<QString, QString> &outfield);
    bool twoPhase() const;
    void updateFullName();
    QString fullName() const;
    void createTactic();
    void resetForm();
    // Tactic management (the "Deine Taktiken" row).
    void refreshTacticList();
    QString selectedTactic() const;
    void loadTactic(const QString &name);
    void renameSelected();
    void duplicateSelected();
    void deleteSelected();
    void exportSelected();
    void importTactic();
    // Sets the edited root, saves it into the profile and reloads; shows an
    // error titled title on failure.
    bool commitDefinitions(const QJsonObject &root, const QString &title);
    void updateEditMode();
    void fillSlotCombo(QComboBox *combo, const QStringList &gamePositions,
                       const QString &phase);

    ThemeManager &m_theme;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_shapeEdit = nullptr;
    QLabel *m_fullNameLabel = nullptr;
    QLabel *m_info = nullptr;
    PhaseEditor m_phases[2];
    QPushButton *m_createButton = nullptr;
    QPushButton *m_newButton = nullptr;
    QLabel *m_heading = nullptr;
    QComboBox *m_tacticList = nullptr;
    QList<QPushButton *> m_selectionButtons; // need a selected tactic
    QString m_editingName; // tactic opened for editing, empty = new tactic
};

} // namespace fm
