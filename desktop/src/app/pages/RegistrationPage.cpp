#include "RegistrationPage.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../widgets/NumericTableItem.h"
#include "PageHelpers.h"
#include "core/PlayerStatus.h"
#include "core/Utils.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fm {

namespace {

enum Column { ColListed, ColAdvice, ColName, ColAge, ColPosition, ColCategory, ColNeed, ColDwrs,
              ColNote, ColCount };

// Settings value behind each competition combo entry.
constexpr int kLeague = 0;
constexpr int kUefa = 1;

// The "registered" cell: a checkbox for players who need a slot, a plain label
// for those eligible without one. Sorts ticked first, then the exempt, then
// the unticked.
class ListedItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem &other) const override
    {
        return rank(*this) < rank(other);
    }

private:
    static int rank(const QTableWidgetItem &item)
    {
        if (!item.flags().testFlag(Qt::ItemIsUserCheckable))
            return 1;
        return item.checkState() == Qt::Checked ? 0 : 2;
    }
};

// Sizes the table to its rows, so the page scrolls as a whole instead of
// nesting a scrollbar per table.
void fitHeightToRows(QTableWidget *table)
{
    table->resizeRowsToContents();
    int height = table->horizontalHeader()->height() + 2 * table->frameWidth();
    for (int row = 0; row < table->rowCount(); ++row)
        height += table->rowHeight(row);
    table->setFixedHeight(height);
}

} // namespace

RegistrationPage::RegistrationPage(AppContext &context, QWidget *parent)
    : PageBase(context, parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Fixed head: competition, the quota counters and the buttons stay visible
    // while the long player tables scroll underneath.
    auto *head = new QWidget(this);
    auto *headLayout = new QVBoxLayout(head);
    headLayout->setContentsMargins(18, 14, 18, 6);
    headLayout->setSpacing(8);
    headLayout->addWidget(
        new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Registrierung")), head));
    m_controls = new QWidget(head);
    auto *controlsLayout = new QVBoxLayout(m_controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(8);
    auto *competitionRow = new QHBoxLayout;
    competitionRow->addWidget(new QLabel(tr("Wettbewerb:"), m_controls));
    m_competitionCombo = new QComboBox(m_controls);
    m_competitionCombo->setMinimumWidth(260);
    competitionRow->addWidget(m_competitionCombo);
    competitionRow->addStretch(1);
    m_adoptButton = new QPushButton(tr("Empfehlung übernehmen"), m_controls);
    m_adoptButton->setToolTip(tr("Setzt die Häkchen auf den Vorschlag des Assistenten."));
    competitionRow->addWidget(m_adoptButton);
    m_saveButton = new QPushButton(tr("Meldeliste speichern"), m_controls);
    m_saveButton->setToolTip(
        tr("Speichert die angehakten Spieler als gemeldete Liste dieses Wettbewerbs. Best XI "
           "nutzt sie, und spätere Änderungen (Transfers, neue Markierungen) werden sichtbar."));
    competitionRow->addWidget(m_saveButton);
    controlsLayout->addLayout(competitionRow);
    m_countsLabel = new QLabel(m_controls);
    m_countsLabel->setWordWrap(true);
    controlsLayout->addWidget(m_countsLabel);
    headLayout->addWidget(m_controls);
    outer->addWidget(head);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(m_scroll, 1);
    m_content = new QWidget;
    QWidget *content = m_content;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 6, 18, 14);
    layout->setSpacing(10);
    m_scroll->setWidget(content);

    m_hint = new QLabel(content);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

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

    m_usageHint = new QLabel(
        tr("Setze das Häkchen bei jedem Spieler, den du im Spiel wirklich gemeldet hast, und "
           "speichere die Liste — Best XI rechnet dann mit ihr. Vorbelegt ist die Empfehlung, "
           "nach dem Speichern deine Liste. Sortieren: Klick auf einen Spaltenkopf "
           "(Standard: Position, vom Tor zum Sturm, rechts vor links)."),
        content);
    m_usageHint->setWordWrap(true);
    m_usageHint->setObjectName(QStringLiteral("kpiCaption"));
    layout->addWidget(m_usageHint);

    const auto makeTable = [this](QGroupBox *box) {
        auto *table = new QTableWidget(box);
        table->setColumnCount(ColCount);
        table->setHorizontalHeaderLabels({tr("Gemeldet"), tr("Empfehlung"), tr("Name"),
                                          tr("Alter"), tr("Position"), tr("Ausbildung"),
                                          tr("Bedarf"), tr("DWRS"), tr("Hinweis")});
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setAlternatingRowColors(true);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Pitch order until the user picks another column; the choice then
        // survives the live refreshes.
        table->horizontalHeader()->setSortIndicator(ColPosition, Qt::AscendingOrder);
        table->setSortingEnabled(true);
        (new QVBoxLayout(box))->addWidget(table);
        // Right-click → Registrierung edits the flags; the page recomputes live.
        PlayerActions::attachToTableWidget(m_context, table, ColName, true, ColListed);
        connect(table, &QTableWidget::itemChanged, this, &RegistrationPage::onItemChanged);
        return table;
    };
    m_keeperBox = new QGroupBox(content);
    m_keeperTable = makeTable(m_keeperBox);
    layout->addWidget(m_keeperBox);
    m_outfieldBox = new QGroupBox(content);
    m_outfieldTable = makeTable(m_outfieldBox);
    layout->addWidget(m_outfieldBox);

    layout->addStretch(1);

    connect(m_competitionCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_updating)
            refresh();
    });
    connect(m_adoptButton, &QPushButton::clicked, this, &RegistrationPage::adoptProposal);
    connect(m_saveButton, &QPushButton::clicked, this, &RegistrationPage::saveList);
}

void RegistrationPage::releaseStoreRows()
{
    m_pool.clear();
    m_proposal = {};
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

    // Unsaved ticks belong to one database.
    if (m_pendingDb != m_context.currentDbName()) {
        m_pendingTicks.clear();
        m_pendingDb = m_context.currentDbName();
    }

    // Competition choice follows the enabled rule sets.
    m_updating = true;
    const int previous = m_competitionCombo->currentData().isValid()
                             ? m_competitionCombo->currentData().toInt()
                             : kLeague;
    m_competitionCombo->clear();
    if (Registration::restrictsSquad(settings.league))
        m_competitionCombo->addItem(Registration::leagueDisplayName(settings.league), kLeague);
    if (settings.uefa)
        m_competitionCombo->addItem(tr("Champions / Europa / Conference League (Liste A)"),
                                    kUefa);
    m_competitionCombo->setCurrentIndex(std::max(0, m_competitionCombo->findData(previous)));
    m_updating = false;

    const bool ready = settings.active() && !userClub.isEmpty();
    for (QWidget *w : {m_controls, static_cast<QWidget *>(m_keeperBox),
                       static_cast<QWidget *>(m_outfieldBox),
                       static_cast<QWidget *>(m_summaryLabel), static_cast<QWidget *>(m_costLabel),
                       static_cast<QWidget *>(m_warningLabel), static_cast<QWidget *>(m_usageHint)})
        w->setVisible(ready);
    if (!ready) {
        releaseStoreRows();
        m_hint->setText(
            settings.active()
                ? tr("Bitte wähle zuerst deinen Verein (Dashboard oder Einstellungen).")
                : tr("⚠️ Keine Registrierungsregeln aktiv. Wähle sie unter "
                     "Einstellungen → Verein → Registrierungsregeln."));
        return;
    }

    const Registration::Competition comp = competition();
    m_pool = pool();
    m_tactics = rankingTactics(&m_tacticFallback);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto ranked = Registration::rankPool(m_context.squadBuilder(), m_context.definitions(),
                                               m_pool, m_tactics, m_context.ratings());
    m_quota = Registration::quotaFor(comp, settings);
    m_proposal = Registration::propose(ranked, comp, m_quota);
    QApplication::restoreOverrideCursor();

    m_hint->setText(
        tr("Pool: Erste Mannschaft + Zweitteam (%1 Spieler, ohne Retired). Rangfolge aus "
           "Startelf, B-Team und Kadertiefe von: %2. Markierungen änderst du per Rechtsklick "
           "→ Registrierung — der Vorschlag rechnet sofort neu.")
            .arg(m_pool.size())
            .arg(m_tactics.isEmpty() ? tr("keine Taktik vorhanden")
                                     : m_tactics.join(QStringLiteral(", "))));

    m_proposedUids.clear();
    m_checkableUids.clear();
    for (const auto &entry : m_proposal.listed) {
        m_proposedUids.insert(entry.player->uid);
        m_checkableUids.insert(entry.player->uid);
    }
    for (const auto &[entry, reason] : m_proposal.leftOut)
        m_checkableUids.insert(entry.player->uid);
    // The saved list may still name players who have left the pool since.
    m_savedUids.clear();
    for (const Player &player : m_context.store().players())
        if (Registration::isListed(player, comp))
            m_savedUids.insert(player.uid);

    // Ticks: unsaved edits first, else the saved list, else the proposal.
    const auto pending = m_pendingTicks.constFind(m_competitionCombo->currentData().toInt());
    if (pending != m_pendingTicks.constEnd())
        m_checkedUids = pending.value();
    else
        m_checkedUids = m_savedUids.isEmpty() ? m_proposedUids : m_savedUids;
    m_checkedUids.intersect(m_checkableUids);

    fillTables();
    updateSelection();
}

void RegistrationPage::fillTables()
{
    const bool uefa = competition() == Registration::Competition::Uefa;
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

    std::vector<Row> rows;
    for (const auto &entry : m_proposal.listed)
        rows.push_back({entry, Advice::List, QString(), 0});
    for (const auto &entry : m_proposal.exempt)
        rows.push_back({entry, Advice::Exempt, QString(), 0});
    for (const auto &[entry, reason] : m_proposal.leftOut)
        rows.push_back({entry, Advice::LeaveOut, reasonText(reason), 0});

    // Default order: across the pitch, the better player first within a position.
    QHash<QString, int> positionKeys; // position strings repeat across the squad
    const auto positionKey = [&positionKeys](const Player &p) {
        auto it = positionKeys.find(p.positionRaw);
        if (it == positionKeys.end())
            it = positionKeys.insert(p.positionRaw, positionSortKey(p.positionRaw));
        return it.value();
    };
    std::stable_sort(rows.begin(), rows.end(), [&positionKey](const Row &a, const Row &b) {
        const int keyA = positionKey(*a.entry.player);
        const int keyB = positionKey(*b.entry.player);
        if (keyA != keyB)
            return keyA < keyB;
        // Same spot on the pitch: keep identical position strings together.
        if (a.entry.player->positionRaw != b.entry.player->positionRaw)
            return a.entry.player->positionRaw < b.entry.player->positionRaw;
        if (a.entry.bestDwrs != b.entry.bestDwrs)
            return a.entry.bestDwrs > b.entry.bestDwrs;
        return a.entry.player->name < b.entry.player->name;
    });
    std::vector<Row> keepers, outfield;
    for (size_t i = 0; i < rows.size(); ++i) {
        rows[i].order = static_cast<int>(i);
        (rows[i].entry.player->isGoalkeeper() ? keepers : outfield).push_back(rows[i]);
    }

    m_updating = true;
    fillTable(m_keeperTable, keepers);
    fillTable(m_outfieldTable, outfield);
    m_updating = false;

    const QString exemptLabel = uefa ? tr("Liste B") : tr("U21");
    for (QTableWidget *table : {m_keeperTable, m_outfieldTable}) {
        table->horizontalHeaderItem(ColListed)->setToolTip(
            tr("Im Spiel gemeldet. '%1' ist ohne Platz spielberechtigt.").arg(exemptLabel));
    }
    m_keeperBox->setTitle(tr("Torhüter (%1)").arg(keepers.size()));
    m_outfieldBox->setTitle(tr("Feldspieler (%1)").arg(outfield.size()));
    m_keeperBox->setVisible(!keepers.empty());
    m_outfieldBox->setVisible(!outfield.empty());
}

void RegistrationPage::fillTable(QTableWidget *table, const std::vector<Row> &rows)
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
    const auto adviceText = [uefa](Advice advice) {
        switch (advice) {
        case Advice::List:
            return tr("✅ Melden");
        case Advice::Exempt:
            return uefa ? tr("🆓 Liste B") : tr("🆓 U21");
        case Advice::LeaveOut:
            break;
        }
        return tr("❌ Nicht melden");
    };

    // Filling an auto-sorting table would re-sort on every cell.
    table->setSortingEnabled(false);
    table->clearContents();
    table->setRowCount(static_cast<int>(rows.size()));
    int row = 0;
    for (const Row &entry : rows) {
        const Player &p = *entry.entry.player;

        auto *listedItem = new ListedItem;
        if (entry.advice == Advice::Exempt) {
            listedItem->setText(uefa ? tr("Liste B") : tr("U21"));
            listedItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            listedItem->setToolTip(tr("Ohne Platz auf der Meldeliste spielberechtigt."));
        } else {
            listedItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                 | Qt::ItemIsUserCheckable);
            listedItem->setCheckState(m_checkedUids.contains(p.uid) ? Qt::Checked
                                                                    : Qt::Unchecked);
        }
        table->setItem(row, ColListed, listedItem);
        table->setItem(row, ColAdvice,
                       new NumericItem(adviceText(entry.advice), static_cast<int>(entry.advice)));
        auto *nameItem = new QTableWidgetItem(p.name);
        nameItem->setData(Qt::UserRole, p.uid);
        table->setItem(row, ColName, nameItem);
        table->setItem(row, ColAge, new NumericItem(QString::number(p.age), p.age));
        table->setItem(row, ColPosition, new NumericItem(p.positionRaw, entry.order));
        table->setItem(row, ColCategory, new QTableWidgetItem(categoryText(p)));
        table->setItem(row, ColNeed,
                       new NumericItem(tierText(entry.entry.tier),
                                       static_cast<int>(entry.entry.tier)));
        auto *dwrsItem = new NumericItem(QStringLiteral("%1%").arg(qRound(entry.entry.bestDwrs)),
                                         entry.entry.bestDwrs);
        if (entry.entry.bestDwrs > 0.0) {
            const CellStyle style = dwrsCellStyle(entry.entry.bestDwrs);
            dwrsItem->setBackground(style.background);
            dwrsItem->setForeground(style.text);
        }
        table->setItem(row, ColDwrs, dwrsItem);
        QStringList notes;
        if (!entry.note.isEmpty())
            notes << entry.note;
        if (Registration::u21Status(p) == Registration::U21Status::Uncertain)
            notes << tr("U21 unklar");
        table->setItem(row, ColNote, new QTableWidgetItem(notes.join(QStringLiteral(" · "))));
        ++row;
    }
    table->setSortingEnabled(true); // re-applies the header's current sort
    table->resizeColumnsToContents();
    // Room for the cell padding and for the sort arrow next to a header text.
    for (int column = 0; column < ColCount - 1; ++column)
        table->setColumnWidth(column, table->columnWidth(column) + 22);
    table->horizontalHeader()->setStretchLastSection(true);
    fitHeightToRows(table);
}

void RegistrationPage::onItemChanged(QTableWidgetItem *item)
{
    if (m_updating || item->column() != ColListed
        || !item->flags().testFlag(Qt::ItemIsUserCheckable))
        return;
    const QTableWidgetItem *nameItem = item->tableWidget()->item(item->row(), ColName);
    if (!nameItem)
        return;
    const QString uid = nameItem->data(Qt::UserRole).toString();
    if (item->checkState() == Qt::Checked)
        m_checkedUids.insert(uid);
    else
        m_checkedUids.remove(uid);
    storePendingTicks();

    // The texts above the tables grow and shrink with the ticks. Settle the
    // layout right away and scroll by the same amount, so the row just clicked
    // stays under the cursor instead of jumping away before the next click.
    const QWidget *anchor = item->tableWidget();
    const int before = anchor->mapTo(m_content, QPoint(0, 0)).y();
    updateSelection();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    const int shift = anchor->mapTo(m_content, QPoint(0, 0)).y() - before;
    if (shift != 0) {
        QScrollBar *bar = m_scroll->verticalScrollBar();
        bar->setValue(bar->value() + shift);
    }
}

void RegistrationPage::storePendingTicks()
{
    QSet<QString> initial = m_savedUids.isEmpty() ? m_proposedUids : m_savedUids;
    initial.intersect(m_checkableUids);
    const int key = m_competitionCombo->currentData().toInt();
    if (m_checkedUids == initial)
        m_pendingTicks.remove(key);
    else
        m_pendingTicks.insert(key, m_checkedUids);
}

void RegistrationPage::adoptProposal()
{
    m_checkedUids = m_proposedUids;
    storePendingTicks();

    m_updating = true;
    for (QTableWidget *table : {m_keeperTable, m_outfieldTable}) {
        // Ticking re-sorts a table ordered by this column; walk it unsorted.
        table->setSortingEnabled(false);
        for (int row = 0; row < table->rowCount(); ++row) {
            QTableWidgetItem *item = table->item(row, ColListed);
            if (!item->flags().testFlag(Qt::ItemIsUserCheckable))
                continue;
            const QString uid = table->item(row, ColName)->data(Qt::UserRole).toString();
            item->setCheckState(m_checkedUids.contains(uid) ? Qt::Checked : Qt::Unchecked);
        }
        table->setSortingEnabled(true);
    }
    m_updating = false;
    updateSelection();
}

void RegistrationPage::updateSelection()
{
    const bool uefa = competition() == Registration::Competition::Uefa;
    const auto namesOf = [this](const QSet<QString> &uids) {
        QStringList names;
        for (const Player *p : m_pool)
            if (uids.contains(p->uid))
                names << p->name;
        return names.join(QStringLiteral(", "));
    };

    std::vector<const Player *> listed, exempt;
    for (const Player *p : m_pool)
        if (m_checkedUids.contains(p->uid))
            listed.push_back(p);
    for (const auto &entry : m_proposal.exempt)
        exempt.push_back(entry.player);
    const Registration::ListCheck check = Registration::checkList(listed, exempt, m_quota);
    const bool asProposed = m_checkedUids == m_proposedUids;

    // --- Counters (fixed head); a broken limit is marked in place ---
    const auto marked = [](const QString &text, bool broken) {
        return broken ? QStringLiteral("<span style=\"color:#e5484d;\">⚠️ %1</span>").arg(text)
                      : text;
    };
    QStringList parts;
    parts << marked(tr("<b>Meldeliste:</b> %1 / %2").arg(check.size).arg(m_quota.maxSize),
                    check.tooMany);
    parts << marked(tr("Nicht-HG %1 / %2").arg(check.nonHomeGrown).arg(m_quota.maxNonHomeGrown),
                    check.tooManyNonHomeGrown);
    parts << tr("Home-Grown %1").arg(check.homeGrownOnly);
    parts << marked(tr("Club-Grown %1").arg(check.clubTrained), check.tooManyNonClubTrained);
    parts << marked(
        tr("Torhüter %1 (min. %2)").arg(check.goalkeepers).arg(m_quota.minGoalkeepers),
        check.tooFewGoalkeepers);
    parts << (uefa ? tr("<b>Liste B</b> (ohne Platz): %1 Spieler")
                   : tr("<b>U21</b> (ohne Platz): %1 Spieler"))
                 .arg(exempt.size());
    m_countsLabel->setText(parts.join(QStringLiteral(" · ")));

    // --- Saved list and recommendation ---
    QStringList lines;
    if (m_savedUids.isEmpty()) {
        lines << (asProposed ? tr("Noch keine Meldeliste gespeichert — die Häkchen entsprechen "
                                  "der Empfehlung.")
                             : tr("Noch keine Meldeliste gespeichert."));
    } else if (m_checkedUids == m_savedUids) {
        lines << tr("Entspricht der gespeicherten Meldeliste.");
    } else {
        lines << tr("Ungespeicherte Änderungen gegenüber der gespeicherten Liste: <b>%1</b> neu "
                    "gemeldet, <b>%2</b> abgemeldet.")
                     .arg((m_checkedUids - m_savedUids).size())
                     .arg((m_savedUids - m_checkedUids).size());
    }
    if (!asProposed) {
        const QSet<QString> missing = m_proposedUids - m_checkedUids;
        const QSet<QString> extra = m_checkedUids - m_proposedUids;
        if (!missing.isEmpty())
            lines << tr("Empfohlen, aber nicht gemeldet: %1").arg(namesOf(missing));
        if (!extra.isEmpty())
            lines << tr("Gemeldet, aber nicht empfohlen: %1").arg(namesOf(extra));
    }
    m_summaryLabel->setText(lines.join(QStringLiteral("<br>")));

    // --- Cost of the quota ---
    std::vector<const Player *> eligible = listed;
    eligible.insert(eligible.end(), exempt.begin(), exempt.end());
    const auto costs = Registration::quotaCosts(m_context.squadBuilder(), m_context.definitions(),
                                                m_pool, eligible, m_tactics, m_context.ratings());
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
    if (m_tacticFallback)
        warnings << tr("Keine Lieblingstaktik gesetzt — Rangfolge aus '%1'. Lege sie unter "
                       "Einstellungen → Verein fest.")
                        .arg(m_tactics.value(0));
    if (check.tooMany)
        warnings << tr("Zu viele Spieler gemeldet: %1 (höchstens %2).")
                        .arg(check.size)
                        .arg(m_quota.maxSize);
    if (check.tooManyNonHomeGrown)
        warnings << tr("Zu viele Nicht-Home-Grown gemeldet: %1 (höchstens %2).")
                        .arg(check.nonHomeGrown)
                        .arg(m_quota.maxNonHomeGrown);
    if (check.tooManyNonClubTrained)
        warnings << tr("Zu viele Spieler ohne Club-Grown gemeldet: %1 (höchstens %2).")
                        .arg(check.nonHomeGrown + check.homeGrownOnly)
                        .arg(m_quota.maxNonClubTrained);
    const int freeSlots = m_quota.maxSize - check.size;
    if (freeSlots > 0 && asProposed) {
        QString why;
        if (check.nonHomeGrown >= m_quota.maxNonHomeGrown
            && check.nonHomeGrown + check.homeGrownOnly >= m_quota.maxNonClubTrained)
            why = tr("nur Club-Grown-Spieler dürften sie belegen, und es gibt keine weiteren.");
        else if (check.nonHomeGrown >= m_quota.maxNonHomeGrown)
            why = tr("nur Home-Grown-Spieler dürften sie belegen, und es gibt keine weiteren.");
        else
            why = tr("es gibt keine weiteren Spieler im Pool.");
        warnings << tr("%1 Plätze bleiben frei: %2").arg(freeSlots).arg(why);
    } else if (freeSlots > 0) {
        warnings << tr("%1 Plätze sind noch frei.").arg(freeSlots);
    }
    if (check.tooFewGoalkeepers)
        warnings << tr("Nur %1 Torhüter gemeldet (gewünscht: %2).")
                        .arg(check.goalkeepers)
                        .arg(m_quota.minGoalkeepers);
    QStringList uncertain;
    for (const Player *p : m_pool)
        if (Registration::u21Status(*p) == Registration::U21Status::Uncertain)
            uncertain << p->name;
    if (!uncertain.isEmpty())
        warnings << tr("U21 unklar (Alter 21 oder unbekannt), zählt vorsichtshalber als nicht "
                       "U21: %1. Per Rechtsklick → Registrierung festlegen.")
                        .arg(uncertain.join(QStringLiteral(", ")));
    // Saved entries without a checkbox any more: sold, retired, or now exempt.
    QStringList gone;
    for (const QString &uid : m_savedUids - m_checkableUids)
        if (const Player *p = m_context.store().findByUid(uid))
            gone << p->name;
    if (!gone.isEmpty()) {
        gone.sort();
        warnings << tr("Steht noch auf der gespeicherten Liste, braucht aber keinen Platz mehr "
                       "oder ist nicht mehr im Kader — wird beim Speichern entfernt: %1")
                        .arg(gone.join(QStringLiteral(", ")));
    }
    m_warningLabel->setText(warnings.isEmpty()
                                ? QString()
                                : QStringLiteral("⚠️ ")
                                      + warnings.join(QStringLiteral("<br>⚠️ ")));
    m_warningLabel->setVisible(!warnings.isEmpty());

    m_adoptButton->setEnabled(!asProposed);
    m_saveButton->setEnabled(m_checkedUids != m_savedUids);
}

void RegistrationPage::saveList()
{
    const Registration::Competition comp = competition();

    std::vector<const Player *> listed, exempt;
    for (const Player *p : m_pool)
        if (m_checkedUids.contains(p->uid))
            listed.push_back(p);
    for (const auto &entry : m_proposal.exempt)
        exempt.push_back(entry.player);
    const Registration::ListCheck check = Registration::checkList(listed, exempt, m_quota);
    if ((check.tooMany || check.tooManyNonHomeGrown || check.tooManyNonClubTrained)
        && QMessageBox::question(this, tr("Registrierung"),
                                 tr("Die Auswahl verletzt die Registrierungsregeln (siehe "
                                    "Warnungen). Trotzdem speichern?"))
               != QMessageBox::Yes)
        return;

    std::vector<std::pair<int, PlayerRegistration>> changes;
    QList<int> ids;
    for (const Player &player : m_context.store().players()) {
        const bool isListed = m_checkedUids.contains(player.uid);
        if (isListed == Registration::isListed(player, comp))
            continue;
        PlayerRegistration r = player.registration;
        (comp == Registration::Competition::Uefa ? r.uefaListed : r.leagueListed) = isListed;
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
    const int count = static_cast<int>(m_checkedUids.size());
    m_pendingTicks.remove(m_competitionCombo->currentData().toInt());
    m_context.refreshPlayers(ids); // re-reads the rows and refreshes the pages
    QMessageBox::information(this, tr("Registrierung"),
                             tr("Meldeliste gespeichert (%1 Spieler).").arg(count));
}

} // namespace fm
