#include "RegistrationPage.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../widgets/NumericTableItem.h"
#include "PageHelpers.h"
#include "core/PlayerStatus.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace fm {

namespace {

enum Column { ColStatus, ColName, ColAge, ColPosition, ColCategory, ColNeed, ColDwrs, ColSaved,
              ColNote, ColCount };

// Settings value behind each competition combo entry.
constexpr int kLeague = 0;
constexpr int kUefa = 1;

bool listedFor(const Player &player, Registration::Competition competition)
{
    return competition == Registration::Competition::Uefa ? player.registration.uefaListed
                                                          : player.registration.leagueListed;
}

} // namespace

RegistrationPage::RegistrationPage(AppContext &context, QWidget *parent)
    : PageBase(context, parent)
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

    layout->addWidget(
        new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Registrierung")), content));

    m_hint = new QLabel(content);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    auto *competitionRow = new QHBoxLayout;
    competitionRow->addWidget(new QLabel(tr("Wettbewerb:"), content));
    m_competitionCombo = new QComboBox(content);
    m_competitionCombo->setMinimumWidth(260);
    competitionRow->addWidget(m_competitionCombo);
    competitionRow->addStretch(1);
    layout->addLayout(competitionRow);

    m_summaryLabel = new QLabel(content);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);
    m_costLabel = new QLabel(content);
    m_costLabel->setWordWrap(true);
    m_costLabel->setObjectName(QStringLiteral("kpiCaption"));
    layout->addWidget(m_costLabel);
    m_warningLabel = new QLabel(content);
    m_warningLabel->setWordWrap(true);
    layout->addWidget(m_warningLabel);

    m_table = new QTableWidget(content);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({tr("Status"), tr("Name"), tr("Alter"), tr("Position"),
                                        tr("Ausbildung"), tr("Bedarf"), tr("DWRS"),
                                        tr("Gespeichert"), tr("Hinweis")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(false); // proposal order is the point
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(520);
    layout->addWidget(m_table, 1);
    // Right-click → Registrierung edits the flags; the page recomputes live.
    PlayerActions::attachToTableWidget(m_context, m_table, ColName, true);

    auto *saveRow = new QHBoxLayout;
    saveRow->addStretch(1);
    m_saveButton = new QPushButton(tr("Als Meldeliste übernehmen"), content);
    m_saveButton->setToolTip(
        tr("Speichert den Vorschlag als gemeldete Liste dieses Wettbewerbs, damit "
           "spätere Änderungen (Transfers, neue Markierungen) sichtbar werden."));
    saveRow->addWidget(m_saveButton);
    layout->addLayout(saveRow);

    connect(m_competitionCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_updating)
            refresh();
    });
    connect(m_saveButton, &QPushButton::clicked, this, &RegistrationPage::saveList);
}

std::vector<const Player *> RegistrationPage::pool() const
{
    const QString userClub = m_context.userClub();
    const QString secondClub = m_context.secondTeamClub();
    const PlayerStatus::FreshnessContext freshness = m_context.freshnessContext();
    std::vector<const Player *> players;
    for (const Player &player : m_context.store().players()) {
        const bool inClub = player.club == userClub
                            || (!secondClub.isEmpty() && player.club == secondClub);
        if (inClub && !PlayerStatus::isRetired(player, freshness))
            players.push_back(&player);
    }
    return players;
}

QStringList RegistrationPage::rankingTactics(bool *usedFallback) const
{
    const QStringList known = m_context.definitions().tacticNames();
    QStringList tactics;
    for (const QString &key : {QStringLiteral("favorite_tactic_1"),
                               QStringLiteral("favorite_tactic_2")}) {
        const QString tactic = m_context.database().setting(key);
        if (!tactic.isEmpty() && known.contains(tactic) && !tactics.contains(tactic))
            tactics << tactic;
    }
    *usedFallback = tactics.isEmpty();
    if (tactics.isEmpty()) {
        const QStringList ordered = favoritesFirstTactics(m_context, false);
        if (!ordered.isEmpty())
            tactics << ordered.first();
    }
    return tactics;
}

Registration::Competition RegistrationPage::competition() const
{
    return m_competitionCombo->currentData().toInt() == kUefa ? Registration::Competition::Uefa
                                                              : Registration::Competition::League;
}

void RegistrationPage::refresh()
{
    const Registration::Settings settings = m_context.registrationSettings();
    const QString userClub = m_context.userClub();

    // Competition choice follows the enabled rule sets.
    m_updating = true;
    const int previous = m_competitionCombo->currentData().isValid()
                             ? m_competitionCombo->currentData().toInt()
                             : kLeague;
    m_competitionCombo->clear();
    if (settings.league == Registration::LeagueRules::PremierLeague)
        m_competitionCombo->addItem(tr("Premier League"), kLeague);
    if (settings.uefa)
        m_competitionCombo->addItem(tr("Champions / Europa / Conference League (Liste A)"),
                                    kUefa);
    m_competitionCombo->setCurrentIndex(std::max(0, m_competitionCombo->findData(previous)));
    m_updating = false;

    const bool ready = settings.active() && !userClub.isEmpty();
    for (QWidget *w : {static_cast<QWidget *>(m_competitionCombo), static_cast<QWidget *>(m_table),
                       static_cast<QWidget *>(m_saveButton), static_cast<QWidget *>(m_summaryLabel),
                       static_cast<QWidget *>(m_costLabel), static_cast<QWidget *>(m_warningLabel)})
        w->setVisible(ready);
    if (!settings.active()) {
        m_hint->setText(tr("⚠️ Keine Registrierungsregeln aktiv. Wähle sie unter "
                           "Einstellungen → Verein → Registrierungsregeln."));
        return;
    }
    if (userClub.isEmpty()) {
        m_hint->setText(tr("Bitte wähle zuerst deinen Verein (Dashboard oder Einstellungen)."));
        return;
    }

    const Registration::Competition comp = competition();
    const auto players = pool();
    bool fallback = false;
    const QStringList tactics = rankingTactics(&fallback);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto ranked = Registration::rankPool(m_context.squadBuilder(), m_context.definitions(),
                                               players, tactics, m_context.ratings());
    const Registration::Quota quota = Registration::quotaFor(comp, settings);
    const Registration::Proposal proposal = Registration::propose(ranked, comp, quota);
    std::vector<const Player *> eligible;
    for (const auto &entry : proposal.listed)
        eligible.push_back(entry.player);
    for (const auto &entry : proposal.exempt)
        eligible.push_back(entry.player);
    const auto costs = Registration::quotaCosts(m_context.squadBuilder(), m_context.definitions(),
                                                players, eligible, tactics, m_context.ratings());
    QApplication::restoreOverrideCursor();

    m_hint->setText(
        tr("Pool: Erste Mannschaft + Zweitteam (%1 Spieler, ohne Retired). Rangfolge aus "
           "Startelf, B-Team und Kadertiefe von: %2. Markierungen änderst du per Rechtsklick "
           "→ Registrierung — der Vorschlag rechnet sofort neu.")
            .arg(players.size())
            .arg(tactics.isEmpty() ? tr("keine Taktik vorhanden") : tactics.join(QStringLiteral(", "))));

    // --- Summary ---
    const bool uefa = comp == Registration::Competition::Uefa;
    QStringList parts;
    parts << tr("<b>Meldeliste:</b> %1 / %2").arg(proposal.listed.size()).arg(quota.maxSize);
    parts << tr("Nicht-HG %1 / %2").arg(proposal.nonHomeGrown).arg(quota.maxNonHomeGrown);
    parts << tr("Home-Grown %1").arg(proposal.homeGrownOnly);
    parts << tr("Club-Grown %1").arg(proposal.clubTrained);
    parts << tr("Torhüter %1 (min. %2)").arg(proposal.goalkeepers).arg(quota.minGoalkeepers);
    QString summary = parts.join(QStringLiteral(" · "));
    summary += QStringLiteral("<br>")
               + (uefa ? tr("<b>Liste B</b> (ohne Platz): %1 Spieler")
                       : tr("<b>U21</b> (ohne Platz): %1 Spieler"))
                     .arg(proposal.exempt.size());

    // Diff against the saved list, if there is one.
    m_proposedUids.clear();
    for (const auto &entry : proposal.listed)
        m_proposedUids.insert(entry.player->uid);
    QStringList added, removed;
    bool haveSaved = false;
    for (const Player &player : m_context.store().players()) {
        const bool saved = listedFor(player, comp);
        haveSaved = haveSaved || saved;
        if (saved && !m_proposedUids.contains(player.uid))
            removed << player.name;
    }
    if (haveSaved) {
        for (const auto &entry : proposal.listed)
            if (!listedFor(*entry.player, comp))
                added << entry.player->name;
        summary += QStringLiteral("<br>")
                   + (added.isEmpty() && removed.isEmpty()
                          ? tr("Entspricht der gespeicherten Meldeliste.")
                          : tr("Gegenüber der gespeicherten Liste: <b>%1</b> neu melden, "
                               "<b>%2</b> abmelden.")
                                .arg(added.size())
                                .arg(removed.size()));
    }
    m_summaryLabel->setText(summary);

    // --- Cost of the quota ---
    QStringList costLines;
    for (const auto &cost : costs) {
        const int before = static_cast<int>(std::lround(cost.unrestricted));
        const int after = static_cast<int>(std::lround(cost.registered));
        costLines << tr("%1: Ø Startelf-DWRS %2 → %3 (%4)")
                         .arg(cost.tactic)
                         .arg(before)
                         .arg(after)
                         .arg(after - before >= 0 ? tr("keine Einbuße")
                                                  : QString::number(after - before));
    }
    m_costLabel->setText(tr("Kosten der Registrierung (ohne Regeln → mit Meldeliste):")
                         + QStringLiteral("<br>") + costLines.join(QStringLiteral("<br>")));

    // --- Warnings ---
    QStringList warnings;
    if (fallback)
        warnings << tr("Keine Lieblingstaktik gesetzt — Rangfolge aus '%1'. Lege sie unter "
                       "Einstellungen → Verein fest.")
                        .arg(tactics.value(0));
    if (proposal.emptySlots > 0) {
        QString why;
        if (proposal.nonHomeGrown >= quota.maxNonHomeGrown
            && proposal.nonHomeGrown + proposal.homeGrownOnly >= quota.maxNonClubTrained)
            why = tr("nur Club-Grown-Spieler dürften sie belegen, und es gibt keine weiteren.");
        else if (proposal.nonHomeGrown >= quota.maxNonHomeGrown)
            why = tr("nur Home-Grown-Spieler dürften sie belegen, und es gibt keine weiteren.");
        else
            why = tr("es gibt keine weiteren Spieler im Pool.");
        warnings << tr("%1 Plätze bleiben frei: %2").arg(proposal.emptySlots).arg(why);
    }
    if (proposal.goalkeepers < quota.minGoalkeepers)
        warnings << tr("Nur %1 Torhüter verfügbar (gewünscht: %2).")
                        .arg(proposal.goalkeepers)
                        .arg(quota.minGoalkeepers);
    QStringList uncertain;
    for (const Player *p : players)
        if (Registration::u21Status(*p) == Registration::U21Status::Uncertain)
            uncertain << p->name;
    if (!uncertain.isEmpty())
        warnings << tr("U21 unklar (Alter 21 oder unbekannt), zählt vorsichtshalber als nicht "
                       "U21: %1. Per Rechtsklick → Registrierung festlegen.")
                        .arg(uncertain.join(QStringLiteral(", ")));
    if (!removed.isEmpty())
        warnings << tr("Abmelden: %1").arg(removed.join(QStringLiteral(", ")));
    m_warningLabel->setText(warnings.isEmpty()
                                ? QString()
                                : QStringLiteral("⚠️ ")
                                      + warnings.join(QStringLiteral("<br>⚠️ ")));
    m_warningLabel->setVisible(!warnings.isEmpty());

    fillTable(proposal);
}

void RegistrationPage::fillTable(const Registration::Proposal &proposal)
{
    const bool uefa = competition() == Registration::Competition::Uefa;
    const auto categoryText = [](const Player &p) {
        switch (Registration::category(p)) {
        case Registration::Category::ClubTrained:
            return tr("Club-Grown");
        case Registration::Category::HomeGrown:
            return tr("Home-Grown");
        case Registration::Category::NonHomeGrown:
            break;
        }
        return tr("Nicht-HG");
    };
    const auto tierText = [](Registration::Tier tier) {
        switch (tier) {
        case Registration::Tier::StartingXi:
            return tr("Startelf");
        case Registration::Tier::BTeam:
            return tr("B-Team");
        case Registration::Tier::Depth:
            return tr("Kadertiefe");
        case Registration::Tier::Reserve:
            break;
        }
        return tr("Reserve");
    };
    const auto reasonText = [](Registration::LeftOutReason reason) {
        switch (reason) {
        case Registration::LeftOutReason::NonHomeGrownFull:
            return tr("Nicht-HG-Quote voll");
        case Registration::LeftOutReason::NonClubTrainedFull:
            return tr("nur noch Club-Grown-Plätze frei");
        case Registration::LeftOutReason::ListFull:
            break;
        }
        return tr("Liste voll");
    };

    m_table->setRowCount(0);
    const auto addRow = [&](const Registration::RankedPlayer &entry, const QString &status,
                            const QString &note) {
        const Player &p = *entry.player;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, ColStatus, new QTableWidgetItem(status));
        auto *nameItem = new QTableWidgetItem(p.name);
        nameItem->setData(Qt::UserRole, p.uid);
        m_table->setItem(row, ColName, nameItem);
        m_table->setItem(row, ColAge, new NumericItem(QString::number(p.age), p.age));
        m_table->setItem(row, ColPosition, new QTableWidgetItem(p.positionRaw));
        m_table->setItem(row, ColCategory, new QTableWidgetItem(categoryText(p)));
        m_table->setItem(row, ColNeed, new QTableWidgetItem(tierText(entry.tier)));
        m_table->setItem(row, ColDwrs, new NumericItem(QStringLiteral("%1%").arg(
                                                           qRound(entry.bestDwrs)),
                                                       entry.bestDwrs));
        m_table->setItem(row, ColSaved,
                         new QTableWidgetItem(listedFor(p, competition()) ? QStringLiteral("✓")
                                                                          : QString()));
        QStringList notes;
        if (!note.isEmpty())
            notes << note;
        if (Registration::u21Status(p) == Registration::U21Status::Uncertain)
            notes << tr("U21 unklar");
        m_table->setItem(row, ColNote, new QTableWidgetItem(notes.join(QStringLiteral(" · "))));
    };

    for (const auto &entry : proposal.listed)
        addRow(entry, tr("✅ Melden"), QString());
    for (const auto &entry : proposal.exempt)
        addRow(entry, uefa ? tr("🆓 Liste B") : tr("🆓 U21"), QString());
    for (const auto &[entry, reason] : proposal.leftOut)
        addRow(entry, tr("❌ Nicht melden"), reasonText(reason));
    m_table->resizeColumnsToContents();
    m_saveButton->setEnabled(!proposal.listed.empty());
}

void RegistrationPage::saveList()
{
    const Registration::Competition comp = competition();
    std::vector<std::pair<int, PlayerRegistration>> changes;
    QList<int> ids;
    for (const Player &player : m_context.store().players()) {
        const bool listed = m_proposedUids.contains(player.uid);
        if (listed == listedFor(player, comp))
            continue;
        PlayerRegistration r = player.registration;
        (comp == Registration::Competition::Uefa ? r.uefaListed : r.leagueListed) = listed;
        changes.push_back({player.id, r});
        ids << player.id;
    }
    if (changes.empty()) {
        QMessageBox::information(this, tr("Registrierung"),
                                 tr("Die gespeicherte Meldeliste ist bereits aktuell."));
        return;
    }
    if (!m_context.database().setRegistrations(changes)) {
        QMessageBox::critical(this, tr("Registrierung"), m_context.database().errorString());
        return;
    }
    m_context.refreshPlayers(ids); // re-reads the rows and refreshes the pages
    QMessageBox::information(this, tr("Registrierung"),
                             tr("Meldeliste gespeichert (%1 Spieler).").arg(m_proposedUids.size()));
}

} // namespace fm
