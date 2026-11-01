#include "BestXiPage.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../theming/ThemeManager.h"
#include "../widgets/NumericTableItem.h"
#include "../widgets/TacticPitchWidget.h"
#include "PageHelpers.h"
#include "SigningAdvisorTab.h"
#include "core/Registration.h"
#include "core/SquadBuilder.h"
#include "core/TalentEngine.h"
#include "core/Utils.h"

#include <QCheckBox>
#include <QFrame>
#include <QPushButton>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QComboBox>
#include <QCursor>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMenu>
#include <QLabel>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fm {

namespace {

// Value behind each competition combo entry. An empty combo (no registration
// rules active) reads as 0: no competition, hence no restriction.
constexpr int kLeague = 1; // restricted only if the league has a squad list
constexpr int kUefa = 2;   // list A + B
constexpr int kCup = 3;    // domestic cups: no registration

} // namespace

BestXiPage::BestXiPage(AppContext &context, ThemeManager &theme, QWidget *parent)
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

    m_heading = new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Best XI")), content);
    layout->addWidget(m_heading);

    auto *tacticRow = new QHBoxLayout;
    tacticRow->addWidget(new QLabel(tr("Taktik:"), content));
    m_tacticCombo = new QComboBox(content);
    m_tacticCombo->setMinimumWidth(260);
    tacticRow->addWidget(m_tacticCombo);
    m_competitionLabel = new QLabel(tr("Wettbewerb:"), content);
    tacticRow->addSpacing(16);
    tacticRow->addWidget(m_competitionLabel);
    m_competitionCombo = new QComboBox(content);
    m_competitionCombo->setMinimumWidth(260);
    m_competitionCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_competitionCombo->setToolTip(
        tr("Beschränkt die erste Mannschaft auf die Spieler, die im Wettbewerb spielberechtigt "
           "sind: die gespeicherte Meldeliste (Transfers → Meldeliste) plus alle, die ohne "
           "Platz spielen dürfen. Im Pokal dürfen alle spielen."));
    tacticRow->addWidget(m_competitionCombo);
    tacticRow->addStretch(1);
    // How the line-up is computed: one click away instead of always on screen.
    m_hintToggle = new QToolButton(content);
    m_hintToggle->setText(tr("Beweisbar optimal"));
    m_hintToggle->setToolTip(tr("Erklärung des Rechenverfahrens ein- oder ausblenden"));
    m_hintToggle->setCheckable(true);
    m_hintToggle->setArrowType(Qt::RightArrow);
    m_hintToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tacticRow->addWidget(m_hintToggle);
    layout->addLayout(tacticRow);

    m_clubNotice = new QLabel(tr("Bitte wähle zuerst deinen Verein (Übersicht oder Einstellungen)."),
                              content);
    m_clubNotice->setWordWrap(true);
    m_clubNotice->setVisible(false);
    layout->addWidget(m_clubNotice);

    m_registrationNote = new QLabel(content);
    m_registrationNote->setWordWrap(true);
    layout->addWidget(m_registrationNote);

    m_hint = new QLabel(algorithmHint(), content);
    m_hint->setWordWrap(true);
    m_hint->setObjectName(QStringLiteral("kpiCaption"));
    m_hint->setVisible(false);
    layout->addWidget(m_hint);
    connect(m_hintToggle, &QToolButton::toggled, this, [this](bool shown) {
        m_hint->setVisible(shown);
        m_hintToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
    });

    // Long-term injuries: who is missing and who takes over.
    m_injuryBox = new QGroupBox(content);
    m_injuryBox->setVisible(false);
    {
        auto *injuryLayout = new QVBoxLayout(m_injuryBox);
        auto *noteRow = new QHBoxLayout;
        m_injuryNote = new QLabel(m_injuryBox);
        m_injuryNote->setWordWrap(true);
        m_injuryNote->setTextFormat(Qt::RichText);
        m_injuryNote->setToolTip(tr("Klick auf einen Namen öffnet das Spieler-Menü "
                                    "(z. B. um ihn wieder als fit zu markieren)."));
        noteRow->addWidget(m_injuryNote, 1);
        m_planInjuredCheck = new QCheckBox(tr("Trotzdem einplanen"), m_injuryBox);
        m_planInjuredCheck->setToolTip(
            tr("Zeigt die Aufstellung, als wären alle fit. Die Tabelle bleibt der Vergleich "
               "mit allen fit gegen ohne die Verletzten."));
        noteRow->addWidget(m_planInjuredCheck, 0, Qt::AlignTop);
        injuryLayout->addLayout(noteRow);
        m_injuryTable = new QTableWidget(m_injuryBox);
        m_injuryTable->setColumnCount(8);
        m_injuryTable->setHorizontalHeaderLabels({tr("Team"), tr("Slot"), tr("Rolle"),
                                                  tr("Alle fit"), tr("DWRS"),
                                                  tr("Ohne Verletzte"), tr("DWRS"), tr("Δ")});
        m_injuryTable->verticalHeader()->setVisible(false);
        m_injuryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_injuryTable->setAlternatingRowColors(true);
        m_injuryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        // Columns follow their content even when the page is polished later.
        m_injuryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_injuryTable->horizontalHeader()->setStretchLastSection(true);
        m_injuryTable->setWordWrap(false);
        m_injuryTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        injuryLayout->addWidget(m_injuryTable);
    }
    layout->addWidget(m_injuryBox);
    connect(m_planInjuredCheck, &QCheckBox::toggled, this, [this] {
        if (!m_updating)
            rebuild();
    });
    connect(m_injuryNote, &QLabel::linkActivated, this, [this](const QString &uid) {
        PlayerActions::showContextMenu(m_context, this, uid, QCursor::pos());
    });
    // Both player columns carry their own uid.
    PlayerActions::attachToTableWidget(m_context, m_injuryTable, -1);

    m_tabs = new QTabWidget(content);

    // --- Tab 1: first team ---
    {
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        // Starting XI / B-team: one click apart, both line-ups (FM27: with
        // ball left, without ball right, like the game) side by side.
        auto *switchRow = new QHBoxLayout;
        auto *segmented = new QFrame(tab);
        segmented->setObjectName(QStringLiteral("segmented"));
        auto *segLayout = new QHBoxLayout(segmented);
        segLayout->setContentsMargins(3, 3, 3, 3);
        segLayout->setSpacing(3);
        m_xiButton = new QPushButton(tr("Startelf"), segmented);
        m_bButton = new QPushButton(tr("B-Team"), segmented);
        auto *teamGroup = new QButtonGroup(tab);
        for (QPushButton *button : {m_xiButton, m_bButton}) {
            button->setCheckable(true);
            button->setObjectName(QStringLiteral("segmentButton"));
            button->setCursor(Qt::PointingHandCursor);
            button->setFocusPolicy(Qt::NoFocus);
            button->setMinimumWidth(110);
            teamGroup->addButton(button);
            segLayout->addWidget(button);
        }
        m_xiButton->setChecked(true);
        switchRow->addWidget(segmented);
        switchRow->addStretch(1);
        tabLayout->addLayout(switchRow);

        m_phaseNote = new QLabel(tab);
        m_phaseNote->setWordWrap(true);
        m_phaseNote->setObjectName(QStringLiteral("kpiCaption"));
        m_phaseNote->setVisible(false);
        tabLayout->addWidget(m_phaseNote);

        m_bridgeBar = new QFrame(tab);
        m_bridgeBar->setObjectName(QStringLiteral("bridgeBar"));
        auto *bridgeLayout = new QHBoxLayout(m_bridgeBar);
        bridgeLayout->setContentsMargins(0, 0, 0, 0);
        m_bridgeNote = new QLabel(m_bridgeBar);
        m_bridgeNote->setWordWrap(true);
        auto *bridgeReset = new QPushButton(tr("Vorschlag wiederherstellen"), m_bridgeBar);
        bridgeReset->setCursor(Qt::PointingHandCursor);
        connect(bridgeReset, &QPushButton::clicked, this, [this] {
            saveBridges(m_bridge.tactic, {});
            rebuild();
        });
        bridgeLayout->addWidget(m_bridgeNote, 1);
        bridgeLayout->addWidget(bridgeReset);
        m_bridgeBar->setVisible(false);
        tabLayout->addWidget(m_bridgeBar);

        m_teamStack = new QStackedWidget(tab);
        const auto makePage = [this](TacticPitchWidget *&ipPitch, TacticPitchWidget *&oopPitch,
                                     QLabel *&ipTitle, QLabel *&oopTitle) {
            auto *page = new QWidget;
            auto *row = new QHBoxLayout(page);
            row->setContentsMargins(0, 0, 0, 0);
            auto *ipColumn = new QVBoxLayout;
            ipTitle = new QLabel(tr("Mit Ball"), page);
            ipTitle->setObjectName(QStringLiteral("sectionTitle"));
            ipTitle->setAlignment(Qt::AlignHCenter);
            ipPitch = new TacticPitchWidget(m_theme, page);
            ipColumn->addWidget(ipTitle);
            ipColumn->addWidget(ipPitch, 0, Qt::AlignTop | Qt::AlignHCenter);
            auto *oopColumn = new QVBoxLayout;
            oopTitle = new QLabel(tr("Ohne Ball"), page);
            oopTitle->setObjectName(QStringLiteral("sectionTitle"));
            oopTitle->setAlignment(Qt::AlignHCenter);
            oopPitch = new TacticPitchWidget(m_theme, page);
            oopColumn->addWidget(oopTitle);
            oopColumn->addWidget(oopPitch, 0, Qt::AlignTop | Qt::AlignHCenter);
            row->addLayout(ipColumn, 1);
            row->addLayout(oopColumn, 1);
            return page;
        };
        m_teamStack->addWidget(makePage(m_xiIpPitch, m_xiPitch, m_xiIpTitle, m_xiTitle));
        m_teamStack->addWidget(makePage(m_bTeamIpPitch, m_bTeamPitch, m_bIpTitle, m_bTitle));
        tabLayout->addWidget(m_teamStack);
        connect(m_xiButton, &QPushButton::toggled, this, [this](bool xi) {
            m_teamStack->setCurrentIndex(xi ? 0 : 1);
        });

        m_depthBox = new QGroupBox(tr("Weitere Kader-Tiefe"), tab);
        auto *depthLayout = new QVBoxLayout(m_depthBox);
        m_depthEmpty = new QLabel(m_depthBox);
        m_depthEmpty->setWordWrap(true);
        m_depthEmpty->setObjectName(QStringLiteral("kpiCaption"));
        depthLayout->addWidget(m_depthEmpty);
        m_depthTable = new QTableWidget(m_depthBox);
        m_depthTable->setColumnCount(5);
        m_depthTable->setHorizontalHeaderLabels(
            {tr("Rolle"), tr("Name"), tr("Alter"), tr("DWRS"), tr("Eignung")});
        m_depthTable->verticalHeader()->setVisible(false);
        m_depthTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_depthTable->setAlternatingRowColors(true);
        m_depthTable->setSortingEnabled(true);
        m_depthTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_depthTable->horizontalHeader()->setStretchLastSection(true);
        m_depthTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        m_depthTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_depthTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        depthLayout->addWidget(m_depthTable);
        tabLayout->addWidget(m_depthBox);
        tabLayout->addStretch(1);
        m_tabs->addTab(tab, tr("Erste Mannschaft"));
    }

    // --- Tab 2: youth & second team ---
    {
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        auto *pitchRow = new QHBoxLayout;
        auto *secondColumn = new QVBoxLayout;
        m_secondXiTitle = new QLabel(tr("Zweitteam-XI"), tab);
        m_secondXiTitle->setObjectName(QStringLiteral("sectionTitle"));
        m_secondXiTitle->setAlignment(Qt::AlignHCenter);
        m_secondXiPitch = new TacticPitchWidget(m_theme, tab);
        secondColumn->addWidget(m_secondXiTitle);
        secondColumn->addWidget(m_secondXiPitch, 0, Qt::AlignTop | Qt::AlignHCenter);
        auto *youthColumn = new QVBoxLayout;
        auto *youthTitle = new QLabel(tr("Jugend-XI"), tab);
        youthTitle->setObjectName(QStringLiteral("sectionTitle"));
        youthTitle->setAlignment(Qt::AlignHCenter);
        m_youthXiPitch = new TacticPitchWidget(m_theme, tab);
        youthColumn->addWidget(youthTitle);
        youthColumn->addWidget(m_youthXiPitch, 0, Qt::AlignTop | Qt::AlignHCenter);
        pitchRow->addLayout(secondColumn, 1);
        pitchRow->addLayout(youthColumn, 1);
        tabLayout->addLayout(pitchRow);

        auto *manageHint = new QLabel(
            tr("✔️ Zum Verwalten dieser Spieler dient Transfers → Leihen & Verkäufe."), tab);
        tabLayout->addWidget(manageHint);

        const auto makeTable = [tab](const QStringList &headers) {
            auto *table = new QTableWidget(tab);
            table->setColumnCount(headers.size());
            table->setHorizontalHeaderLabels(headers);
            table->verticalHeader()->setVisible(false);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setAlternatingRowColors(true);
            table->setSortingEnabled(true);
            table->setMinimumHeight(240);
            table->horizontalHeader()->setStretchLastSection(true);
            return table;
        };
        m_loanBox = new QGroupBox(tr("Vielversprechende Talente zum Verleihen "
                                     "(nach Talent-Score)"),
                                  tab);
        auto *loanLayout = new QVBoxLayout(m_loanBox);
        m_loanTable = makeTable({tr("Name"), tr("Alter"), tr("Position"), tr("Beste Rolle"),
                                 tr("Bester DWRS"), tr("Talent"), tr("Ent"), tr("Arb"),
                                 tr("Transfer"), tr("Leihe")});
        loanLayout->addWidget(m_loanTable);
        tabLayout->addWidget(m_loanBox);

        m_sellBox = new QGroupBox(tr("Überzählige Spieler für Verkauf / Freigabe"), tab);
        auto *sellLayout = new QVBoxLayout(m_sellBox);
        m_sellTable = makeTable({tr("Name"), tr("Alter"), tr("Position"), tr("Beste Rolle"),
                                 tr("Bester DWRS"), tr("Ent"), tr("Arb"), tr("Transfer"),
                                 tr("Leihe")});
        sellLayout->addWidget(m_sellTable);
        tabLayout->addWidget(m_sellBox);
        tabLayout->addStretch(1);

        m_tabs->addTab(tab, tr("Jugend && Zweitteam"));
    }

    // --- Tab 3: where buying really improves the line-up ---
    m_signingTab = new SigningAdvisorTab(m_context);
    m_tabs->addTab(m_signingTab, tr("🛒 Kaufberatung"));

    // The main window's tabs choose the view (showSection).
    m_tabs->tabBar()->hide();
    layout->addWidget(m_tabs, 1);

    for (QComboBox *combo : {m_tacticCombo, m_competitionCombo}) {
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            if (!m_updating)
                rebuild();
        });
    }

    // Player boxes on the pitches are interactive (M13).
    for (TacticPitchWidget *pitch : {m_xiPitch, m_bTeamPitch, m_xiIpPitch, m_bTeamIpPitch,
                                     m_secondXiPitch, m_youthXiPitch}) {
        connect(pitch, &TacticPitchWidget::playerDoubleClicked, this,
                [this](const QString &uid) { PlayerActions::openProfile(m_context, uid); });
        connect(pitch, &TacticPitchWidget::playerContextMenuRequested, this,
                [this, pitch](const QString &uid, const QPoint &globalPos) {
                    // Starting XI of a two-phase tactic: bridges can be moved.
                    const bool startingXi = pitch == m_xiPitch || pitch == m_xiIpPitch;
                    if (startingXi && !m_bridge.tactic.isEmpty()
                        && m_bridge.slotsOfUid.contains(uid))
                        showBridgeMenu(uid, globalPos, pitch == m_xiIpPitch);
                    else
                        PlayerActions::showContextMenu(m_context, this, uid, globalPos);
                });
    }
    PlayerActions::attachToTableWidget(m_context, m_loanTable);
    PlayerActions::attachToTableWidget(m_context, m_sellTable);
    // The depth options carry a player uid in column 1 (Name).
    PlayerActions::attachToTableWidget(m_context, m_depthTable, 1);
}

void BestXiPage::showSection(const QString &pageId)
{
    const bool signing = pageId == QLatin1String("signing_advisor");
    m_tabs->setCurrentIndex(signing ? 2 : pageId == QLatin1String("best_xi_youth") ? 1 : 0);
    m_heading->setText(
        QStringLiteral("<h2>%1</h2>").arg(signing ? tr("Kaufberatung") : tr("Best XI")));
}

void BestXiPage::releaseStoreRows()
{
    m_signingTab->clear();
    m_bridge = BridgeState(); // its pool points into the store
}

QString BestXiPage::algorithmHint()
{
    return tr("Startelf und B-Team werden gemeinsam als Zuordnungsproblem berechnet, mit der "
              "Ungarischen Methode (englisch: Hungarian algorithm, Kuhn–Munkres): Spieler werden "
              "Positionen eins zu eins so zugeordnet, dass die Summe der Bewertungen maximal ist. "
              "Keine andere Aufstellung hat eine stärkere Startelf, und unter allen gleich starken "
              "bekommst du das stärkste B-Team — vielseitige Spieler stehen dort, wo ihr Fehlen "
              "am wenigsten kostet. Grundlage ist der DWRS je Rolle, gewichtet mit Spielzeit und "
              "natürlicher Position; Hauptrollen werden eingehalten.");
}

void BestXiPage::refresh()
{
    m_updating = true;
    const QString previous = m_tacticCombo->currentText();
    m_tacticCombo->clear();
    m_tacticCombo->addItems(favoritesFirstTactics(m_context, false));
    if (!previous.isEmpty() && m_tacticCombo->findText(previous) >= 0)
        m_tacticCombo->setCurrentText(previous);

    // Competition choice — league, UEFA competitions, domestic cup. Each has
    // its own eligibility: the league and UEFA lists are independent of each
    // other, the cups need no registration at all.
    const Registration::Settings registration = m_context.registrationSettings();
    const QVariant previousCompetition = m_competitionCombo->currentData();
    m_competitionCombo->clear();
    if (registration.active()) {
        const QString league = Registration::leagueDisplayName(registration.league);
        const QString cup = Registration::cupDisplayName(registration.league);
        if (Registration::restrictsSquad(registration.league))
            m_competitionCombo->addItem(tr("%1 (Meldeliste)").arg(league), kLeague);
        else
            m_competitionCombo->addItem(
                tr("%1 (alle Spieler)").arg(league.isEmpty() ? tr("Liga") : league), kLeague);
        if (registration.uefa)
            m_competitionCombo->addItem(
                tr("Champions / Europa / Conference League (Liste A + B)"), kUefa);
        m_competitionCombo->addItem(
            tr("%1 (alle Spieler)").arg(cup.isEmpty() ? tr("Pokal") : cup), kCup);
        if (previousCompetition.isValid())
            m_competitionCombo->setCurrentIndex(
                std::max(0, m_competitionCombo->findData(previousCompetition)));
    }
    m_competitionLabel->setVisible(registration.active());
    m_competitionCombo->setVisible(registration.active());
    m_updating = false;
    rebuild();
}

void BestXiPage::rebuild()
{
    const QString userClub = m_context.userClub();
    const QString secondClub = m_context.secondTeamClub();
    const QString tactic = m_tacticCombo->currentText();

    if (userClub.isEmpty() || tactic.isEmpty()) {
        m_clubNotice->setVisible(true);
        m_registrationNote->setVisible(false);
        m_xiPitch->clearData();
        m_bTeamPitch->clearData();
        m_xiIpPitch->clearData();
        m_bTeamIpPitch->clearData();
        m_secondXiPitch->clearData();
        m_youthXiPitch->clearData();
        m_injuryBox->setVisible(false);
        m_signingTab->setSquad(QString(), {});
        return;
    }
    m_clubNotice->setVisible(false);

    const auto positions = m_context.definitions().tacticRoles().value(tactic);
    const QStringList slotOrder = m_context.definitions().tacticSlotOrder(tactic);
    const auto layout = m_context.definitions().tacticLayouts().value(tactic);
    const auto roleNames = m_context.definitions().roleDisplayMap();
    const RoleRatings &ratings = m_context.ratings();

    // Long-term injured players are left out of every squad unless the user
    // plans them in anyway.
    const bool planInjured = m_planInjuredCheck->isChecked();
    std::vector<const Player *> clubAll, secondAll, injured;
    for (const Player &player : m_context.store().players()) {
        const bool firstTeam = player.club == userClub;
        if (!firstTeam && (secondClub.isEmpty() || player.club != secondClub))
            continue;
        (firstTeam ? clubAll : secondAll).push_back(&player);
        if (player.longTermInjured)
            injured.push_back(&player);
    }
    const auto fitOnly = [](const std::vector<const Player *> &players) {
        std::vector<const Player *> fit;
        for (const Player *player : players) {
            if (!player->longTermInjured)
                fit.push_back(player);
        }
        return fit;
    };
    const std::vector<const Player *> clubFit = fitOnly(clubAll);
    const std::vector<const Player *> secondFit = fitOnly(secondAll);
    const std::vector<const Player *> &clubPlayers = planInjured ? clubAll : clubFit;
    const std::vector<const Player *> &secondPlayers = planInjured ? secondAll : secondFit;

    // Registration: with a competition chosen, the first team is built only
    // from the players who may play in it. Second-team players count when they
    // are on the list themselves — the assistant fills free places with them.
    // The development squads (second team, youth, loan/sell lists) are about
    // the whole club, so they always start from the unrestricted first team.
    const int competitionMode = m_competitionCombo->currentData().toInt();
    const bool leagueList =
        competitionMode == kLeague
        && Registration::restrictsSquad(m_context.registrationSettings().league);
    const bool listedCompetition = leagueList || competitionMode == kUefa;
    const Registration::Competition competition = competitionMode == kUefa
                                                      ? Registration::Competition::Uefa
                                                      : Registration::Competition::League;
    const Registration::Quota competitionQuota =
        Registration::quotaFor(competition, m_context.registrationSettings());
    std::vector<const Player *> registrationPool = clubAll;
    registrationPool.insert(registrationPool.end(), secondAll.begin(), secondAll.end());
    const bool restricted =
        listedCompetition && Registration::hasSavedList(registrationPool, competition);

    // The first team of one pool; *full receives the unrestricted squad,
    // *note (optional) who the competition leaves out and *pool (optional) the
    // players the first team was chosen from.
    const auto firstTeamOf = [&](const std::vector<const Player *> &club,
                                 const std::vector<const Player *> &second, SquadResult *full,
                                 QString *note, std::vector<const Player *> *pool) {
        *full = m_context.squadBuilder().calculateSquadForTactic(club, tactic, ratings);
        if (!restricted) {
            if (pool)
                *pool = club;
            return *full;
        }
        // Who the list costs: the players a free choice would line up.
        QSet<QString> unrestrictedTeams;
        for (const auto &team : {full->startingXi, full->bTeam})
            for (const XiCell &cell : team)
                if (cell.isFilled())
                    unrestrictedTeams.insert(cell.playerUid);
        std::vector<const Player *> eligiblePlayers;
        int leftOut = 0;
        QStringList missed;
        for (const auto *pool : {&club, &second}) {
            for (const Player *player : *pool) {
                const bool firstTeam = pool == &club;
                if (firstTeam ? Registration::isEligible(*player, competition, competitionQuota)
                              : Registration::isListed(*player, competition)) {
                    eligiblePlayers.push_back(player);
                } else if (firstTeam) {
                    ++leftOut;
                    if (unrestrictedTeams.contains(player->uid))
                        missed << player->name;
                }
            }
        }
        if (note) {
            if (leftOut == 0) {
                *note = tr("Alle Spieler der ersten Mannschaft sind spielberechtigt.");
            } else {
                *note = tr("%1 Spieler der ersten Mannschaft sind nicht gemeldet und bleiben "
                           "außen vor.")
                            .arg(leftOut);
                if (!missed.isEmpty())
                    *note += QLatin1Char(' ')
                             + tr("Ohne Meldeliste stünden davon in Startelf oder B-Team: %1")
                                   .arg(missed.join(QStringLiteral(", ")));
            }
        }
        SquadResult first = m_context.squadBuilder().calculateSquadForTactic(
            eligiblePlayers, tactic, ratings);
        if (pool)
            *pool = std::move(eligiblePlayers);
        return first;
    };

    QString note;
    if (listedCompetition && !restricted)
        note = tr("⚠️ Für diesen Wettbewerb ist noch keine Meldeliste gespeichert — Best XI "
                  "rechnet mit allen Spielern. Speichere sie unter Transfers → Meldeliste.");
    SquadResult full;
    std::vector<const Player *> firstPool;
    const SquadResult first =
        firstTeamOf(clubPlayers, secondPlayers, &full, &note, &firstPool);
    m_registrationNote->setText(note);
    m_registrationNote->setVisible(!note.isEmpty());

    // The injury comparison is always "all fit" against "without the injured",
    // whichever of the two is on the pitches.
    if (injured.empty()) {
        m_injuryBox->setVisible(false);
    } else {
        SquadResult otherFull;
        const SquadResult other = planInjured
                                      ? firstTeamOf(clubFit, secondFit, &otherFull, nullptr,
                                                    nullptr)
                                      : firstTeamOf(clubAll, secondAll, &otherFull, nullptr,
                                                    nullptr);
        fillInjuryBox(injured, planInjured ? first : other, planInjured ? other : first,
                      slotOrder, positions);
    }

    // A copy: the two-phase line-ups below still need the pool.
    m_signingTab->setSquad(tactic, firstPool);

    const DevelopmentSquads dev = m_context.squadBuilder().calculateDevelopmentSquads(
        secondPlayers, full.depthPool, positions, slotOrder, ratings, full.depthPlayerUids);

    for (TacticPitchWidget *pitch : {m_xiPitch, m_bTeamPitch, m_xiIpPitch, m_bTeamIpPitch,
                                     m_secondXiPitch, m_youthXiPitch})
        pitch->setFmVersionId(m_context.fmVersionId());
    const bool twoPhase = m_context.definitions().isTwoPhaseTactic(tactic);
    for (QWidget *w : std::initializer_list<QWidget *>{m_xiIpPitch, m_bTeamIpPitch, m_xiIpTitle,
                                                       m_bIpTitle, m_xiTitle, m_bTitle})
        w->setVisible(twoPhase);
    m_phaseNote->setVisible(twoPhase);
    m_bridge = BridgeState();
    m_bridgeBar->setVisible(false);
    if (twoPhase) {
        // Two free line-ups: who plays and where in each is one optimisation
        // (SquadBuilder::calculateTwoPhaseSquad); depth, injuries and the
        // development squads below still use the out-of-possession line-up.
        const auto &lineUps = m_context.definitions().twoPhaseTactics().value(tactic);
        const int share = m_context.config().ipSharePercent();
        // Manual bridges of players no longer in the pool (sold, injured,
        // not registered) are left out.
        Bridges bridges;
        {
            const Bridges stored = loadBridges(tactic);
            for (const Player *player : firstPool) {
                const auto it = stored.constFind(player->uid);
                if (it != stored.cend())
                    bridges.insert(player->uid, *it);
            }
        }
        const TwoPhaseSquadResult squad = m_context.squadBuilder().calculateTwoPhaseSquad(
            firstPool, tactic, ratings, share / 100.0, true, bridges);
        m_bridge.tactic = tactic;
        m_bridge.pool = firstPool;
        m_bridge.share = share / 100.0;
        m_bridge.bridges = bridges;
        m_bridge.slotsOfUid = squad.slotsOfUid;
        m_bridge.current = SquadBuilder::startingValue(squad);
        m_bridge.optimum = bridges.isEmpty()
            ? m_bridge.current
            : SquadBuilder::startingValue(m_context.squadBuilder().calculateTwoPhaseSquad(
                  firstPool, tactic, ratings, share / 100.0));
        if (!bridges.isEmpty()) {
            const double loss = m_bridge.optimum - m_bridge.current;
            m_bridgeNote->setText(
                tr("✋ Von Hand angepasst (%n Spieler). Die Elf ist %1 DWRS-Punkte schwächer "
                   "als der Vorschlag des Analyzers.",
                   nullptr, static_cast<int>(bridges.size()))
                    .arg(QLocale().toString(std::max(0.0, loss), 'f', 1)));
            m_bridgeBar->setVisible(true);
        }
        const auto &slotMap = masterPositionMap(m_context.fmVersionId());
        const auto layoutOf = [&](const QHash<QString, QString> &lineUp) {
            QHash<QString, QStringList> strata;
            for (auto it = lineUp.cbegin(); it != lineUp.cend(); ++it) {
                if (it.key() != QLatin1String("GK"))
                    strata[slotMap.value(it.key()).stratum].append(it.key());
            }
            return strata;
        };
        // Under each name: his slot in the other line-up.
        const auto partnerTexts = [&](const QHash<QString, XiCell> &team, bool ipSide) {
            QHash<QString, QString> texts;
            for (auto it = team.cbegin(); it != team.cend(); ++it) {
                if (!it->isFilled())
                    continue;
                const auto pair = squad.slotsOfUid.value(it->playerUid);
                texts.insert(it.key(), ipSide ? tr("ohne Ball: %1").arg(pair.second)
                                              : tr("mit Ball: %1").arg(pair.first));
            }
            return texts;
        };
        m_xiPitch->setTeam(squad.startingOop, lineUps.oop, layoutOf(lineUps.oop), roleNames);
        m_xiPitch->setSlotSubtexts(partnerTexts(squad.startingOop, false));
        m_bTeamPitch->setTeam(squad.bTeamOop, lineUps.oop, layoutOf(lineUps.oop), roleNames);
        m_bTeamPitch->setSlotSubtexts(partnerTexts(squad.bTeamOop, false));
        m_xiIpPitch->setTeam(squad.startingIp, lineUps.ip, layoutOf(lineUps.ip), roleNames);
        m_xiIpPitch->setSlotSubtexts(partnerTexts(squad.startingIp, true));
        m_bTeamIpPitch->setTeam(squad.bTeamIp, lineUps.ip, layoutOf(lineUps.ip), roleNames);
        m_bTeamIpPitch->setSlotSubtexts(partnerTexts(squad.bTeamIp, true));
        m_phaseNote->setText(
            tr("Diese Taktik hat eine Aufstellung mit Ball und eine ohne Ball. Der Analyzer "
               "wählt die Spieler so, dass die Summe aus %1 % Rolle mit Ball und %2 % Rolle "
               "ohne Ball am höchsten ist; jeder Spieler braucht die Position in beiden "
               "Aufstellungen. Unter dem Namen steht sein Platz in der anderen Aufstellung. "
               "Den Anteil stellst du in den Einstellungen ein. Per Rechtsklick auf einen "
               "Spieler der Startelf verschiebst du ihn selbst; der Analyzer zeigt, was "
               "jeder Platz kostet.")
                .arg(share)
                .arg(100 - share));
    } else {
        m_xiPitch->setTeam(first.startingXi, positions, layout, roleNames);
        m_bTeamPitch->setTeam(first.bTeam, positions, layout, roleNames);
        m_xiPitch->setSlotSubtexts({});
        m_bTeamPitch->setSlotSubtexts({});
    }
    m_secondXiTitle->setText(secondClub.isEmpty()
                                 ? tr("Zweitteam-XI (aus Überschuss des Hauptvereins)")
                                 : tr("Zweitteam-XI (%1)").arg(secondClub));
    m_secondXiPitch->setTeam(dev.secondTeamXi, positions, layout, roleNames);
    m_youthXiPitch->setTeam(dev.youthXi, positions, layout, roleNames);

    // --- Depth options grouped by role, naturally sorted ---
    fillDepthTable(first.bestDepthOptions);

    fillSurplusTable(m_loanTable, dev.loanCandidates, true);
    fillSurplusTable(m_sellTable, dev.sellCandidates, false);
    m_loanBox->setVisible(!dev.loanCandidates.empty());
    m_sellBox->setVisible(!dev.sellCandidates.empty());
}

BestXiPage::Bridges BestXiPage::loadBridges(const QString &tactic) const
{
    Bridges bridges;
    const QJsonObject obj =
        QJsonDocument::fromJson(
            m_context.database().setting(QStringLiteral("bridges/") + tactic).toUtf8())
            .object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QJsonArray pair = it.value().toArray();
        if (pair.size() == 2)
            bridges.insert(it.key(), {pair.at(0).toString(), pair.at(1).toString()});
    }
    return bridges;
}

void BestXiPage::saveBridges(const QString &tactic, const Bridges &bridges)
{
    const QString key = QStringLiteral("bridges/") + tactic;
    if (bridges.isEmpty()) {
        m_context.database().removeSetting(key);
        return;
    }
    QJsonObject obj;
    for (auto it = bridges.cbegin(); it != bridges.cend(); ++it)
        obj.insert(it.key(), QJsonArray{it->first, it->second});
    m_context.database().setSetting(key,
                                    QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void BestXiPage::showBridgeMenu(const QString &uid, const QPoint &globalPos, bool ipPhase)
{
    const BridgeState state = m_bridge; // a rebuild may run while the menu is open
    const auto nameOf = [this](const QString &playerUid) {
        const Player *p = m_context.store().findByUid(playerUid);
        return p ? p->name : playerUid;
    };
    const auto delta = [](double value) {
        const QString text = QLocale().toString(value, 'f', 1);
        return value >= 0.05 ? QStringLiteral("+") + text : value <= -0.05 ? text : QStringLiteral("±0");
    };
    PlayerActions::showContextMenu(m_context, this, uid, globalPos, [&](QMenu &menu) {
        const QPair<QString, QString> mine = state.slotsOfUid.value(uid);
        const QString current = ipPhase ? mine.first : mine.second;
        QMenu *sub = menu.addMenu(ipPhase ? tr("↔ Platz mit Ball ändern (jetzt %1)").arg(current)
                                          : tr("↔ Platz ohne Ball ändern (jetzt %1)").arg(current));
        const QList<BridgeOption> options = m_context.squadBuilder().bridgeOptions(
            state.pool, state.tactic, m_context.ratings(), state.share, state.bridges, uid,
            ipPhase);
        if (options.isEmpty()) {
            sub->addAction(tr("Kein anderer Platz möglich (Position fehlt)"))->setEnabled(false);
        } else {
            auto *caption = sub->addAction(tr("Platz · Tausch mit · DWRS-Punkte der Elf"));
            caption->setEnabled(false);
            sub->addSeparator();
        }
        for (const BridgeOption &option : options) {
            const QString text =
                option.swapUid.isEmpty()
                    ? tr("%1 (frei) · %2").arg(option.slot, delta(option.total - state.current))
                    : tr("%1 · %2 · %3")
                          .arg(option.slot, nameOf(option.swapUid),
                               delta(option.total - state.current));
            const Bridges bridges = option.bridges;
            const QString tactic = state.tactic;
            sub->addAction(text, this, [this, tactic, bridges] {
                saveBridges(tactic, bridges);
                rebuild();
            });
        }
        if (state.bridges.contains(uid)) {
            menu.addAction(tr("↺ %1 wieder dem Analyzer überlassen").arg(nameOf(uid)), this,
                           [this, uid, tactic = state.tactic] {
                               Bridges bridges = loadBridges(tactic);
                               bridges.remove(uid);
                               saveBridges(tactic, bridges);
                               rebuild();
                           });
        }
        if (!state.bridges.isEmpty()) {
            menu.addAction(tr("↺ Vorschlag des Analyzers wiederherstellen (%1)")
                               .arg(delta(state.optimum - state.current)),
                           this, [this, tactic = state.tactic] {
                               saveBridges(tactic, {});
                               rebuild();
                           });
        }
    });
}

void BestXiPage::fillInjuryBox(const std::vector<const Player *> &injured,
                               const SquadResult &allFit, const SquadResult &withoutInjured,
                               const QStringList &slotOrder,
                               const QHash<QString, QString> &positions)
{
    const auto roleNames = m_context.definitions().roleDisplayMap();
    const auto total = [](const QHash<QString, XiCell> &team) {
        double sum = 0.0;
        for (const XiCell &cell : team)
            sum += cell.rating;
        return sum;
    };

    QStringList names;
    for (const Player *player : injured)
        names << QStringLiteral("<a href=\"%1\" style=\"color:%3\">%2</a>")
                     .arg(player->uid.toHtmlEscaped(), player->name.toHtmlEscaped(),
                          m_theme.text().name());
    const int xiLoss = qRound(total(allFit.startingXi) - total(withoutInjured.startingXi));
    const int bLoss = qRound(total(allFit.bTeam) - total(withoutInjured.bTeam));
    QString text = (m_planInjuredCheck->isChecked() ? tr("Trotz Verletzung eingeplant: %1.")
                                                    : tr("Nicht eingeplant: %1."))
                       .arg(names.join(QStringLiteral(", ")));
    if (xiLoss == 0 && bLoss == 0)
        text += QLatin1Char(' ') + tr("Startelf und B-Team bleiben gleich stark.");
    else
        text += QLatin1Char(' ')
                + tr("Der Kader gleicht das so aus — Startelf %1, B-Team %2 DWRS-Punkte "
                     "(Summe über alle Positionen):")
                      .arg(xiLoss > 0 ? QStringLiteral("−%1").arg(xiLoss)
                                      : QStringLiteral("±0"))
                      .arg(bLoss > 0 ? QStringLiteral("−%1").arg(bLoss)
                                     : bLoss < 0 ? QStringLiteral("+%1").arg(-bLoss)
                                                 : QStringLiteral("±0"));
    m_injuryNote->setText(text);
    m_injuryBox->setTitle(tr("🚑 Langzeitverletzt (%1)").arg(injured.size()));

    struct Change {
        QString team;
        QString slot;
        XiCell before;
        XiCell after;
    };
    QList<Change> changes;
    // Slots sharing a role are compared as a group, so a player who merely
    // switches sides (DMCL <-> DMCR) is no change: only who left the group is
    // paired with who joined it.
    const auto collect = [&](const QString &team, const QHash<QString, XiCell> &before,
                             const QHash<QString, XiCell> &after) {
        QStringList done;
        for (const QString &slot : slotOrder) {
            const QString role = positions.value(slot);
            if (done.contains(role))
                continue;
            done << role;
            QStringList groupSlots;
            for (const QString &s : slotOrder)
                if (positions.value(s) == role)
                    groupSlots << s;
            QList<QPair<QString, XiCell>> left;
            QList<XiCell> joined;
            for (const QString &s : groupSlots) {
                const XiCell a = before.value(s);
                const bool stays = std::any_of(groupSlots.cbegin(), groupSlots.cend(),
                                               [&](const QString &o) {
                                                   return after.value(o).playerUid == a.playerUid;
                                               });
                if (!stays)
                    left.append({s, a});
                const XiCell b = after.value(s);
                const bool was = std::any_of(groupSlots.cbegin(), groupSlots.cend(),
                                             [&](const QString &o) {
                                                 return before.value(o).playerUid == b.playerUid;
                                             });
                if (!was)
                    joined.append(b);
            }
            for (qsizetype i = 0; i < left.size() && i < joined.size(); ++i)
                changes.append({team, left.at(i).first, left.at(i).second, joined.at(i)});
        }
    };
    collect(tr("Startelf"), allFit.startingXi, withoutInjured.startingXi);
    collect(tr("B-Team"), allFit.bTeam, withoutInjured.bTeam);

    // Visible before filling, so the columns are measured with the final style.
    m_injuryBox->setVisible(true);
    m_injuryTable->setVisible(!changes.isEmpty());
    m_injuryTable->clearContents();
    m_injuryTable->setRowCount(static_cast<int>(changes.size()));
    const auto playerItem = [](const XiCell &cell) {
        auto *item = new QTableWidgetItem(cell.isFilled() ? cell.name : tr("— unbesetzt —"));
        item->setData(Qt::UserRole, cell.playerUid);
        return item;
    };
    const auto dwrsItem = [](const XiCell &cell) {
        auto *item = new NumericItem(cell.isFilled() ? QStringLiteral("%1%").arg(qRound(cell.rating))
                                                     : QStringLiteral("–"),
                                     cell.rating);
        const CellStyle style = dwrsCellStyle(cell.rating);
        if (cell.isFilled() && style.isValid()) {
            item->setBackground(style.background);
            item->setForeground(style.text);
        }
        return item;
    };
    int row = 0;
    for (const Change &change : changes) {
        const QString role = positions.value(change.slot);
        m_injuryTable->setItem(row, 0, new QTableWidgetItem(change.team));
        m_injuryTable->setItem(row, 1, new QTableWidgetItem(change.slot));
        m_injuryTable->setItem(row, 2, new QTableWidgetItem(roleNames.value(role, role)));
        auto *before = playerItem(change.before);
        if (const Player *p = m_context.store().findByUid(change.before.playerUid);
            p && p->longTermInjured)
            before->setText(QStringLiteral("🚑 ") + before->text());
        m_injuryTable->setItem(row, 3, before);
        m_injuryTable->setItem(row, 4, dwrsItem(change.before));
        m_injuryTable->setItem(row, 5, playerItem(change.after));
        m_injuryTable->setItem(row, 6, dwrsItem(change.after));
        const int delta = qRound(change.after.rating - change.before.rating);
        m_injuryTable->setItem(row, 7,
                               new NumericItem(delta > 0 ? QStringLiteral("+%1").arg(delta)
                                                         : QString::number(delta),
                                               delta));
        ++row;
    }
    // Size the table to its rows: no inner scrollbar, no dead space.
    int height = m_injuryTable->horizontalHeader()->sizeHint().height()
                 + 2 * m_injuryTable->frameWidth();
    for (int r = 0; r < row; ++r)
        height += m_injuryTable->rowHeight(r);
    m_injuryTable->setFixedHeight(height);
}

void BestXiPage::fillSurplusTable(QTableWidget *table,
                                  const std::vector<const Player *> &players,
                                  bool includeTalent)
{
    // Port of legacy create_detailed_surplus_df.
    const RoleRatings &ratings = m_context.ratings();
    const auto roleNames = m_context.definitions().roleDisplayMap();
    const int outfielderCap = m_context.config().ageThreshold(QStringLiteral("outfielder"));
    const int goalkeeperCap = m_context.config().ageThreshold(QStringLiteral("goalkeeper"));

    table->setSortingEnabled(false);
    table->clearContents();
    table->setRowCount(static_cast<int>(players.size()));
    int row = 0;
    for (const Player *player : players) {
        double bestDwrs = 0.0;
        QString bestRole;
        for (const QString &role : player->assignedRoles) {
            const double rating = ratings.value(role).value(player->uid, 0.0);
            if (rating > bestDwrs) {
                bestDwrs = rating;
                bestRole = role;
            }
        }

        int column = 0;
        auto *nameItem = new NameItem(player->name);
        nameItem->setData(Qt::UserRole, player->uid);
        table->setItem(row, column++, nameItem);
        table->setItem(row, column++,
                       new NumericItem(QString::number(player->age), player->age));
        table->setItem(row, column++, new QTableWidgetItem(player->positionRaw));
        table->setItem(row, column++,
                       new QTableWidgetItem(bestRole.isEmpty()
                                                ? QStringLiteral("–")
                                                : roleNames.value(bestRole, bestRole)));
        auto *dwrsItem = new NumericItem(QStringLiteral("%1%").arg(qRound(bestDwrs)), bestDwrs);
        if (bestDwrs > 0.0) {
            const CellStyle dwrsStyle = dwrsCellStyle(bestDwrs);
            dwrsItem->setBackground(dwrsStyle.background);
            dwrsItem->setForeground(dwrsStyle.text);
        }
        table->setItem(row, column++, dwrsItem);
        if (includeTalent) {
            const double ageCap =
                TalentEngine::ageCapForPlayer(*player, outfielderCap, goalkeeperCap);
            const double talent = TalentEngine::talentForPlayer(m_context.definitions(),
                                                                *player, bestDwrs, ageCap);
            auto *item = new NumericItem(QString::number(qRound(talent)), talent);
            const CellStyle style = dwrsCellStyle(talent);
            if (style.isValid()) {
                item->setBackground(style.background);
                item->setForeground(style.text);
            }
            table->setItem(row, column++, item);
        }
        const auto attrItem = [player](Attr attr) {
            const int value = static_cast<int>(player->attrMean(idx(attr)));
            auto *item = new NumericItem(QString::number(value), value);
            const CellStyle style = attributeCellStyle(value);
            if (style.isValid()) {
                item->setBackground(style.background);
                item->setForeground(style.text);
            }
            return item;
        };
        table->setItem(row, column++, attrItem(Attr::Determination));
        table->setItem(row, column++, attrItem(Attr::WorkRate));
        table->setItem(row, column++,
                       new QTableWidgetItem(player->transferStatus ? QStringLiteral("✅")
                                                                   : QStringLiteral("❌")));
        table->setItem(row, column++,
                       new QTableWidgetItem(player->loanStatus ? QStringLiteral("✅")
                                                               : QStringLiteral("❌")));
        ++row;
    }
    table->setSortingEnabled(true);
    table->resizeColumnsToContents();
}

void BestXiPage::fillDepthTable(const QHash<QString, QList<DepthOption>> &options)
{
    const auto roleNames = m_context.definitions().roleDisplayMap();

    if (options.isEmpty()) {
        m_depthEmpty->setText(
            tr("Keine weiteren Spieler als Tiefen-Optionen für diese Taktik geeignet."));
        m_depthEmpty->setVisible(true);
        m_depthTable->setVisible(false);
        m_depthTable->setRowCount(0);
        return;
    }
    m_depthEmpty->setVisible(false);
    m_depthTable->setVisible(true);

    // One row per (role, player) option, roles in natural pitch order.
    QStringList roles = options.keys();
    roles = m_context.definitions().sortRolesNaturally(roles);
    int rowCount = 0;
    for (const QString &role : roles)
        rowCount += options.value(role).size();

    m_depthTable->setSortingEnabled(false);
    m_depthTable->clearContents();
    m_depthTable->setRowCount(rowCount);
    int row = 0;
    for (const QString &role : roles) {
        for (const DepthOption &option : options.value(role)) {
            int column = 0;
            m_depthTable->setItem(row, column++,
                                  new QTableWidgetItem(roleNames.value(role, role)));

            auto *nameItem = new NameItem(option.name);
            nameItem->setData(Qt::UserRole, option.playerUid);
            m_depthTable->setItem(row, column++, nameItem);

            m_depthTable->setItem(row, column++,
                                  new NumericItem(QString::number(option.age), option.age));

            auto *dwrsItem =
                new NumericItem(QStringLiteral("%1%").arg(qRound(option.rating)), option.rating);
            dwrsItem->setTextAlignment(Qt::AlignCenter);
            const CellStyle style = dwrsCellStyle(option.rating);
            if (style.isValid()) {
                dwrsItem->setBackground(style.background);
                dwrsItem->setForeground(style.text);
            }
            m_depthTable->setItem(row, column++, dwrsItem);

            m_depthTable->setItem(row, column++,
                                  new QTableWidgetItem(option.apt.isEmpty()
                                                           ? QStringLiteral("–")
                                                           : option.apt));
            ++row;
        }
    }
    m_depthTable->setSortingEnabled(true);
    m_depthTable->resizeColumnsToContents();
    m_depthTable->horizontalHeader()->setStretchLastSection(true);

    // Size the table to its content so no inner scrollbar and no dead space
    // appears between it and the pitches above.
    m_depthTable->resizeRowsToContents();
    int height = m_depthTable->horizontalHeader()->height() + 2 * m_depthTable->frameWidth();
    for (int r = 0; r < rowCount; ++r)
        height += m_depthTable->rowHeight(r);
    m_depthTable->setFixedHeight(height);
}

} // namespace fm
