#pragma once

#include "PageBase.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QStringList>

#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableView;
class QVBoxLayout;

namespace fm {

struct Player;
class PersonalityFilterWidget;
class PlayerTableModel;
class PlayerFilterProxy;
struct PlayerColumn;

// DWRS matrix (players x roles) in two tabs: "Scouting" (every non-club player
// with talent, personality, attribute-threshold and market filters, shortlist
// toggle and a delta to the club's best player in the chosen role) and
// "Mein Kader" (first team and/or second team, optionally youth only, never
// touched by the scouting filters). CSV export on both. The tabs are two
// navigation targets: "scouting_matrix" and "squad_matrix" (showSection).
// Grew out of legacy player_role_matrix.py.
class SquadMatrixPage : public PageBase
{
    Q_OBJECT

public:
    explicit SquadMatrixPage(AppContext &context, QWidget *parent = nullptr);

    void refresh() override;
    void showSection(const QString &pageId) override;
    void releaseStoreRows() override;

private:
    struct TableSection {
        QGroupBox *box = nullptr;
        QLineEdit *search = nullptr;
        PlayerTableModel *model = nullptr;
        PlayerFilterProxy *proxy = nullptr;
        QTableView *table = nullptr;
    };

    // One "attribute >= threshold" row of the attribute-threshold panel.
    struct AttributeRow {
        QWidget *widget = nullptr;
        QComboBox *attribute = nullptr;
        QSpinBox *minimum = nullptr;
    };

    QWidget *buildScoutingTab();
    QWidget *buildSquadTab();
    void buildTable(TableSection *section, const QString &title, QWidget *parent, bool scouted);
    QList<PlayerColumn> buildColumns(bool clubTable) const;
    void rebuildAll();
    void rebuildScoutedOnly();
    QStringList selectedRoles() const;
    void toggleShortlist(const Player *player);
    void exportCsv(const TableSection &section);

    // Attribute thresholds (scouting): highlight or filter players whose
    // attribute reaches a minimum, e.g. Decisions for an AM(C).
    void addAttributeRow(int attrIndex, int minimum);
    void removeAttributeRow(QWidget *rowWidget);
    QList<QPair<int, int>> activeThresholds() const; // (attr index, minimum)
    void attributeThresholdsChanged();
    void loadAttributeThresholds();

    QTabWidget *m_tabs = nullptr;
    QLabel *m_heading = nullptr;

    // Shared options
    QComboBox *m_tacticCombo = nullptr;
    QCheckBox *m_extraDetailsCheck = nullptr;

    // "Mein Kader": first team / second team / both, optionally youth only
    QWidget *m_squadScopeBox = nullptr; // hidden without a second team
    QButtonGroup *m_squadScope = nullptr;
    QCheckBox *m_youthOnlyCheck = nullptr;

    // Scouting filters
    QCheckBox *m_hideRetiredCheck = nullptr;
    PersonalityFilterWidget *m_personalityFilter = nullptr;
    QComboBox *m_scoutedFilterCombo = nullptr;
    QCheckBox *m_freeAgentsCheck = nullptr;
    QComboBox *m_scoutedNatCombo = nullptr; // nationality scope, independent of talent filter
    QSpinBox *m_dwrsMinSpin = nullptr;
    QSpinBox *m_dwrsMaxSpin = nullptr;
    QSpinBox *m_minAgeSpin = nullptr;
    QSpinBox *m_maxAgeSpin = nullptr;
    QSlider *m_maxValueSlider = nullptr;
    QLabel *m_maxValueLabel = nullptr;

    // Talent filter (scouting)
    QGroupBox *m_talentBox = nullptr;
    QButtonGroup *m_talentScope = nullptr;
    QSlider *m_talentAgeSlider = nullptr;
    QLabel *m_talentAgeLabel = nullptr;
    QSlider *m_talentMentalitySlider = nullptr;
    QLabel *m_talentMentalityLabel = nullptr;
    QCheckBox *m_talentGoodOnlyCheck = nullptr;

    // Attribute thresholds (scouting)
    QGroupBox *m_attrBox = nullptr;
    QVBoxLayout *m_attrRowsLayout = nullptr;
    QCheckBox *m_attrOnlyMatchesCheck = nullptr;
    QList<AttributeRow> m_attrRows;
    QHash<int, QString> m_attrShortNames; // attr index -> export header ("Dec")
    QString m_attrLoadedDb;               // database the thresholds were loaded for

    TableSection m_clubSection;
    TableSection m_scoutedSection;

    std::vector<const Player *> m_scoutedPool; // after the scouting pool filters
    std::vector<const Player *> m_firstTeam;   // benchmark for the scouting delta
    QHash<QString, double> m_talentScores;     // uid -> score (talent filter on)
    bool m_updating = false;
};

} // namespace fm
