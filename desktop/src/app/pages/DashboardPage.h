#pragma once

#include "PageBase.h"

#include "core/SquadBuilder.h"

#include "../ImportRunner.h"

#include <QSet>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QFrame;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QProgressDialog;
class QPushButton;
class QSlider;
class QTableWidget;
class QTimer;

namespace fm {

class StrengthGridWidget;
class ThemeManager;

// Club-mode dashboard: HTML import, tactic-based squad KPIs, positional
// strength grid, club roster and transfer-target suggestions.
// Port of legacy app.main_page.
class DashboardPage : public PageBase
{
    Q_OBJECT

public:
    DashboardPage(AppContext &context, ThemeManager &theme, QWidget *parent = nullptr);

    void refresh() override;

    // Puts filePath into the import field so the user only confirms the
    // options and clicks "Importieren" (drag & drop, Datei → Import).
    void prepareImport(const QString &filePath);

private:
    QWidget *buildImportSection(QWidget *parent);
    QWidget *buildAnalysisHeader(QWidget *parent);
    QWidget *buildKpiRow(QWidget *parent);
    QWidget *buildSquadSection(QWidget *parent);
    QWidget *buildSuggestionSection(QWidget *parent);

    void chooseImportFile();
    void startImport();
    void importFinished(const ImportPipelineResult &result);
    void resolveDepartures(const QSet<QString> &affectedUids);

    void refreshCombos();
    void refreshAnalysis();
    void updateKpis(const std::vector<const Player *> &coreSquad);
    void updateClubTable(const std::vector<const Player *> &clubPlayers);
    void rebuildSuggestions();
    void clearSuggestionCards();
    // Positional strength (2.2.3): the starters of the selected tactic with
    // their role DWRS and what their absence costs the XI.
    void updateStarterView(const std::vector<const Player *> &clubPlayers, const QString &tactic);
    void showStarterView(); // markers of the chosen tactical view

    QString selectedTactic() const;
    double maxValueFilter() const;

    ThemeManager &m_theme;

    // Import section
    QLineEdit *m_filePathEdit = nullptr;
    QCheckBox *m_squadUpdateCheck = nullptr;
    QCheckBox *m_autoAssignCheck = nullptr;
    QPushButton *m_importButton = nullptr;
    bool m_importRunning = false;
    bool m_pendingSquadUpdate = false;

    // Analysis header
    QComboBox *m_clubCombo = nullptr;
    QComboBox *m_tacticCombo = nullptr;
    QLabel *m_analysisHint = nullptr;

    // KPI tiles
    QLabel *m_kpiPlayers = nullptr;
    QLabel *m_kpiTotalValue = nullptr;
    QLabel *m_kpiAvgValue = nullptr;
    QLabel *m_kpiAvgAge = nullptr;
    // In-game date and season of the newest save game read in (hidden
    // until a save brought one).
    QFrame *m_kpiGameDateTile = nullptr;
    QLabel *m_kpiGameDate = nullptr;
    QLabel *m_kpiGameDateCaption = nullptr;
    QWidget *m_kpiRow = nullptr;

    // Squad section
    QWidget *m_squadSection = nullptr;
    StrengthGridWidget *m_strengthGrid = nullptr;    // single view / with ball
    StrengthGridWidget *m_strengthGridOop = nullptr; // "Both": without ball
    QLabel *m_gridTitle = nullptr;
    QLabel *m_gridTitleOop = nullptr;
    QLabel *m_gridLegend = nullptr;
    QLabel *m_colorLegend = nullptr; // squad comparison: what the colours mean
    void updateColorLegend();
    QWidget *m_phaseSwitch = nullptr;
    QButtonGroup *m_colorModeGroup = nullptr;
    void applyStrengthColorMode();
    int m_phaseView = 0; // 0 combined, 1 with ball, 2 without, 3 both
    // Last computed starters (no pointers into the store).
    struct StarterView {
        bool valid = false;
        bool twoPhase = false;
        QString tactic;
        QHash<QString, XiCell> xi;    // FM24: slot -> starter
        QHash<QString, XiCell> ip;    // two-phase: slot -> starter
        QHash<QString, XiCell> oop;
        QHash<QString, QPair<QString, QString>> slotsOfUid;
        QHash<QString, double> valueOfUid;
        QHash<QString, QString> impact;  // uid -> "XI −4.0" / "no full XI"
        QHash<QString, QString> tooltip; // uid -> who comes on, who moves
        int ipShare = 50;
        QString key; // inputs it was computed from (see updateStarterView)
    } m_starters;
    QLabel *m_clubTableTitle = nullptr;
    QTableWidget *m_clubTable = nullptr;

    // Transfer suggestions
    QGroupBox *m_suggestionBox = nullptr;
    QSlider *m_ageSlider = nullptr;
    QLabel *m_ageLabel = nullptr;
    QSlider *m_valueSlider = nullptr;
    QLabel *m_valueLabel = nullptr;
    QLabel *m_suggestionStatus = nullptr;
    QGridLayout *m_cardsLayout = nullptr;
    QTimer *m_suggestionDebounce = nullptr;

    bool m_updatingCombos = false;
};

} // namespace fm
