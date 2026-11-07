#include "PlayerProfilePage.h"
#include "widgets/RolePhaseSwitch.h"

#include "../AppContext.h"
#include "../ImportRunner.h"
#include "../RecalcHelper.h"
#include "../widgets/Charts.h"
#include "../widgets/FootStrengthWidget.h"
#include "../widgets/PlayerSearchModel.h"
#include "PageHelpers.h"
#include "core/Constants.h"
#include "core/Development.h"
#include "core/DwrsEngine.h"
#include "core/Freshness.h"
#include "core/HtmlImporter.h"
#include "core/ImportHistory.h"
#include "core/RoleAnalysis.h"
#include "core/TalentEngine.h"
#include "core/TrainingAdvice.h"
#include "core/Utils.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include "widgets/FlowLayout.h"

#include <QScrollArea>
#include <QSet>
#include <QScrollBar>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <array>

namespace fm {

namespace {

constexpr int kPortraitSize = 96;
constexpr int kCrestSize = 24;
constexpr int kCrestStandInSize = 72; // crest in the portrait slot

enum Scope { MyClub = 0, SecondTeam = 1, Scouted = 2, All = 3, NationalSquad = 4 };

QString pill(const QString &label, const QString &value, const QString &color,
             const QString &textColor = QStringLiteral("white"))
{
    return QStringLiteral(
               "<span style='background-color:%1; color:%2; padding:3px 10px; "
               "border-radius:12px; font-size:9pt; white-space:nowrap;'>%3: %4</span>&nbsp;")
        .arg(color, textColor, label, value.toHtmlEscaped());
}

QString attrChip(const QString &name, int value)
{
    const CellStyle style = attributeCellStyle(value);
    return QStringLiteral(
               "<span style='background-color:%1; color:%2; padding:2px 8px; "
               "border-radius:6px; font-size:9pt; white-space:nowrap;'>%3: %4</span>&nbsp; ")
        .arg(style.background.name(), style.text.name(), name.toHtmlEscaped())
        .arg(value);
}

} // namespace

PlayerProfilePage::PlayerProfilePage(AppContext &context, ThemeManager &theme, QWidget *parent)
    : PageBase(context, parent)
    , m_theme(theme)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(10);
    scroll->setWidget(content);

    auto *heading = new QLabel(QStringLiteral("<h2 style='font-weight:600'>%1</h2>").arg(tr("Spieler-Profil")), content);
    layout->addWidget(heading);

    // --- Selection row ---
    auto *selectRow = new QHBoxLayout;
    m_scopeGroup = new QButtonGroup(this);
    const auto addScope = [&](const QString &label, int id, bool checked = false) {
        auto *radio = new QRadioButton(label, content);
        radio->setChecked(checked);
        m_scopeGroup->addButton(radio, id);
        selectRow->addWidget(radio);
    };
    addScope(tr("Mein Verein"), MyClub);
    addScope(tr("Zweitteam"), SecondTeam);
    addScope(tr("Gescoutete"), Scouted);
    addScope(tr("Nationalkader"), NationalSquad);
    addScope(tr("Alle Spieler"), All, true);
    selectRow->addSpacing(12);

    // Type-to-search instead of a giant dropdown: fast even with 80k players
    // (the completer model is built lazily on the first keystroke) and matches
    // accent-/umlaut-insensitively.
    m_searchEdit = new QLineEdit(content);
    m_searchEdit->setMinimumWidth(340);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setPlaceholderText(tr("Spieler suchen"));
    m_searchModel = new PlayerSearchModel(this);
    m_searchCompleter = new FoldingCompleter(m_searchModel, this);
    m_searchCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_searchCompleter->setFilterMode(Qt::MatchContains);
    m_searchCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_searchCompleter->setCompletionRole(PlayerSearchKeyRole);
    m_searchCompleter->setMaxVisibleItems(12);
    m_searchEdit->setCompleter(m_searchCompleter);
    selectRow->addWidget(m_searchEdit, 1);
    // Small squads (own club, second team, national squad): pick from a
    // menu grouped by position instead of typing a name.
    m_squadButton = new QToolButton(content);
    m_squadButton->setText(tr("Kader"));
    m_squadButton->setToolTip(tr("Spieler aus der Liste wählen, nach Mannschaftsteil sortiert"));
    m_squadButton->setPopupMode(QToolButton::InstantPopup);
    m_squadButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_squadButton->setCursor(Qt::PointingHandCursor);
    auto *squadMenu = new QMenu(m_squadButton);
    m_squadButton->setMenu(squadMenu);
    connect(squadMenu, &QMenu::aboutToShow, this, [this, squadMenu] { fillSquadMenu(squadMenu); });
    selectRow->insertWidget(selectRow->indexOf(m_searchEdit), m_squadButton);
    layout->addLayout(selectRow);

    // --- Header block ---
    // One surface: picture, identity and status on the left, the three
    // figures that change during a career (value, wage, rating) on the right,
    // each with its change since the previous in-game month when known.
    auto *header = new QFrame(content);
    header->setObjectName(QStringLiteral("profileHeader"));
    auto *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(20, 20, 20, 20);
    headerRow->setSpacing(20);
    m_nameLabel = new QLabel(header);
    m_nameLabel->setObjectName(QStringLiteral("profileName"));
    m_nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_vitalsLabel = new QLabel(header);
    m_vitalsLabel->setTextFormat(Qt::RichText);
    m_tagsLabel = new QLabel(header);
    m_tagsLabel->setTextFormat(Qt::RichText);
    m_tagsLabel->setWordWrap(true);
    m_tagsLabel->setObjectName(QStringLiteral("kpiCaption"));
    // Portrait (graphics folder); without one the club crest takes its place.
    m_portraitLabel = new QLabel(header);
    m_portraitLabel->setFixedSize(kPortraitSize, kPortraitSize);
    m_portraitLabel->setAlignment(Qt::AlignCenter);
    m_portraitLabel->setVisible(false);
    // Small crest in front of the club name.
    m_crestLabel = new QLabel(header);
    m_crestLabel->setFixedSize(kCrestSize, kCrestSize);
    m_crestLabel->setAlignment(Qt::AlignCenter);
    m_crestLabel->setVisible(false);
    auto *metaRow = new QHBoxLayout;
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(8);
    metaRow->addWidget(m_crestLabel, 0, Qt::AlignVCenter);
    metaRow->addWidget(m_vitalsLabel, 0, Qt::AlignVCenter);
    metaRow->addStretch(1);
    auto *headerText = new QVBoxLayout;
    headerText->setContentsMargins(0, 0, 0, 0);
    headerText->setSpacing(4);
    headerText->addWidget(m_nameLabel);
    headerText->addLayout(metaRow);
    headerText->addWidget(m_tagsLabel);
    m_footWidget = new FootStrengthWidget(header);
    headerText->addSpacing(8);
    headerText->addWidget(m_footWidget, 0, Qt::AlignLeft);
    headerText->addStretch(1);

    m_factsWidget = new QWidget(header);
    auto *facts = new QGridLayout(m_factsWidget);
    facts->setContentsMargins(0, 0, 0, 0);
    facts->setHorizontalSpacing(32);
    facts->setVerticalSpacing(2);
    const QStringList factCaptions = {tr("Marktwert"), tr("Gehalt"), tr("Ø-Note")};
    for (int i = 0; i < factCaptions.size(); ++i) {
        auto *caption = new QLabel(factCaptions.at(i), m_factsWidget);
        caption->setObjectName(QStringLiteral("kpiCaption"));
        auto *value = new QLabel(m_factsWidget);
        value->setObjectName(QStringLiteral("profileFact"));
        auto *change = new QLabel(m_factsWidget);
        change->setObjectName(QStringLiteral("kpiCaption"));
        facts->addWidget(caption, 0, i, Qt::AlignRight);
        facts->addWidget(value, 1, i, Qt::AlignRight);
        facts->addWidget(change, 2, i, Qt::AlignRight);
        m_factValues << value;
        m_factChanges << change;
    }
    m_factsNote = new QLabel(m_factsWidget);
    m_factsNote->setObjectName(QStringLiteral("kpiCaption"));
    facts->addWidget(m_factsNote, 3, 0, 1, factCaptions.size(), Qt::AlignRight);
    facts->setRowMinimumHeight(3, 24);
    facts->setRowStretch(4, 1);

    headerRow->addWidget(m_portraitLabel, 0, Qt::AlignTop);
    headerRow->addLayout(headerText, 1);
    headerRow->addWidget(m_factsWidget, 0, Qt::AlignTop);
    layout->addWidget(header);
    connect(&m_context.graphics(), &GraphicsLibrary::stateChanged, this, [this] {
        if (isVisible())
            showPlayer();
    });

    // --- Manual single-player update ---
    m_updateBox = new QGroupBox(tr("Diesen Spieler aus einer Exportdatei aktualisieren (HTML oder CSV)"),
                                content);
    m_updateBox->setCheckable(true);
    m_updateBox->setChecked(false);
    auto *updateInner = new QWidget(m_updateBox);
    auto *updateLayout = new QVBoxLayout(updateInner);
    auto *updateBoxLayout = new QVBoxLayout(m_updateBox);
    updateBoxLayout->addWidget(updateInner);
    connect(m_updateBox, &QGroupBox::toggled, updateInner, &QWidget::setVisible);
    updateInner->setVisible(false);
    auto *updateHint = new QLabel(
        tr("Lade einen FM-Export hoch, der NUR diesen Spieler enthält. Die Daten werden "
           "direkt auf dieses Profil geschrieben — auch wenn die UID in der Datei abweicht "
           "(z. B. fehlendes 'r-'-Präfix). Zugewiesene Rollen, Primärrolle und "
           "Transfer-/Leih-Einstellungen bleiben erhalten."),
        updateInner);
    updateHint->setWordWrap(true);
    updateHint->setObjectName(QStringLiteral("kpiCaption"));
    updateLayout->addWidget(updateHint);
    auto *fileRow = new QHBoxLayout;
    m_updateFileEdit = new QLineEdit(updateInner);
    m_updateFileEdit->setReadOnly(true);
    m_updateFileEdit->setPlaceholderText(tr("Keine Datei ausgewählt…"));
    auto *browse = new QPushButton(tr("Durchsuchen…"), updateInner);
    fileRow->addWidget(m_updateFileEdit, 1);
    fileRow->addWidget(browse);
    updateLayout->addLayout(fileRow);
    auto *confirmRow = new QHBoxLayout;
    m_updateConfirm = new QCheckBox(updateInner);
    m_updateButton = new QPushButton(tr("Spieler aktualisieren"), updateInner);
    m_updateButton->setEnabled(false);
    confirmRow->addWidget(m_updateConfirm, 1);
    confirmRow->addWidget(m_updateButton);
    updateLayout->addLayout(confirmRow);
    layout->addWidget(m_updateBox);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("FM-Export (HTML oder CSV) auswählen"), m_context.htmlExportDir(),
            tr("FM-Exporte (*.html *.htm *.csv);;Alle Dateien (*)"));
        if (!path.isEmpty())
            m_updateFileEdit->setText(path);
        m_updateButton->setEnabled(!m_updateFileEdit->text().isEmpty()
                                   && m_updateConfirm->isChecked());
    });
    connect(m_updateConfirm, &QCheckBox::toggled, this, [this](bool checked) {
        m_updateButton->setEnabled(checked && !m_updateFileEdit->text().isEmpty());
    });
    connect(m_updateButton, &QPushButton::clicked, this, &PlayerProfilePage::runManualUpdate);

    // --- Detail block (top roles, talent, analysis, chart) ---
    m_detailWidget = new QWidget(content);
    auto *detailLayout = new QVBoxLayout(m_detailWidget);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(10);

    auto *topRolesHeader = new QHBoxLayout;
    auto *topRolesTitle = new QLabel(tr("Rollen (beste zuerst)"), m_detailWidget);
    topRolesTitle->setObjectName(QStringLiteral("sectionTitle"));
    topRolesHeader->addWidget(topRolesTitle);
    topRolesHeader->addSpacing(16);
    // Legend for the outlined tiles: roles of the favorite tactic.
    m_tacticLegend = new QWidget(m_detailWidget);
    auto *legend = new QHBoxLayout(m_tacticLegend);
    legend->setContentsMargins(0, 0, 0, 0);
    legend->setSpacing(8);
    const auto addSwatch = [&](const QString &name, const QString &text) {
        auto *swatch = new QFrame(m_tacticLegend);
        swatch->setObjectName(name);
        swatch->setFixedSize(12, 12);
        legend->addWidget(swatch);
        auto *label = new QLabel(text, m_tacticLegend);
        label->setObjectName(QStringLiteral("kpiCaption"));
        legend->addWidget(label);
        legend->addSpacing(8);
    };
    m_tacticLegendLabel = new QLabel(m_tacticLegend);
    m_tacticLegendLabel->setObjectName(QStringLiteral("kpiCaption"));
    legend->addWidget(m_tacticLegendLabel);
    addSwatch(QStringLiteral("ipSwatch"), tr("mit Ball"));
    addSwatch(QStringLiteral("oopSwatch"), tr("ohne Ball"));
    topRolesHeader->addWidget(m_tacticLegend);
    topRolesHeader->addStretch(1);
    m_roleFilterCombo = new QComboBox(m_detailWidget);
    m_roleFilterCombo->addItem(tr("Alle Rollen"), QStringLiteral("all"));
    m_roleFilterCombo->addItem(tr("Nur mit Ball (IP)"), QStringLiteral("ip"));
    m_roleFilterCombo->addItem(tr("Nur ohne Ball (OOP)"), QStringLiteral("oop"));
    m_roleFilterCombo->setToolTip(tr("Welche Rollen als Kacheln gezeigt werden."));
    connect(m_roleFilterCombo, &QComboBox::currentIndexChanged, this,
            [this] { fillRoleTiles(); });
    topRolesHeader->addWidget(m_roleFilterCombo);
    detailLayout->addLayout(topRolesHeader);

    // Every assigned role as a compact tile; the tiles wrap into rows so all
    // of them stay readable without scrolling sideways.
    auto *tilesHost = new QWidget(m_detailWidget);
    m_topRolesLayout = new FlowLayout(tilesHost, 10, 10);
    detailLayout->addWidget(tilesHost);

    m_talentBox = new QGroupBox(tr("🌱 Talent-Projektion"), m_detailWidget);
    auto *talentLayout = new QVBoxLayout(m_talentBox);
    m_talentLabel = new QLabel(m_talentBox);
    m_talentLabel->setWordWrap(true);
    talentLayout->addWidget(m_talentLabel);
    detailLayout->addWidget(m_talentBox);

    m_trainingBox = new QGroupBox(tr("🎯 Trainingsempfehlung"), m_detailWidget);
    auto *trainingLayout = new QVBoxLayout(m_trainingBox);
    auto *trainingRoleRow = new QHBoxLayout;
    trainingRoleRow->addWidget(new QLabel(tr("Rolle:"), m_trainingBox));
    m_trainingRoleCombo = new QComboBox(m_trainingBox);
    m_trainingRoleCombo->setMinimumWidth(220);
    trainingRoleRow->addWidget(new RolePhaseSwitch(m_context.definitions(), m_trainingRoleCombo, m_trainingRoleCombo->parentWidget()));
    trainingRoleRow->addWidget(m_trainingRoleCombo);
    trainingRoleRow->addStretch(1);
    trainingLayout->addLayout(trainingRoleRow);
    m_trainingLabel = new QLabel(m_trainingBox);
    m_trainingLabel->setWordWrap(true);
    m_trainingLabel->setTextFormat(Qt::RichText);
    trainingLayout->addWidget(m_trainingLabel);
    detailLayout->addWidget(m_trainingBox);
    connect(m_trainingRoleCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        const Player *player = m_context.store().findByUid(m_currentUid);
        if (player)
            showTrainingAdvice(player); // preview only; role is set on the plan page
    });

    m_analysisTitle = new QLabel(m_detailWidget);
    m_analysisTitle->setObjectName(QStringLiteral("sectionTitle"));
    detailLayout->addWidget(m_analysisTitle);
    auto *analysisRow = new QHBoxLayout;
    auto *prosConsColumn = new QVBoxLayout;
    m_prosLabel = new QLabel(m_detailWidget);
    m_prosLabel->setWordWrap(true);
    m_consLabel = new QLabel(m_detailWidget);
    m_consLabel->setWordWrap(true);
    prosConsColumn->addWidget(m_prosLabel);
    prosConsColumn->addWidget(m_consLabel);
    prosConsColumn->addStretch(1);
    auto *keyColumn = new QVBoxLayout;
    auto *keyTitle = new QLabel(tr("Attribut-Übersicht"), m_detailWidget);
    keyTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_keyAttrsLabel = new QLabel(m_detailWidget);
    m_keyAttrsLabel->setTextFormat(Qt::RichText);
    m_keyAttrsLabel->setWordWrap(true);
    m_keyAttrsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    keyColumn->addWidget(keyTitle);
    keyColumn->addWidget(m_keyAttrsLabel);
    keyColumn->addStretch(1);
    analysisRow->addLayout(prosConsColumn, 1);
    analysisRow->addLayout(keyColumn, 1);
    detailLayout->addLayout(analysisRow);

    auto *chartTitle = new QLabel(tr("DWRS-Entwicklung"), m_detailWidget);
    chartTitle->setObjectName(QStringLiteral("sectionTitle"));
    detailLayout->addWidget(chartTitle);
    m_chart = new LineChartWidget(m_theme, m_detailWidget);
    // One player's own history: each point is a stored change of his ratings.
    m_chart->setAxisTitle(tr("Gespeicherte Veränderungen vor dem aktuellen Wert"));
    detailLayout->addWidget(m_chart);

    auto *historyTitle = new QLabel(tr("Verlauf nach Ingame-Datum"), m_detailWidget);
    historyTitle->setObjectName(QStringLiteral("sectionTitle"));
    detailLayout->addWidget(historyTitle);
    m_historyTable = new QTableWidget(0, 5, m_detailWidget);
    m_historyTable->setHorizontalHeaderLabels(
        {tr("Ingame-Datum"), tr("Ø-Note"), tr("Marktwert"), tr("Gehalt"),
         tr("Veränderte Attribute")});
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_historyTable->setWordWrap(true);
    detailLayout->addWidget(m_historyTable);
    m_historyEmpty = new QLabel(
        tr("Noch kein Verlauf. Bei jedem Einlesen eines FM27-Spielstands speichert der Analyzer pro "
           "Ingame-Monat einen Stand: für deinen Kader, das Zweitteam, die Shortlist, den "
           "Nationalkader und Spieler aus deinem Scouting, auf Wunsch für alle Spieler "
           "(Einstellungen → Verlauf und Datenfrische)."),
        m_detailWidget);
    m_historyEmpty->setObjectName(QStringLiteral("kpiCaption"));
    m_historyEmpty->setWordWrap(true);
    detailLayout->addWidget(m_historyEmpty);

    layout->addWidget(m_detailWidget);
    m_noRolesLabel = new QLabel(
        tr("Dieser Spieler hat noch keine bewerteten Rollen. Weise ihm unter "
           "Rollen & Taktiken → Rollen zuweisen Rollen zu."),
        content);
    m_noRolesLabel->hide();
    layout->addWidget(m_noRolesLabel);
    layout->addStretch(1);

    connect(m_scopeGroup, &QButtonGroup::idClicked, this, [this] {
        if (!m_updating)
            onScopeChanged();
    });
    // Build the (potentially huge) completer model only once the user starts
    // typing, then reuse it until the scope or the store changes.
    connect(m_searchEdit, &QLineEdit::textEdited, this, [this] {
        if (m_searchModelDirty)
            rebuildSearchModel();
    });
    connect(m_searchCompleter, QOverload<const QModelIndex &>::of(&QCompleter::activated), this,
            [this](const QModelIndex &index) {
                selectPlayer(index.data(Qt::UserRole).toString());
            });
}

void PlayerProfilePage::refresh()
{
    // The store may have changed underneath us; the completer model is rebuilt
    // lazily on the next keystroke.
    m_searchModelDirty = true;

    // One-shot navigation from the global player search.
    const QString target = m_context.pendingProfileUid();
    if (!target.isEmpty()) {
        m_context.setPendingProfileUid(QString());
        m_updating = true;
        m_scopeGroup->button(All)->setChecked(true);
        m_updating = false;
        updateScopeAvailability();
        if (const Player *player = m_context.store().findByUid(target))
            m_searchEdit->setText(QStringLiteral("%1 (%2)").arg(player->name, player->club));
        selectPlayer(target);
        return;
    }

    updateScopeAvailability();
    // Keep showing the current player across data refreshes if it still exists.
    if (m_currentUid.isEmpty() || !m_context.store().findByUid(m_currentUid))
        m_currentUid.clear();
    showPlayer();
}

void PlayerProfilePage::updateScopeAvailability()
{
    const int checkedScope = m_scopeGroup->checkedId();
    m_squadButton->setVisible(checkedScope == MyClub || checkedScope == SecondTeam
                              || checkedScope == NationalSquad);
    const QString secondClub = m_context.secondTeamClub();
    m_scopeGroup->button(SecondTeam)->setVisible(!secondClub.isEmpty());

    bool hasNationalSquad = false;
    for (const Player &player : m_context.store().players()) {
        if (player.inNationalSquad) {
            hasNationalSquad = true;
            break;
        }
    }
    m_scopeGroup->button(NationalSquad)->setVisible(hasNationalSquad);
    if (!hasNationalSquad && m_scopeGroup->checkedId() == NationalSquad) {
        m_updating = true;
        m_scopeGroup->button(All)->setChecked(true);
        m_updating = false;
        m_searchModelDirty = true;
    }
}

void PlayerProfilePage::onScopeChanged()
{
    updateScopeAvailability();
    m_searchModelDirty = true;
    m_searchEdit->clear();
    m_currentUid.clear();
    showPlayer();
}

void PlayerProfilePage::fillSquadMenu(QMenu *menu)
{
    menu->clear();
    const QString userClub = m_context.userClub();
    const QString secondClub = m_context.secondTeamClub();
    const int scope = m_scopeGroup->checkedId();

    // Grouped by the first position FM lists (FM lists them from the back
    // to the front, so a full-back who also plays wing-back sits in defence).
    enum Group { Keepers, Defence, Midfield, Attack, GroupCount };
    const QStringList groupNames = {tr("Torhüter"), tr("Abwehr"), tr("Mittelfeld"),
                                    tr("Angriff")};
    const auto groupOf = [](const Player &player) {
        if (player.isGoalkeeper())
            return Keepers;
        QString first = player.positionRaw.section(QLatin1Char(','), 0, 0).trimmed();
        first = first.section(QLatin1Char(' '), 0, 0).section(QLatin1Char('/'), 0, 0);
        if (first == QLatin1String("D") || first == QLatin1String("WB"))
            return Defence;
        if (first == QLatin1String("DM") || first == QLatin1String("M"))
            return Midfield;
        if (first == QLatin1String("AM") || first == QLatin1String("ST"))
            return Attack;
        return Midfield;
    };

    std::array<std::vector<const Player *>, GroupCount> groups;
    for (const Player &player : m_context.store().players()) {
        const bool include = (scope == MyClub && !userClub.isEmpty() && player.club == userClub)
                             || (scope == SecondTeam && !secondClub.isEmpty()
                                 && player.club == secondClub)
                             || (scope == NationalSquad && player.inNationalSquad);
        if (include)
            groups[groupOf(player)].push_back(&player);
    }
    bool any = false;
    for (int g = 0; g < GroupCount; ++g) {
        auto &pool = groups[g];
        if (pool.empty())
            continue;
        any = true;
        std::sort(pool.begin(), pool.end(), [](const Player *a, const Player *b) {
            return getLastName(a->name).localeAwareCompare(getLastName(b->name)) < 0;
        });
        QMenu *sub = menu->addMenu(QStringLiteral("%1 (%2)").arg(groupNames.at(g)).arg(pool.size()));
        for (const Player *player : pool) {
            QAction *action = sub->addAction(
                QStringLiteral("%1   ·   %2").arg(player->name, player->positionRaw));
            action->setCheckable(true);
            action->setChecked(player->uid == m_currentUid);
            const QString uid = player->uid;
            const QString label = QStringLiteral("%1 (%2)").arg(player->name, player->club);
            connect(action, &QAction::triggered, this, [this, uid, label] {
                m_searchEdit->setText(label);
                selectPlayer(uid);
            });
        }
    }
    if (!any)
        menu->addAction(tr("Keine Spieler in dieser Auswahl"))->setEnabled(false);
}

void PlayerProfilePage::rebuildSearchModel()
{
    const QString userClub = m_context.userClub();
    const QString secondClub = m_context.secondTeamClub();
    const int scope = m_scopeGroup->checkedId();

    std::vector<const Player *> pool;
    for (const Player &player : m_context.store().players()) {
        const bool isMine = !userClub.isEmpty() && player.club == userClub;
        const bool isSecond = !secondClub.isEmpty() && player.club == secondClub;
        if ((scope == MyClub && !isMine) || (scope == SecondTeam && !isSecond)
            || (scope == Scouted && (isMine || isSecond))
            || (scope == NationalSquad && !player.inNationalSquad))
            continue;
        pool.push_back(&player);
    }
    std::sort(pool.begin(), pool.end(), [](const Player *a, const Player *b) {
        return getLastName(a->name).localeAwareCompare(getLastName(b->name)) < 0;
    });

    // One model reset instead of one QStandardItem + dataChanged per player:
    // the completer re-filters on every source-model change, so item-wise
    // population froze the UI for tens of seconds on large saves.
    std::vector<PlayerSearchModel::Entry> entries;
    entries.reserve(pool.size());
    for (const Player *player : pool) {
        entries.push_back({QStringLiteral("%1 (%2)").arg(player->name, player->club),
                           player->uid,
                           foldForSearch(player->name + QLatin1Char(' ') + player->club)});
    }
    m_searchModel->setEntries(std::move(entries));
    m_searchModelDirty = false;
}

void PlayerProfilePage::restoreHistoryState(const QString &state)
{
    // Same one-shot handoff the player search uses; refresh() consumes it.
    if (!state.isEmpty())
        m_context.setPendingProfileUid(state);
}

void PlayerProfilePage::selectPlayer(const QString &uid)
{
    m_currentUid = uid;
    showPlayer();
}

void PlayerProfilePage::showPlayer()
{
    const Player *player = m_context.store().findByUid(m_currentUid);

    const bool hasPlayer = player != nullptr;
    m_updateBox->setVisible(hasPlayer);
    m_detailWidget->setVisible(hasPlayer);
    m_noRolesLabel->setVisible(false);
    if (!hasPlayer) {
        m_nameLabel->setText(tr("Kein Spieler ausgewählt"));
        m_factsWidget->setVisible(false);
        m_vitalsLabel->clear();
        m_tagsLabel->clear();
        m_footWidget->setFeet({}, {});
        m_portraitLabel->setVisible(false);
        m_crestLabel->setVisible(false);
        return;
    }

    // --- Header ---
    m_factsWidget->setVisible(true);
    m_nameLabel->setTextFormat(Qt::PlainText);
    m_nameLabel->setText(player->name);
    QStringList vitals;
    if (!player->club.isEmpty())
        vitals << player->club.toHtmlEscaped();
    if (player->age > 0)
        vitals << tr("%1 Jahre").arg(player->age);
    if (!player->positionRaw.isEmpty())
        vitals << player->positionRaw.toHtmlEscaped();
    m_vitalsLabel->setText(vitals.join(QStringLiteral(" &nbsp;·&nbsp; ")));

    // Portrait in the picture slot; without one the crest stands in there,
    // with one the crest sits small in front of the club name.
    const QPixmap portrait = m_context.graphics().portrait(player->uid, kPortraitSize);
    const bool graphics = m_context.graphics().isReady();
    const QPixmap crest = graphics && portrait.isNull()
                              ? m_context.clubCrest(player->club, kCrestStandInSize)
                              : graphics ? m_context.clubCrest(player->club, kCrestSize)
                                         : QPixmap();
    m_portraitLabel->setPixmap(portrait.isNull() ? crest : portrait);
    m_portraitLabel->setToolTip(portrait.isNull() ? player->club : QString());
    m_portraitLabel->setVisible(!portrait.isNull() || !crest.isNull());
    m_crestLabel->setPixmap(portrait.isNull() ? QPixmap() : crest);
    m_crestLabel->setToolTip(player->club);
    m_crestLabel->setVisible(!portrait.isNull() && !crest.isNull());

    // Status line: quiet text; a colored dot only where the color means
    // something (personality category, data freshness).
    const auto dot = [](const QString &color) {
        return QStringLiteral("<span style='color:%1;'>●</span>&nbsp;").arg(color);
    };
    const auto roleNames = m_context.definitions().roleDisplayMap();
    QStringList status;
    if (!player->agreedPlayingTime.isEmpty())
        status << player->agreedPlayingTime.toHtmlEscaped();
    if (!player->primaryRole.isEmpty())
        status << tr("Primärrolle %1")
                      .arg(roleNames.value(player->primaryRole, player->primaryRole).toHtmlEscaped());
    if (!player->personality.isEmpty()) {
        const CellStyle style = personalityCellStyle(
            m_context.definitions().personalityCategory(player->personality));
        status << (style.isValid() ? dot(style.background.name()) : QString())
                      + player->personality.toHtmlEscaped();
    }

    // --- Data freshness (see Freshness) ---
    const int counter = m_context.updateCounter();
    const int retirementAge =
        m_context.config().freshnessSetting(QStringLiteral("retirement_age"));
    const int staleAfter =
        m_context.config().freshnessSetting(QStringLiteral("stale_after_full_uploads"));
    const int missed = Freshness::uploadsSinceSeen(*player, counter);
    if (Freshness::isRetired(*player, counter, retirementAge, staleAfter,
                             m_context.userClub())) {
        status << dot(QStringLiteral("#c0392b"))
                      + tr("Retired (verpasste Komplett-Exporte: %1)").arg(missed);
    } else if (Freshness::isStale(*player, counter, staleAfter)) {
        status << dot(QStringLiteral("#d9822b"))
                      + tr("Daten veraltet (verpasste Komplett-Exporte: %1)").arg(missed);
    } else if (missed > 0) {
        status << dot(QStringLiteral("#8a8f98"))
                      + tr("verpasste Komplett-Exporte: %1").arg(missed);
    } else if (counter > 0) {
        status << dot(QStringLiteral("#2e9e5b")) + tr("Daten aktuell");
    }
    m_tagsLabel->setText(status.join(QStringLiteral(" &nbsp;·&nbsp; ")));
    fillFacts(player);
    m_footWidget->setFeet(player->leftFoot, player->rightFoot);
    m_updateConfirm->setText(
        tr("Ja, diese Datei ist sicher ein Update für %1.").arg(player->name));

    // --- Training advice: own players only (first or second team). ---
    const bool ownPlayer =
        (!m_context.userClub().isEmpty() && player->club == m_context.userClub())
        || (!m_context.secondTeamClub().isEmpty() && player->club == m_context.secondTeamClub());
    m_trainingBox->setVisible(ownPlayer);
    if (ownPlayer) {
        const QString tactic = m_context.database().setting(QStringLiteral("favorite_tactic_1"));
        const QStringList choices = tactic.isEmpty()
                                        ? QStringList()
                                        : trainingRoleChoices(m_context, *player, tactic);
        const QString effective = effectiveTrainingRole(m_context, *player, choices);
        const auto roleNames = m_context.definitions().roleDisplayMap();
        m_updating = true;
        m_trainingRoleCombo->clear();
        for (const QString &role : choices)
            m_trainingRoleCombo->addItem(roleNames.value(role, role), role);
        const int idx = m_trainingRoleCombo->findData(effective);
        m_trainingRoleCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_updating = false;
        showTrainingAdvice(player);
    }

    // --- Roles (all of them, best first) ---
    // role -> uid -> (absolute, normalized), the shape topRolesForPlayer expects.
    QHash<QString, QHash<QString, QPair<double, double>>> ratingsByRole;
    const LatestRatings &latest = m_context.latestRatings();
    for (const QString &role : player->assignedRoles) {
        const auto it = latest.constFind({player->id, role});
        if (it != latest.constEnd())
            ratingsByRole[role].insert(player->uid, it.value());
    }
    const auto topRoles = RoleAnalysis::topRolesForPlayer(
        m_context.definitions(), *player, ratingsByRole,
        static_cast<int>(player->assignedRoles.size()));

    m_roleTiles.clear();
    for (const auto &topRole : topRoles)
        m_roleTiles.append({topRole.role, topRole.roleName, topRole.normalized});
    m_roleFilterCombo->setVisible(m_context.definitions().hasRolePhases());
    fillRoleTiles();

    if (topRoles.empty()) {
        m_detailWidget->setVisible(false);
        m_noRolesLabel->setVisible(true);
        return;
    }

    // --- Talent projection (prospects only) ---
    const int outfielderCap = m_context.config().ageThreshold(QStringLiteral("outfielder"));
    const int goalkeeperCap = m_context.config().ageThreshold(QStringLiteral("goalkeeper"));
    const double ageCap = TalentEngine::ageCapForPlayer(*player, outfielderCap, goalkeeperCap);
    if (player->age > 0 && player->age <= ageCap) {
        const double score = TalentEngine::talentForPlayer(
            m_context.definitions(), *player, topRoles.front().normalized, ageCap);
        m_talentBox->setVisible(true);
        m_talentLabel->setText(
            tr("<b style='font-size:14pt;'>Talent-Score (U%1): %2</b><br/>"
               "Bester Rollen-DWRS <b>%3%</b> + Entwicklungs-Spielraum (%1−%4 Jahre) "
               "+ Mentalität (Ent %5 / Arb %6) + Persönlichkeit (%7).")
                .arg(static_cast<int>(ageCap))
                .arg(qRound(score))
                .arg(topRoles.front().normalized)
                .arg(player->age)
                .arg(static_cast<int>(player->attrMean(idx(Attr::Determination))))
                .arg(static_cast<int>(player->attrMean(idx(Attr::WorkRate))))
                .arg(player->personality.isEmpty() ? QStringLiteral("—")
                                                   : player->personality.toHtmlEscaped()));
    } else {
        m_talentBox->setVisible(false);
    }

    // --- Pros & cons + key attributes for the best role ---
    const QString bestRole = topRoles.front().role;
    m_analysisTitle->setText(tr("Analyse als %1").arg(topRoles.front().roleName));
    const RoleAnalysis::RoleReport report = RoleAnalysis::analyzePlayerForRole(
        m_context.definitions(), *player, bestRole, true, true);

    QString prosHtml = QStringLiteral("<b>✅ %1</b><ul>").arg(tr("Stärken"));
    for (const auto &pro : report.pros)
        prosHtml += QStringLiteral("<li>%1</li>")
                        .arg(RoleAnalysis::formatProLine(pro, report.roleName).toHtmlEscaped());
    prosHtml += QStringLiteral("</ul>");
    m_prosLabel->setText(report.pros.empty()
                             ? QStringLiteral("<b>✅ %1</b><p>%2</p>")
                                   .arg(tr("Stärken"), tr("Keine herausragenden Stärken."))
                             : prosHtml);
    QString consHtml = QStringLiteral("<b>⚠️ %1</b><ul>").arg(tr("Schwächen"));
    for (const auto &con : report.cons)
        consHtml += QStringLiteral("<li>%1</li>")
                        .arg(RoleAnalysis::formatConLine(con, report.roleName).toHtmlEscaped());
    consHtml += QStringLiteral("</ul>");
    m_consLabel->setText(report.cons.empty()
                             ? QStringLiteral("<b>⚠️ %1</b><p>%2</p>")
                                   .arg(tr("Schwächen"), tr("Keine nennenswerten Schwächen."))
                             : consHtml);

    const RoleWeights weights = m_context.definitions().roleWeights(bestRole);
    QString chips;
    const auto renderChips = [&](const QStringList &attrs, const QString &title) {
        if (attrs.isEmpty())
            return;
        chips += QStringLiteral("<p><b>%1</b><br/>").arg(title);
        for (const QString &attr : attrs) {
            const int attrIndex = attrIndexByName(attr);
            if (attrIndex < 0)
                continue;
            const int value = qRound(player->attrMean(attrIndex));
            if (value > 0)
                chips += attrChip(attr, value);
        }
        chips += QStringLiteral("</p>");
    };
    renderChips(weights.key, tr("Schlüssel-Attribute"));
    renderChips(weights.preferable, tr("Bevorzugte Attribute"));
    m_keyAttrsLabel->setText(chips.isEmpty() ? tr("Keine Attribut-Definitionen für diese Rolle.")
                                             : chips);

    // --- Development chart for the top 3 roles (x = his upload snapshots) ---
    QStringList chartRoles;
    for (int i = 0; i < static_cast<int>(topRoles.size()) && i < 3; ++i)
        chartRoles << topRoles[i].role;
    std::vector<DwrsEntry> history = m_context.database().dwrsHistory({player->id});
    std::erase_if(history,
                  [&chartRoles](const DwrsEntry &entry) { return !chartRoles.contains(entry.role); });
    const Development::Timeline timeline =
        Development::buildTimeline(history, m_context.database().dwrsRecalcTimestamps());
    QList<LineChartWidget::Series> chartSeries;
    QList<const Development::Track *> chartTracks;
    for (int i = 0; i < chartRoles.size(); ++i) {
        const Development::Track *track = timeline.track(player->id, chartRoles.at(i));
        if (!track)
            continue;
        chartTracks << track;
        LineChartWidget::Series series;
        series.name = topRoles[i].roleName;
        series.emphasized = i == 0;
        for (int s = track->firstSession; s < timeline.size(); ++s)
            series.points.append({double(s), track->adjusted.at(s)});
        chartSeries.append(series);
    }
    const QStringList snapshotLabels = developmentSnapshotLabels(m_context, timeline);
    const QList<int> markers = Development::recalcMarkers(chartTracks, 0);
    if (chartSeries.isEmpty())
        m_chart->clearChart(tr("Noch keine historischen DWRS-Daten. Importiere über die Zeit "
                               "weitere Snapshots, um die Entwicklung zu sehen."));
    else
        m_chart->setSeries(std::move(chartSeries), snapshotLabels,
                           LineChartWidget::ValueMode::Percent, markers);
    fillHistory(player);
}

void PlayerProfilePage::fillFacts(const Player *player)
{
    // Current figures; the change compares the two newest in-game snapshots
    // (only changes are stored, so the previous one differs by definition).
    const std::vector<ImportHistory::Snapshot> snapshots =
        m_context.database().snapshots(player->id);
    const ImportHistory::Snapshot *last = snapshots.empty() ? nullptr : &snapshots.back();
    const ImportHistory::Snapshot *before =
        snapshots.size() >= 2 ? &snapshots[snapshots.size() - 2] : nullptr;
    const QString dash = QStringLiteral("–");
    m_factValues[0]->setText(player->transferValueRaw.isEmpty() ? dash : player->transferValueRaw);
    m_factValues[1]->setText(player->wageRaw.isEmpty() ? dash : player->wageRaw);
    m_factValues[2]->setText(player->averageRating > 0
                                 ? QLocale().toString(player->averageRating, 'f', 2)
                                 : dash);
    for (QLabel *change : std::as_const(m_factChanges))
        change->clear();
    const auto since = [&](const ImportHistory::Snapshot &s) {
        return s.condensed ? ImportHistory::seasonLabel(ImportHistory::seasonOfPeriod(s.period),
                                                        ImportHistory::seasonStartOf(
                                                            m_context.database()))
                           : QLocale().toString(s.gameDate, QLocale::ShortFormat);
    };
    // Same style as the game's figures ("€1.8M", "€450K").
    const auto moneyText = [](double euros) {
        return euros >= 1e6 ? QStringLiteral("€%1M").arg(euros / 1e6, 0, 'f', 1)
                            : QStringLiteral("€%1K").arg(std::lround(euros / 1e3));
    };
    const auto signedText = [](double delta, const QString &number) {
        return (delta > 0 ? QStringLiteral("+") : QStringLiteral("−")) + number;
    };
    if (last && before) {
        const double valueDelta = last->transferValue - before->transferValue;
        if (std::abs(valueDelta) >= 1000 && last->transferValue < kUnbuyableValue
            && before->transferValue < kUnbuyableValue)
            m_factChanges[0]->setText(
                tr("%1 seit %2")
                    .arg(signedText(valueDelta, moneyText(std::abs(valueDelta))),
                         since(*before)));
        if (last->wage != before->wage && !before->wage.isEmpty())
            m_factChanges[1]->setText(tr("vorher %1").arg(before->wage));
        const double ratingDelta = last->averageRating - before->averageRating;
        if (last->averageRating > 0 && before->averageRating > 0 && std::abs(ratingDelta) >= 0.005)
            m_factChanges[2]->setText(
                tr("%1 seit %2")
                    .arg(signedText(ratingDelta, QLocale().toString(std::abs(ratingDelta), 'f', 2)),
                         since(*before)));
    }
    m_factsNote->setText(last ? tr("Ingame-Stand %1").arg(since(*last)) : QString());
}

void PlayerProfilePage::fillHistory(const Player *player)
{
    // Newest first; each row lists the attributes that moved since the
    // previous (older) snapshot.
    const std::vector<ImportHistory::Snapshot> snapshots =
        m_context.database().snapshots(player->id);
    m_historyTable->setRowCount(0);
    m_historyTable->setVisible(!snapshots.empty());
    m_historyEmpty->setVisible(snapshots.empty());
    if (snapshots.empty())
        return;
    const auto mean = [](const ImportHistory::Snapshot &s, int i) {
        return (s.attrLo[size_t(i)] + s.attrHi[size_t(i)]) / 2.0;
    };
    const ImportHistory::SeasonStart seasonStart =
        ImportHistory::seasonStartOf(m_context.database());
    const int rows = static_cast<int>(snapshots.size());
    m_historyTable->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        const ImportHistory::Snapshot &s = snapshots[size_t(i)];
        const int row = rows - 1 - i;
        const QString when =
            s.condensed ? tr("Saison %1 (Ende)")
                              .arg(ImportHistory::seasonLabel(ImportHistory::seasonOfPeriod(s.period),
                                                              seasonStart))
                        : QLocale().toString(s.gameDate, QLocale::ShortFormat);
        QStringList moved;
        if (i > 0) {
            const ImportHistory::Snapshot &before = snapshots[size_t(i - 1)];
            for (int a = 0; a < kAttrCount; ++a) {
                if (!s.attrLo[size_t(a)] || !before.attrLo[size_t(a)])
                    continue;
                const double delta = mean(s, a) - mean(before, a);
                if (std::abs(delta) >= 0.5)
                    moved << QStringLiteral("%1 %2%3")
                                 .arg(attrNames()[a], delta > 0 ? QStringLiteral("+")
                                                                : QStringLiteral("−"))
                                 .arg(QLocale().toString(std::abs(delta), 'g', 3));
            }
        }
        const QStringList cells = {
            when,
            s.averageRating > 0 ? QLocale().toString(s.averageRating, 'f', 2) : QStringLiteral("–"),
            s.transferValueText.isEmpty() ? QStringLiteral("–") : s.transferValueText,
            s.wage.isEmpty() ? QStringLiteral("–") : s.wage,
            i == 0 ? tr("erster Stand") : (moved.isEmpty() ? QStringLiteral("–")
                                                            : moved.join(QStringLiteral(" · "))),
        };
        for (int c = 0; c < cells.size(); ++c)
            m_historyTable->setItem(row, c, new QTableWidgetItem(cells.at(c)));
    }
    fitTableToContents(m_historyTable, false, 520);
}

void PlayerProfilePage::fillRoleTiles()
{
    while (QLayoutItem *item = m_topRolesLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    const QString filter = m_context.definitions().hasRolePhases()
                               ? m_roleFilterCombo->currentData().toString()
                               : QStringLiteral("all");
    // Roles of the favorite tactic get an outline per phase.
    const QString tactic =
        m_context.database().setting(QStringLiteral("favorite_tactic_1"));
    const Definitions &definitions = m_context.definitions();
    QSet<QString> tacticRoles;
    if (!tactic.isEmpty() && definitions.tacticNames().contains(tactic)) {
        for (const QString &role : definitions.tacticRoleList(tactic))
            tacticRoles.insert(role);
    }
    bool anyMarked = false;
    bool first = true;
    for (const RoleTile &entry : std::as_const(m_roleTiles)) {
        if (filter != QLatin1String("all")
            && m_context.definitions().rolePhase(entry.role) != filter)
            continue;
        auto *tile = new QFrame(m_topRolesLayout->parentWidget());
        tile->setObjectName(QStringLiteral("kpiTile"));
        tile->setFixedWidth(168);
        if (tacticRoles.contains(entry.role)) {
            const bool oop = definitions.hasRolePhases()
                             && definitions.rolePhase(entry.role) == QLatin1String("oop");
            tile->setProperty("tacticPhase", oop ? QStringLiteral("oop") : QStringLiteral("ip"));
            anyMarked = true;
        }
        auto *tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(12, 8, 12, 8);
        tileLayout->setSpacing(2);
        auto *value = new QLabel(QStringLiteral("%1%").arg(entry.value), tile);
        value->setObjectName(QStringLiteral("kpiValue"));
        value->setStyleSheet(QStringLiteral("font-size: 15pt;"));
        auto *caption = new QLabel(
            (first && entry.role == m_roleTiles.front().role ? QStringLiteral("🥇 ")
                                                            : QString())
                + entry.name,
            tile);
        caption->setObjectName(QStringLiteral("kpiCaption"));
        caption->setWordWrap(true);
        // Two caption lines for every tile, so the rows line up evenly.
        caption->setMinimumHeight(2 * caption->fontMetrics().lineSpacing());
        caption->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        tile->setToolTip(m_context.definitions().roleLabel(entry.role));
        tileLayout->addWidget(value);
        tileLayout->addWidget(caption);
        m_topRolesLayout->addWidget(tile);
        first = false;
    }
    m_topRolesLayout->invalidate();
    m_tacticLegend->setVisible(anyMarked);
    m_tacticLegendLabel->setText(tr("Rahmen = Rolle in „%1“:").arg(tactic));
    // Without phases (FM24) a single color says "in the tactic".
    m_tacticLegend->findChild<QFrame *>(QStringLiteral("oopSwatch"))
        ->setVisible(definitions.hasRolePhases());
    for (QLabel *label : m_tacticLegend->findChildren<QLabel *>()) {
        if (label->text() == tr("ohne Ball"))
            label->setVisible(definitions.hasRolePhases());
        else if (label->text() == tr("mit Ball"))
            label->setVisible(definitions.hasRolePhases());
    }
}

void PlayerProfilePage::runManualUpdate()
{
    const QString uid = m_currentUid;
    const Player *player = m_context.store().findByUid(uid);
    const QString filePath = m_updateFileEdit->text();
    if (!player || filePath.isEmpty())
        return;

    ImportTable table;
    ImportError error;
    if (!readImportTable(filePath, m_context.fmVersionId(), &table, &error)) {
        QMessageBox::critical(this, tr("Spieler aktualisieren"), importErrorText(error));
        return;
    }
    const QString fileName = HtmlImporter::forceUpdateSinglePlayer(
        table, m_context.database(), m_context.store().players(), uid, &error,
        m_context.fmVersionId());
    if (error.code != ImportErrorCode::None) {
        QMessageBox::critical(this, tr("Spieler aktualisieren"), importErrorText(error));
        return;
    }

    const QString playerName = player->name;
    const int playerId = player->id;
    m_updateFileEdit->clear();
    m_updateConfirm->setChecked(false);
    m_context.refreshPlayers({playerId});

    recalcDwrsFor(m_context, this, {uid}, [this, playerName, fileName](const QString &recalcError) {
        if (!recalcError.isEmpty()) {
            QMessageBox::critical(this, tr("Spieler aktualisieren"), recalcError);
            return;
        }
        QString message = tr("✅ %1 wurde aus der Datei aktualisiert.").arg(playerName);
        if (!fileName.isEmpty() && fileName != playerName)
            message += tr(" (Name in der Datei: '%1'.)").arg(fileName);
        QMessageBox::information(this, tr("Spieler aktualisieren"), message);
    }, RatingsOrigin::Import);
}

void PlayerProfilePage::showTrainingAdvice(const Player *player)
{
    if (!player) {
        m_trainingBox->setVisible(false);
        return;
    }
    m_trainingBox->setVisible(true);

    const QString role = m_trainingRoleCombo->currentData().toString();
    if (role.isEmpty()) {
        m_trainingLabel->setText(
            tr("Keine trainierbare Rolle aus <i>Positionen ∩ Lieblingstaktik</i>. "
               "Lege in den Einstellungen eine Lieblings-Taktik fest."));
        return;
    }

    const TrainingAdvice::FocusAdvice advice = TrainingAdvice::adviseFocusesForRole(
        *player, role, m_context.dwrsEngine(), trainingWindows(m_context));
    if (!advice.valid || advice.focus.empty()) {
        m_trainingLabel->setText(
            tr("Für diese Rolle gibt es aktuell keine sinnvolle Trainingsmethode "
               "(Attribute bereits hoch oder altersbedingt gesperrt)."));
        return;
    }

    QString html = QStringLiteral("<b>%1</b><ol>")
                       .arg(tr("Trainingsmethoden in dieser Reihenfolge:"));
    const int limit = std::min<int>(5, static_cast<int>(advice.focus.size()));
    for (int i = 0; i < limit; ++i) {
        const TrainingAdvice::FocusRecommendation &f = advice.focus[i];
        QString note;
        if (f.allMental)
            note = tr("mental, lebenslang trainierbar");
        else if (f.anyLimited)
            note = tr("altersbedingt nur noch begrenzt");
        else
            note = tr("noch voll trainierbar");
        html += QStringLiteral("<li><b>%1</b> — %2 <span style='opacity:0.75;'>(%3)</span></li>")
                    .arg(f.focus.toHtmlEscaped(),
                         f.attributes.join(QStringLiteral(", ")).toHtmlEscaped(), note);
    }
    html += QStringLiteral("</ol>");

    if (advice.formLimited) {
        html += tr("⚠ Ø-Note unter 7.0 — entwickelt sich aktuell nur langsam.");
    }
    m_trainingLabel->setText(html);
}

} // namespace fm
