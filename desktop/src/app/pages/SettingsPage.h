#pragma once

#include "core/ColorTemplates.h"

#include <memory>

#include "PageBase.h"

#include <QHash>

class QCheckBox;
class QDateEdit;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace fm {

class ThemeManager;

// Central settings page. A section list on the left (grouped: save, data,
// calculation, program) switches between short pages of cards; one footer
// saves everything. Explanations are folded away behind a small toggle.
class SettingsPage : public PageBase
{
    Q_OBJECT

public:
    SettingsPage(AppContext &context, ThemeManager &theme, QWidget *parent = nullptr);

    void refresh() override;

    // Opens a section by its German title (screenshot tour); false if unknown.
    bool openSettingsSection(const QString &germanTitle);

signals:
    void recalcRequested(); // weights changed or asked for -> MainWindow runs the recalc

private:
    QWidget *buildClubPage();
    QWidget *buildNationalPage();
    QWidget *buildRegistrationPage();
    QWidget *buildImportPage();
    QWidget *buildHistoryPage();
    QWidget *buildGraphicsPage();
    QWidget *buildWeightsPage();
    QWidget *buildSquadPage();
    QWidget *buildTrainingPage();
    QWidget *buildThemePage();
    QWidget *buildDatabasePage();
    void addSection(const QString &groupTitle, const QString &title, const char *germanTitle,
                    QWidget *page);

    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    QHash<QString, int> m_sectionRows; // German title -> nav row
    QString m_currentNavGroup;
    QLabel *m_footerScope = nullptr;
    void saveAll();
    void updateContrastWarning();

    ThemeManager &m_theme;

    // Weights tab widgets keyed by category / option name.
    QHash<QString, QDoubleSpinBox *> m_weightSpins;
    QHash<QString, QDoubleSpinBox *> m_gkWeightSpins;
    QDoubleSpinBox *m_keyMultSpin = nullptr;
    QDoubleSpinBox *m_prefMultSpin = nullptr;
    QHash<QString, QDoubleSpinBox *> m_aptSpins;

    // Thresholds tab.
    QSpinBox *m_outfielderAgeSpin = nullptr;
    QSpinBox *m_goalkeeperAgeSpin = nullptr;
    QDoubleSpinBox *m_naturalPosSpin = nullptr;
    QSpinBox *m_ipShareSpin = nullptr; // FM26+: share of the in-possession role
    QDoubleSpinBox *m_bridgeToleranceSpin = nullptr;
    QSpinBox *m_maxDepthRolesSpin = nullptr;
    QSpinBox *m_minLoanTalentSpin = nullptr;
    QSpinBox *m_minLoanAgeSpin = nullptr;
    QSpinBox *m_youthLoanOverAgeSpin = nullptr;
    QDoubleSpinBox *m_displacementSpin = nullptr;
    QDoubleSpinBox *m_dropoffSpin = nullptr;
    QDoubleSpinBox *m_wrongSideSpin = nullptr;
    QSpinBox *m_retirementAgeSpin = nullptr;   // Freshness X
    QSpinBox *m_staleUploadsSpin = nullptr;    // Freshness Y
    // Training age windows (peak/locked per group).
    QSpinBox *m_explosivePeakSpin = nullptr;
    QSpinBox *m_explosiveLockedSpin = nullptr;
    QSpinBox *m_strengthPeakSpin = nullptr;
    QSpinBox *m_strengthLockedSpin = nullptr;
    QSpinBox *m_technicalPeakSpin = nullptr;
    QSpinBox *m_technicalLockedSpin = nullptr;

    // Theme tab: preset picker + color buttons per mode+role.
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QPushButton *m_deleteTemplateButton = nullptr;
    QComboBox *m_headerStyleCombo = nullptr;
    QCheckBox *m_watermarkCheck = nullptr;
    std::unique_ptr<ColorTemplateStore> m_templates;
    QHash<QString, QPushButton *> m_colorButtons; // "night_primary_color" etc.
    QHash<QString, QString> m_pendingColors;
    QString m_pendingPreset;
    QLabel *m_contrastLabel = nullptr;

    void applyPresetToPending(const QString &presetId);
    // Built-in presets and the user's templates, grouped by league; own
    // templates without a league under "Custom".
    void fillPresetCombo();
    void saveColorTemplate();
    void deleteColorTemplate();
    void selectPresetInCombo(const QString &presetId);
    void markCustomPreset();

    // Database tab.
    QCheckBox *m_autoOpenCheck = nullptr;
    QCheckBox *m_attributeMaskingCheck = nullptr; // per save: FM27 save-game import
    QCheckBox *m_historyAllCheck = nullptr; // per save: snapshots of every player
    QDateEdit *m_seasonStartEdit = nullptr; // per save: first day of a season (day + month)
    QCheckBox *m_liveSaveCheck = nullptr;          // per save: follow an FM27 save
    QLineEdit *m_liveSaveEdit = nullptr;
    QComboBox *m_dbCombo = nullptr;

    // Club tab (settings table keys, same as legacy).
    QComboBox *m_fmVersionCombo = nullptr;
    QLabel *m_weightsVersionLabel = nullptr;
    QLineEdit *m_importDirEdit = nullptr; // app-wide (config.ini), not per save
    QComboBox *m_userClubCombo = nullptr;
    QComboBox *m_secondClubCombo = nullptr;
    QLineEdit *m_clubCountryEdit = nullptr;
    QLineEdit *m_fullClubNameEdit = nullptr;
    QLineEdit *m_stadiumEdit = nullptr;
    QLabel *m_logoPreview = nullptr;
    QPushButton *m_logoRemoveButton = nullptr;
    void chooseLogo();
    void removeLogo();
    void updateLogoPreview();
    // Optional portraits/crests from the FM graphics folder.
    QCheckBox *m_graphicsCheck = nullptr;
    QWidget *m_graphicsBody = nullptr;
    QLineEdit *m_graphicsDirEdit = nullptr;
    QLineEdit *m_clubFmIdEdit = nullptr;
    QLabel *m_graphicsStatus = nullptr;
    QLabel *m_graphicsCrestPreview = nullptr;
    void updateGraphicsStatus();
    QComboBox *m_favTactic1Combo = nullptr;
    QComboBox *m_favTactic2Combo = nullptr;
    QWidget *m_registrationCard = nullptr;
    QLabel *m_registrationUnavailable = nullptr;
    QComboBox *m_registrationLeagueCombo = nullptr;
    QCheckBox *m_registrationUefaCheck = nullptr;
    QSpinBox *m_registrationMinGkSpin = nullptr;
    QLineEdit *m_natNameEdit = nullptr;
    QLabel *m_flagPreview = nullptr;
    QPushButton *m_flagRemoveButton = nullptr;
    void chooseFlag();
    void removeFlag();
    void updateFlagPreview();
    QLineEdit *m_natCodeEdit = nullptr;
    QSpinBox *m_natAgeSpin = nullptr;
    QComboBox *m_natFav1Combo = nullptr;
    QComboBox *m_natFav2Combo = nullptr;
};

} // namespace fm
