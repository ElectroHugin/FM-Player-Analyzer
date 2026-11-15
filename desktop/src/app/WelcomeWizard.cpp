#include "WelcomeWizard.h"

#include "AppContext.h"
#include "theming/ThemeManager.h"
#include "theming/ThemePresets.h"
#include "core/AppConfig.h"
#include "core/ColorTemplates.h"
#include "core/Constants.h"
#include "core/Database.h"
#include "core/Registration.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimer>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fm {

namespace {

enum Step { ClubStep = 0, PlayStep = 1, LookStep = 2, StepCount = 3 };

// An opened explanation must not be squeezed or cut off: the dialog grows in
// height (up to the screen) so the whole text fits without dragging the window.
void growWindowToFit(QWidget *window)
{
    if (!window || !window->layout())
        return;
    QLayout *layout = window->layout();
    layout->activate();
    int needed = layout->hasHeightForWidth() ? layout->totalHeightForWidth(window->width())
                                             : layout->totalSizeHint().height();
    needed = std::max(needed, layout->totalMinimumSize().height());
    if (needed <= window->height())
        return;
    QRect available = window->screen() ? window->screen()->availableGeometry() : QRect();
    if (available.isValid())
        needed = std::min(needed, available.height() - 48);
    if (needed <= window->height())
        return;
    window->resize(window->width(), needed);
    if (available.isValid()) {
        QRect frame = window->frameGeometry();
        if (frame.bottom() > available.bottom())
            window->move(frame.left(), std::max(available.top(), available.bottom() - frame.height()));
    }
}

// A collapsible explanation: a small arrow button that shows/hides the text.
QWidget *foldableHint(const QString &title, const QString &text, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *toggle = new QToolButton(box);
    toggle->setObjectName(QStringLiteral("foldButton"));
    toggle->setText(title);
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setCursor(Qt::PointingHandCursor);
    auto *body = new QLabel(text, box);
    body->setObjectName(QStringLiteral("kpiCaption"));
    body->setWordWrap(true);
    body->hide();
    layout->addWidget(toggle, 0, Qt::AlignLeft);
    layout->addWidget(body);
    QObject::connect(toggle, &QToolButton::toggled, body, [toggle, body, box](bool on) {
        toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(on);
        if (on)
            QTimer::singleShot(0, box, [box] { growWindowToFit(box->window()); });
    });
    return box;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("kpiCaption"));
    label->setWordWrap(true);
    return label;
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("fieldLabel"));
    return label;
}

// Same scaling as the settings page: the header shows the logo at 34 px.
QByteArray pngBytes(QPixmap pixmap)
{
    if (pixmap.width() > 512 || pixmap.height() > 512)
        pixmap = pixmap.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return bytes;
}

QString normalizedName(QString name)
{
    name = name.toCaseFolded();
    name.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]")));
    return name;
}

} // namespace

QString WelcomeWizard::doneSettingKey()
{
    return QStringLiteral("welcome_wizard_done");
}

bool WelcomeWizard::shouldOffer(AppContext &context)
{
    if (!context.hasDatabase() || context.store().size() == 0)
        return false;
    // Saves set up before the assistant existed already know their club.
    if (!context.userClub().isEmpty())
        return false;
    return context.database().setting(doneSettingKey()).isEmpty();
}

WelcomeWizard::WelcomeWizard(AppContext &context, ThemeManager &theme, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
    , m_theme(theme)
    , m_templates(std::make_unique<ColorTemplateStore>(context.paths().colorTemplatesFile()))
{
    m_templates->load();
    setWindowTitle(tr("Willkommen"));
    setMinimumSize(720, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 22, 28, 18);
    layout->setSpacing(16);

    auto *title = new QLabel(tr("Richte deinen Spielstand ein"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(caption(
        tr("Die ersten Spieler sind da (%n Spieler). Mit ein paar Angaben weiß der Analyzer, "
           "welcher Kader deiner ist und wie du spielst. Alles lässt sich später unter "
           "Einstellungen ändern, und diesen Assistenten findest du jederzeit unter "
           "Hilfe → Willkommens-Assistent.",
           nullptr, m_context.store().size()),
        this));

    // Step indicator (same look as the new-save assistant).
    auto *steps = new QHBoxLayout;
    steps->setSpacing(10);
    const QStringList stepNames = {tr("Verein"), tr("Taktik und Kader"), tr("Farben")};
    for (int i = 0; i < stepNames.size(); ++i) {
        if (i > 0) {
            auto *line = new QFrame(this);
            line->setObjectName(QStringLiteral("stepLine"));
            line->setFixedHeight(2);
            line->setMinimumWidth(30);
            steps->addWidget(line, 1);
        }
        auto *number = new QLabel(QString::number(i + 1), this);
        number->setObjectName(QStringLiteral("stepNumber"));
        number->setAlignment(Qt::AlignCenter);
        auto *label = new QLabel(stepNames.at(i), this);
        label->setObjectName(QStringLiteral("stepLabel"));
        m_stepLabels.append(number);
        m_stepLabels.append(label);
        steps->addWidget(number);
        steps->addWidget(label);
    }
    layout->addLayout(steps);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildClubPage());
    m_pages->addWidget(buildPlayPage());
    m_pages->addWidget(buildLookPage());
    layout->addWidget(m_pages, 1);

    auto *footer = new QHBoxLayout;
    m_skipButton = new QPushButton(tr("Nicht mehr anzeigen"), this);
    m_skipButton->setToolTip(tr("Schließt den Assistenten für diesen Spielstand. Du kannst ihn "
                                "über Hilfe → Willkommens-Assistent wieder öffnen."));
    m_backButton = new QPushButton(tr("Zurück"), this);
    m_nextButton = new QPushButton(tr("Weiter"), this);
    m_nextButton->setDefault(true);
    footer->addWidget(m_skipButton);
    footer->addStretch(1);
    footer->addWidget(m_backButton);
    footer->addWidget(m_nextButton);
    layout->addLayout(footer);

    connect(m_skipButton, &QPushButton::clicked, this, &WelcomeWizard::skipForGood);
    connect(m_backButton, &QPushButton::clicked, this, &WelcomeWizard::back);
    connect(m_nextButton, &QPushButton::clicked, this, &WelcomeWizard::next);

    goTo(ClubStep);
    m_clubCombo->setFocus();
}

WelcomeWizard::~WelcomeWizard() = default;

QWidget *WelcomeWizard::buildClubPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionTitle(tr("Welchen Verein managst du?"), page));
    m_clubCombo = new QComboBox(page);
    m_clubCombo->setEditable(true);
    m_clubCombo->setInsertPolicy(QComboBox::NoInsert);
    m_clubCombo->completer()->setFilterMode(Qt::MatchContains);
    m_clubCombo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    layout->addWidget(m_clubCombo);
    layout->addWidget(caption(tr("Übersicht, Best XI, Kaufberatung und Trainingsplan rechnen mit "
                                 "diesem Verein. Oben steht der Verein mit den meisten "
                                 "importierten Spielern."),
                              page));
    layout->addSpacing(8);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(8);
    m_secondClubCombo = new QComboBox(page);
    m_fullNameEdit = new QLineEdit(page);
    m_fullNameEdit->setPlaceholderText(tr("voller Vereinsname (für den Header)"));
    m_stadiumEdit = new QLineEdit(page);
    m_stadiumEdit->setPlaceholderText(tr("Name des Stadions"));
    m_countryEdit = new QLineEdit(page);
    m_countryEdit->setMaxLength(3);
    m_countryEdit->setMaximumWidth(80);
    m_countryEdit->setPlaceholderText(QStringLiteral("ABC"));
    m_countryEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[A-Za-z]{0,3}")), m_countryEdit));
    form->addRow(tr("Zweitteam:"), m_secondClubCombo);
    form->addRow(tr("Voller Vereinsname (Anzeige):"), m_fullNameEdit);
    form->addRow(tr("Stadion:"), m_stadiumEdit);
    form->addRow(tr("Vereins-Land (3-Buchstaben-Code):"), m_countryEdit);

    m_logoPreview = new QLabel(page);
    m_logoPreview->setFixedSize(120, 48);
    m_logoPreview->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *chooseLogo = new QPushButton(tr("Logo wählen (PNG)…"), page);
    m_logoRemoveButton = new QPushButton(tr("Entfernen"), page);
    auto *logoRow = new QHBoxLayout;
    logoRow->setContentsMargins(0, 0, 0, 0);
    logoRow->addWidget(m_logoPreview);
    logoRow->addWidget(chooseLogo);
    logoRow->addWidget(m_logoRemoveButton);
    logoRow->addStretch(1);
    form->addRow(tr("Vereinslogo:"), logoRow);

    // Versions that read save games: following the save keeps the analyzer
    // current without any export (offered here so nobody has to find it).
    const QString versionId = m_context.fmVersionId();
    if (fmDataVersion(versionId).importFormats.contains(QStringLiteral("fm"))) {
        const AppConfig &config = m_context.config();
        m_followCheck = new QCheckBox(tr("live verfolgen"), page);
        m_followCheck->setToolTip(tr("Solange der Analyzer offen ist, liest er den Spielstand "
                                     "nach jedem Speichern im Spiel neu ein, auch nach einem "
                                     "Autosave. Schneller als ein Export."));
        m_followEdit = new QLineEdit(page);
        m_followEdit->setPlaceholderText(tr(".fm-Datei wählen"));
        m_followEdit->setText(QDir::toNativeSeparators(config.liveSavePath()));
        m_followCheck->setChecked(config.liveSaveEnabled());
        auto *browse = new QPushButton(tr("Durchsuchen …"), page);
        auto *followRow = new QHBoxLayout;
        followRow->setContentsMargins(0, 0, 0, 0);
        followRow->addWidget(m_followCheck);
        followRow->addWidget(m_followEdit, 1);
        followRow->addWidget(browse);
        form->addRow(tr("Spielstand im Spiel:"), followRow);
        connect(browse, &QPushButton::clicked, this, [this, versionId] {
            QString start = m_followEdit->text().trimmed();
            if (start.isEmpty())
                start = AppConfig::defaultSavesDir(versionId);
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Spielstand auswählen"), start,
                tr("Spielstände (*.fm);;Alle Dateien (*)"));
            if (path.isEmpty())
                return;
            m_followEdit->setText(QDir::toNativeSeparators(path));
            m_followCheck->setChecked(true);
        });
    }
    layout->addLayout(form);
    connect(chooseLogo, &QPushButton::clicked, this, &WelcomeWizard::chooseLogo);
    connect(m_logoRemoveButton, &QPushButton::clicked, this, &WelcomeWizard::removeLogo);

    layout->addSpacing(4);
    layout->addWidget(foldableHint(
        tr("Wofür brauche ich das?"),
        tr("Das Zweitteam (zum Beispiel die zweite Mannschaft oder U23) zählt zum eigenen "
           "Kader, etwa für Leihen und die Jugendelf. Vereinsname, Stadion und Logo erscheinen "
           "im Kopf des Fensters. Das Vereins-Land steuert den Inland-Filter beim Scouting und "
           "die Ausbildungsregeln der Kaderregistrierung. Der Code ist der dreistellige "
           "Nationen-Code aus dem Spiel."),
        page));
    layout->addStretch(1);

    fillClubs();
    m_fullNameEdit->setText(m_context.database().setting(QStringLiteral("full_club_name")));
    m_stadiumEdit->setText(m_context.database().setting(QStringLiteral("stadium_name")));
    m_countryEdit->setText(m_context.database().setting(QStringLiteral("club_country_code")));
    updateLogoPreview();
    connect(m_clubCombo, &QComboBox::currentTextChanged, this,
            &WelcomeWizard::suggestPresetForClub);
    return page;
}

QWidget *WelcomeWizard::buildPlayPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionTitle(tr("Lieblings-Taktiken"), page));
    auto *tacticForm = new QFormLayout;
    tacticForm->setHorizontalSpacing(16);
    m_tactic1Combo = new QComboBox(page);
    m_tactic1Combo->setMinimumWidth(260);
    m_tactic2Combo = new QComboBox(page);
    m_tactic2Combo->setMinimumWidth(260);
    QStringList tactics = m_context.definitions().tacticNames();
    std::sort(tactics.begin(), tactics.end());
    const auto fill = [&tactics](QComboBox *combo, const QString &current) {
        combo->addItem(QString());
        combo->addItems(tactics);
        if (!current.isEmpty())
            combo->setCurrentText(current);
    };
    fill(m_tactic1Combo, m_context.database().setting(QStringLiteral("favorite_tactic_1")));
    fill(m_tactic2Combo, m_context.database().setting(QStringLiteral("favorite_tactic_2")));
    if (m_tactic1Combo->currentText().isEmpty() && !tactics.isEmpty())
        m_tactic1Combo->setCurrentText(tactics.first());
    tacticForm->addRow(tr("Primäre Taktik:"), m_tactic1Combo);
    tacticForm->addRow(tr("Sekundäre Taktik:"), m_tactic2Combo);
    layout->addLayout(tacticForm);
    layout->addWidget(caption(tr("Werden auf Übersicht, Aufstellung, Kader und Transfers "
                                 "vorausgewählt. Die primäre bestimmt die Rollen auf der Seite "
                                 "Entwicklung. Eigene Taktiken legst du unter Taktiken an."),
                              page));

    // Two line-ups per tactic (FM27): weight of the in-possession role.
    m_ipShareBox = new QWidget(page);
    auto *ipLayout = new QVBoxLayout(m_ipShareBox);
    ipLayout->setContentsMargins(0, 12, 0, 0);
    ipLayout->setSpacing(8);
    ipLayout->addWidget(sectionTitle(tr("Mit Ball und ohne Ball"), m_ipShareBox));
    auto *ipForm = new QFormLayout;
    ipForm->setHorizontalSpacing(16);
    m_ipShareSpin = new QSpinBox(m_ipShareBox);
    m_ipShareSpin->setRange(0, 100);
    m_ipShareSpin->setSuffix(QStringLiteral(" %"));
    m_ipShareSpin->setValue(m_context.config().ipSharePercent());
    ipForm->addRow(tr("Anteil mit Ball:"), m_ipShareSpin);
    ipLayout->addLayout(ipForm);
    ipLayout->addWidget(foldableHint(
        tr("Was bedeutet der Anteil?"),
        tr("Jede Taktik hat eine Aufstellung mit Ball und eine ohne Ball. Der Wert eines "
           "Spielers ist dieser Anteil seiner Rolle mit Ball plus der Rest seiner Rolle ohne "
           "Ball. Bei 50 % zählen beide gleich, bei mehr zählt der Ballbesitz stärker."),
        m_ipShareBox));
    m_ipShareBox->setVisible(!m_context.definitions().twoPhaseTactics().isEmpty());
    layout->addWidget(m_ipShareBox);

    // Registration rules exist per FM version; hidden where none are modelled.
    const QString fmVersion = m_context.fmVersionId();
    const auto leagues = Registration::leagueRulesFor(fmVersion);
    const bool uefaAvailable = Registration::uefaRulesFor(fmVersion);
    m_registrationBox = new QWidget(page);
    auto *regLayout = new QVBoxLayout(m_registrationBox);
    regLayout->setContentsMargins(0, 12, 0, 0);
    regLayout->setSpacing(8);
    regLayout->addWidget(sectionTitle(tr("Kaderregistrierung"), m_registrationBox));
    auto *regForm = new QFormLayout;
    regForm->setHorizontalSpacing(16);
    m_leagueCombo = new QComboBox(m_registrationBox);
    m_leagueCombo->setMinimumWidth(260);
    m_leagueCombo->addItem(tr("Standard (keine Beschränkung)"),
                           Registration::leagueRulesKey(Registration::LeagueRules::None));
    for (const Registration::LeagueRules league : leagues)
        m_leagueCombo->addItem(Registration::leagueDisplayName(league),
                               Registration::leagueRulesKey(league));
    const Registration::Settings registration = m_context.registrationSettings();
    m_leagueCombo->setCurrentIndex(
        std::max(0, m_leagueCombo->findData(Registration::leagueRulesKey(registration.league))));
    m_uefaCheck = new QCheckBox(tr("Champions League / Europa League / Conference League"),
                                m_registrationBox);
    m_uefaCheck->setEnabled(uefaAvailable);
    m_uefaCheck->setChecked(registration.uefa);
    regForm->addRow(tr("Liga:"), m_leagueCombo);
    regForm->addRow(tr("Europapokal:"), m_uefaCheck);
    regLayout->addLayout(regForm);
    regLayout->addWidget(foldableHint(
        tr("Was bewirkt das?"),
        tr("Sobald ein Wettbewerb eine Meldeliste verlangt, markiert der Analyzer im Land und "
           "im Verein ausgebildete Spieler und bietet den Registrierungs-Assistenten an. "
           "'Standard' kennt keine Beschränkung, dann sind alle Spieler spielberechtigt."),
        m_registrationBox));
    m_registrationBox->setVisible(!leagues.isEmpty() || uefaAvailable);
    layout->addWidget(m_registrationBox);

    layout->addStretch(1);
    return page;
}

QWidget *WelcomeWizard::buildLookPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(sectionTitle(tr("Farben"), page));
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    m_presetCombo = new QComboBox(page);
    m_presetCombo->setMinimumWidth(280);
    m_presetCombo->setMaxVisibleItems(20);
    m_modeCombo = new QComboBox(page);
    m_modeCombo->addItem(tr("Nacht (dunkel)"), QStringLiteral("night"));
    m_modeCombo->addItem(tr("Tag (hell)"), QStringLiteral("day"));
    form->addRow(tr("Vereins-/Farbvorlage:"), m_presetCombo);
    form->addRow(tr("Modus:"), m_modeCombo);
    layout->addLayout(form);
    layout->addWidget(caption(tr("Die Vorlage färbt Kopfzeile, Knöpfe und Auswahl in den "
                                 "Vereinsfarben. Die Grundfarben bleiben neutral. Feinere "
                                 "Anpassungen findest du unter Einstellungen → Design."),
                              page));

    fillPresets();
    const auto themeSettings = m_context.config().themeSettings();
    m_modeCombo->setCurrentIndex(
        themeSettings.value(QStringLiteral("current_mode")) == QLatin1String("day") ? 1 : 0);
    connect(m_presetCombo, &QComboBox::activated, this, [this] { m_presetTouched = true; });

    layout->addSpacing(16);
    layout->addWidget(sectionTitle(tr("Was es sonst noch gibt"), page));
    layout->addWidget(foldableHint(
        tr("Weitere Einstellungen"),
        tr("In den Einstellungen findest du außerdem das Nationalteam (schaltet das "
           "National-Management frei), Porträts und Wappen aus dem Graphics-Ordner des Spiels "
           "und, unter Import, für FM27 das Live-Verfolgen eines Spielstands. Unter DWRS-Gewichte "
           "sowie Kader und Analyse passt du an, wie Rollen bewertet und Kader aufgebaut werden."),
        page));
    layout->addStretch(1);
    return page;
}

void WelcomeWizard::fillClubs()
{
    // Most represented clubs first: a squad export makes the user's club the top entry.
    QHash<QString, int> counts;
    for (const Player &player : m_context.store().players()) {
        if (!player.club.isEmpty())
            ++counts[player.club];
    }
    QStringList byCount = counts.keys();
    std::sort(byCount.begin(), byCount.end(), [&counts](const QString &a, const QString &b) {
        const int ca = counts.value(a);
        const int cb = counts.value(b);
        return ca != cb ? ca > cb : a.localeAwareCompare(b) < 0;
    });
    m_clubCombo->clear();
    m_clubCombo->addItems(byCount);
    const QString current = m_context.userClub();
    if (!current.isEmpty())
        m_clubCombo->setCurrentText(current);
    else
        m_clubCombo->setCurrentIndex(byCount.isEmpty() ? -1 : 0);

    QStringList alphabetical = byCount;
    std::sort(alphabetical.begin(), alphabetical.end(),
              [](const QString &a, const QString &b) { return a.localeAwareCompare(b) < 0; });
    m_secondClubCombo->clear();
    m_secondClubCombo->addItem(QString());
    m_secondClubCombo->addItems(alphabetical);
    QString second = m_context.secondTeamClub();
    if (second.isEmpty() && current.isEmpty()) {
        // A reserve side usually carries the first team's name ("… II", "… U23").
        const QString first = m_clubCombo->currentText();
        for (const QString &club : std::as_const(alphabetical)) {
            if (!first.isEmpty() && club != first && club.startsWith(first)) {
                second = club;
                break;
            }
        }
    }
    m_secondClubCombo->setCurrentText(second);
}

void WelcomeWizard::fillPresets()
{
    struct Entry {
        QString league;
        QString name;
        QString id;
    };
    QList<Entry> entries;
    for (const ClubTheme &preset : clubThemes())
        entries.append({preset.league, preset.displayName, preset.id});
    const QString customLeague = tr("Custom");
    for (const ColorTemplate &own : m_templates->templates())
        entries.append({own.league.isEmpty() ? customLeague : own.league,
                        tr("%1 (eigene)").arg(own.name), own.id});
    // Neutral first (empty league), leagues alphabetically, own ones last.
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) {
        const auto rank = [&](const Entry &e) {
            return e.league.isEmpty() ? 0 : (e.league == customLeague ? 2 : 1);
        };
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        const int league = QString::localeAwareCompare(a.league, b.league);
        if (league != 0)
            return league < 0;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });

    QString league;
    for (const Entry &entry : std::as_const(entries)) {
        if (entry.league != league) {
            league = entry.league;
            m_presetCombo->insertSeparator(m_presetCombo->count());
            m_presetCombo->addItem(QStringLiteral("— %1 —").arg(league));
            if (auto *model = qobject_cast<QStandardItemModel *>(m_presetCombo->model()))
                model->item(m_presetCombo->count() - 1)->setEnabled(false);
        }
        m_presetCombo->addItem(entry.name, entry.id);
    }

    // The save's current colors; "custom" ones stay as they are.
    const QString current = m_context.config().themeSettings().value(
        QStringLiteral("theme_preset"), QStringLiteral("neutral"));
    const int index = m_presetCombo->findData(current);
    if (index >= 0) {
        m_presetCombo->setCurrentIndex(index);
    } else {
        m_presetCombo->addItem(tr("Bisherige Farben behalten"), QStringLiteral("custom"));
        m_presetCombo->setCurrentIndex(m_presetCombo->count() - 1);
    }
    suggestPresetForClub();
}

void WelcomeWizard::suggestPresetForClub()
{
    // Only a suggestion: a choice the user made on the colors page wins.
    if (m_presetTouched || !m_presetCombo)
        return;
    const QString currentId = m_presetCombo->currentData().toString();
    if (currentId != QLatin1String("neutral") && !currentId.isEmpty()) {
        // An earlier suggestion may be replaced, a deliberate color choice not.
        bool wasSuggestion = false;
        for (const ClubTheme &preset : clubThemes())
            wasSuggestion = wasSuggestion || preset.id == currentId;
        if (!wasSuggestion)
            return;
    }
    const QString club = normalizedName(m_clubCombo->currentText());
    QString match = QStringLiteral("neutral");
    if (club.size() >= 3) {
        for (const ClubTheme &preset : clubThemes()) {
            const QString name = normalizedName(preset.displayName);
            if (preset.league.isEmpty() || name.size() < 3)
                continue;
            if (name == club || club.contains(name) || name.contains(club)) {
                match = preset.id;
                break;
            }
        }
    }
    const int index = m_presetCombo->findData(match);
    if (index >= 0)
        m_presetCombo->setCurrentIndex(index);
}

void WelcomeWizard::goTo(int step)
{
    m_pages->setCurrentIndex(step);
    for (int i = 0; i < m_stepLabels.size(); ++i) {
        QLabel *label = m_stepLabels.at(i);
        const int labelStep = i / 2; // number + name per step
        label->setProperty("state", labelStep < step    ? QStringLiteral("done")
                                    : labelStep == step ? QStringLiteral("current")
                                                        : QStringLiteral("todo"));
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
    m_backButton->setVisible(step > ClubStep);
    m_nextButton->setText(step == StepCount - 1 ? tr("Fertig") : tr("Weiter"));
    if (isVisible())
        QTimer::singleShot(0, this, [this] { growWindowToFit(this); });
}

void WelcomeWizard::next()
{
    const int step = m_pages->currentIndex();
    if (step < StepCount - 1)
        goTo(step + 1);
    else
        finish();
}

void WelcomeWizard::back()
{
    const int step = m_pages->currentIndex();
    if (step > 0)
        goTo(step - 1);
}

void WelcomeWizard::markDone()
{
    m_context.database().setSetting(doneSettingKey(), QStringLiteral("true"));
}

void WelcomeWizard::skipForGood()
{
    markDone();
    accept();
}

void WelcomeWizard::finish()
{
    AppConfig &config = m_context.config();
    AppConfig::Transaction transaction(config);
    Database &db = m_context.database();
    bool ok = transaction.isActive();
    const auto save = [&db, &ok](const QString &key, const QString &value) {
        if (ok)
            ok = value.isEmpty() ? db.removeSetting(key) : db.setSetting(key, value);
    };

    save(QStringLiteral("user_club"), m_clubCombo->currentText().trimmed());
    save(QStringLiteral("second_team_club"), m_secondClubCombo->currentText());
    save(QStringLiteral("full_club_name"), m_fullNameEdit->text().trimmed());
    save(QStringLiteral("stadium_name"), m_stadiumEdit->text().trimmed());
    save(QStringLiteral("club_country_code"), m_countryEdit->text().trimmed().toUpper());
    save(QStringLiteral("favorite_tactic_1"), m_tactic1Combo->currentText());
    save(QStringLiteral("favorite_tactic_2"), m_tactic2Combo->currentText());
    if (m_registrationBox->isVisibleTo(this)) {
        const QString leagueKey = m_leagueCombo->currentData().toString();
        save(QStringLiteral("registration_league"),
             leagueKey == Registration::leagueRulesKey(Registration::LeagueRules::None)
                 ? QString()
                 : leagueKey);
        save(QStringLiteral("registration_uefa"),
             m_uefaCheck->isChecked() ? QStringLiteral("true") : QString());
    }
    save(doneSettingKey(), QStringLiteral("true"));
    if (ok && m_followCheck) {
        const QString path = QDir::fromNativeSeparators(m_followEdit->text().trimmed());
        const bool follow = m_followCheck->isChecked() && !path.isEmpty();
        if (path != config.liveSavePath() || (follow && !config.liveSaveEnabled()))
            ok = config.setLiveSaveLastModified(0);
        if (ok)
            ok = config.setLiveSavePath(path) && config.setLiveSaveEnabled(follow);
    }
    if (ok && m_ipShareBox->isVisibleTo(this))
        ok = config.setIpSharePercent(m_ipShareSpin->value());

    auto themeSettings = config.themeSettings();
    const QString presetId = m_presetCombo->currentData().toString();
    if (!presetId.isEmpty() && presetId != QLatin1String("custom")) {
        QHash<QString, QString> colors = presetThemeSettings(presetId);
        if (const ColorTemplate *own = m_templates->find(presetId))
            colors = own->colors;
        for (auto it = colors.constBegin(); it != colors.constEnd(); ++it)
            themeSettings.insert(it.key(), it.value());
        themeSettings.insert(QStringLiteral("theme_preset"), presetId);
    }
    themeSettings.insert(QStringLiteral("current_mode"), m_modeCombo->currentData().toString());
    if (ok)
        ok = config.saveThemeSettings(themeSettings);

    QString error = ok ? QString() : db.errorString();
    if (!ok || !transaction.commit(&error)) {
        QMessageBox::critical(
            this, tr("Willkommen"),
            tr("Die Angaben konnten nicht gespeichert werden. Der Spielstand behält seinen "
               "bisherigen Stand.\n\n%1")
                .arg(error.isEmpty() ? config.lastError() : error));
        return;
    }

    m_context.reloadEngines();
    m_theme.reload();
    m_context.notifySettingsChanged();
    accept();
}

void WelcomeWizard::chooseLogo()
{
    const QString source = QFileDialog::getOpenFileName(
        this, tr("Vereinslogo wählen"), QString(),
        tr("Bilder (*.png *.jpg *.jpeg *.bmp);;Alle Dateien (*)"));
    if (source.isEmpty())
        return;
    QPixmap pixmap;
    if (!pixmap.load(source)) {
        QMessageBox::warning(this, tr("Vereinslogo"),
                             tr("Die Datei konnte nicht als Bild geladen werden."));
        return;
    }
    QString error;
    if (!m_context.setProfileImage(AppContext::ProfileImage::ClubLogo, pngBytes(pixmap), &error)) {
        QMessageBox::warning(this, tr("Vereinslogo"),
                             tr("Das Logo konnte nicht gespeichert werden.\n%1").arg(error));
        return;
    }
    updateLogoPreview();
    m_context.notifySettingsChanged(); // refreshes the header
}

void WelcomeWizard::removeLogo()
{
    QString error;
    if (!m_context.setProfileImage(AppContext::ProfileImage::ClubLogo, {}, &error))
        QMessageBox::warning(this, tr("Vereinslogo"),
                             tr("Das Logo konnte nicht entfernt werden.\n%1").arg(error));
    updateLogoPreview();
    m_context.notifySettingsChanged();
}

void WelcomeWizard::updateLogoPreview()
{
    QPixmap pixmap;
    if (pixmap.loadFromData(m_context.profileImage(AppContext::ProfileImage::ClubLogo))) {
        m_logoPreview->setPixmap(
            pixmap.scaled(m_logoPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_logoRemoveButton->setEnabled(true);
    } else {
        m_logoPreview->setPixmap(QPixmap());
        m_logoPreview->setText(tr("(kein Logo)"));
        m_logoPreview->setObjectName(QStringLiteral("kpiCaption"));
        m_logoRemoveButton->setEnabled(false);
    }
}

} // namespace fm
