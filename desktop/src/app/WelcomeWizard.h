#pragma once

#include <QDialog>
#include <QList>

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace fm {

class AppContext;
class ColorTemplateStore;
class ThemeManager;

// Welcome assistant for a save: appears once the save holds its first players
// (MainWindow::maybeShowWelcome) and walks through the settings that are easy
// to forget at the start: club, second team, display name, stadium, country,
// logo, favorite tactics, registration rules and colors. Everything stays
// editable in Settings; the assistant can be reopened from the Help menu.
class WelcomeWizard : public QDialog
{
    Q_OBJECT

public:
    WelcomeWizard(AppContext &context, ThemeManager &theme, QWidget *parent = nullptr);
    ~WelcomeWizard() override;

    // Profile setting that marks the assistant as finished or skipped.
    static QString doneSettingKey();
    // True when the open save should get the assistant on its own: it holds
    // players, no club is set and the assistant was neither finished nor skipped.
    static bool shouldOffer(AppContext &context);

private:
    QWidget *buildClubPage();
    QWidget *buildPlayPage();
    QWidget *buildLookPage();

    void goTo(int step);
    void next();
    void back();
    void skipForGood();
    void finish();

    void fillClubs();
    void fillPresets();
    void suggestPresetForClub();
    void chooseLogo();
    void removeLogo();
    void updateLogoPreview();
    void markDone();

    AppContext &m_context;
    ThemeManager &m_theme;
    std::unique_ptr<ColorTemplateStore> m_templates;

    QList<QLabel *> m_stepLabels;
    QStackedWidget *m_pages = nullptr;

    QComboBox *m_clubCombo = nullptr;
    QComboBox *m_secondClubCombo = nullptr;
    QLineEdit *m_fullNameEdit = nullptr;
    QLineEdit *m_stadiumEdit = nullptr;
    QLineEdit *m_countryEdit = nullptr;
    QCheckBox *m_followCheck = nullptr; // FM27: follow the game's save file
    QLineEdit *m_followEdit = nullptr;
    QLabel *m_logoPreview = nullptr;
    QPushButton *m_logoRemoveButton = nullptr;

    QComboBox *m_tactic1Combo = nullptr;
    QComboBox *m_tactic2Combo = nullptr;
    QWidget *m_registrationBox = nullptr;
    QComboBox *m_leagueCombo = nullptr;
    QCheckBox *m_uefaCheck = nullptr;
    QWidget *m_ipShareBox = nullptr;
    QSpinBox *m_ipShareSpin = nullptr;

    QComboBox *m_presetCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;
    bool m_presetTouched = false;

    QPushButton *m_skipButton = nullptr;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_nextButton = nullptr;
};

} // namespace fm
