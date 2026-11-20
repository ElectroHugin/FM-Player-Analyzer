#include "SigningAdvisorTab.h"
#include "core/Currency.h"
#include "core/DwrsDisplay.h"

#include "PageHelpers.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../widgets/NumericTableItem.h"
#include "core/PlayerStatus.h"
#include "core/Utils.h"
#include "core/GameTerms.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace fm {

namespace {

// Per-database filter settings.
const QString kBudgetKey = QStringLiteral("signing_budget_millions"); // empty = no limit
const QString kMaxAgeKey = QStringLiteral("signing_max_age");
const QString kShortlistKey = QStringLiteral("signing_shortlist_only");

constexpr int kNeedIndexRole = Qt::UserRole + 1;

// "+3" / "−1" / "+2.5": whole DWRS points unless the sum has a fraction.
QString gainText(double gain)
{
    const double magnitude = std::abs(gain);
    const bool whole = std::abs(magnitude - std::round(magnitude)) < 0.05;
    const QString number = QLocale().toString(magnitude, 'f', whole ? 0 : 1);
    return (gain <= -0.05 ? QStringLiteral("−") : QStringLiteral("+")) + number;
}

// Green, stronger for a bigger share of the largest gain.
QColor gainColor(double gain, double maxGain)
{
    const double share = maxGain > 0.0 ? std::clamp(gain / maxGain, 0.0, 1.0) : 0.0;
    return QColor(40, 150, 70, static_cast<int>(60 + share * 170));
}

QTableWidget *makeTable(QWidget *parent, const QStringList &headers)
{
    auto *table = new QTableWidget(parent);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    // Columns follow their content (header and sort arrow included).
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return table;
}

// Exactly as high as its rows: no inner scrollbar, no dead space.
void fitHeightToRows(QTableWidget *table)
{
    int height = table->horizontalHeader()->sizeHint().height() + 2 * table->frameWidth();
    for (int row = 0; row < table->rowCount(); ++row)
        height += table->rowHeight(row);
    table->setFixedHeight(height);
}

QString gapTypeLabel(const Gap &gap)
{
    if (gap.playerName == QLatin1String("—") || gap.playerName == QLatin1String("-"))
        return SigningAdvisorTab::tr("⛔ Unfilled");
    if (gap.isDisplacement && gap.isDropoff)
        return SigningAdvisorTab::tr("🕳️⚠️ Hidden + Obvious");
    if (gap.isDisplacement)
        return SigningAdvisorTab::tr("🕳️ Hidden");
    if (gap.isDropoff)
        return SigningAdvisorTab::tr("⚠️ Obvious");
    return QString();
}

// Collapsible section header in the style of Best XI's "provably optimal" button.
QToolButton *makeToggle(QWidget *parent, const QString &text, const QString &toolTip)
{
    auto *toggle = new QToolButton(parent);
    toggle->setText(text);
    toggle->setToolTip(toolTip);
    toggle->setCheckable(true);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    return toggle;
}

} // namespace

SigningAdvisorTab::SigningAdvisorTab(AppContext &context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // --- Market filters ---
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Budget (market value up to):"), this));
    m_budgetSpin = new QDoubleSpinBox(this);
    m_budgetSpin->setRange(0.0, 2000.0);
    m_budgetSpin->setDecimals(1);
    m_budgetSpin->setSingleStep(5.0);
    m_budgetSpin->setSuffix(tr(" M"));
    m_budgetSpin->setToolTip(tr("Highest market value of a candidate (lower end of the range as "
                                "FM exports it). 0 = free transfers only."));
    filterRow->addWidget(m_budgetSpin);
    m_noLimitCheck = new QCheckBox(tr("No limit"), this);
    filterRow->addWidget(m_noLimitCheck);
    filterRow->addSpacing(16);
    filterRow->addWidget(new QLabel(tr("Max age:"), this));
    m_maxAgeSpin = new QSpinBox(this);
    m_maxAgeSpin->setRange(15, 45);
    filterRow->addWidget(m_maxAgeSpin);
    filterRow->addSpacing(16);
    m_shortlistOnlyCheck = new QCheckBox(tr("Shortlist only"), this);
    filterRow->addWidget(m_shortlistOnlyCheck);
    filterRow->addStretch(1);
    m_hintToggle = makeToggle(this, tr("How is this calculated?"),
                              tr("Show or hide the explanation"));
    filterRow->addWidget(m_hintToggle);
    layout->addLayout(filterRow);

    m_hint = new QLabel(
        tr("For every position the best players of the filtered market are added to the squad on "
           "trial, and the starting XI and B-team are lined up again, provably optimally, each "
           "time (as on 'First team'). 'XI +' is how much the starting XI's total DWRS rises — "
           "with every knock-on effect: if a versatile player moves to a better position thanks "
           "to the newcomer, that counts too (column 'Knock-on'). A position that merely looks "
           "weak, but for which the market offers nobody better, therefore does not end up on "
           "top. 'Better on market' counts how many players of the filtered market are better in "
           "the role than your current one. Signings are calculated without an agreed playing "
           "time; 'Not for Sale' players are left out."),
        this);
    m_hint->setWordWrap(true);
    m_hint->setObjectName(QStringLiteral("kpiCaption"));
    m_hint->setVisible(false);
    layout->addWidget(m_hint);
    connect(m_hintToggle, &QToolButton::toggled, this, [this](bool shown) {
        m_hint->setVisible(shown);
        m_hintToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
    });

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    layout->addWidget(m_summary);

    // --- One row per XI slot, biggest gain first ---
    m_needsTable = makeTable(this, {tr("Slot"), tr("Role"), tr("Current"), tr("DWRS"),
                                    tr("XI +"), tr("Best signing"), tr("DWRS"),
                                    tr("Market Value"), tr("B-team +"), tr("Better on market")});
    m_needsTable->horizontalHeaderItem(9)->setToolTip(
        tr("Players of the filtered market who are better in the role than your current one "
           "— out of all who can play the position."));
    layout->addWidget(m_needsTable);
    // Current player and best signing each carry their own uid.
    PlayerActions::attachToTableWidget(m_context, m_needsTable, -1);
    connect(m_needsTable, &QTableWidget::itemSelectionChanged, this,
            &SigningAdvisorTab::showOptions);

    // --- Signings for the selected slot ---
    m_optionsTitle = new QLabel(this);
    m_optionsTitle->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(m_optionsTitle);
    m_optionsTable = makeTable(this, {tr("Name"), tr("Age"), tr("Club"), tr("Position"),
                                      tr("DWRS"), tr("Market Value"), tr("Wage"),
                                      tr("Personality"), tr("XI +"), tr("B-team +"),
                                      tr("Knock-on")});
    m_optionsTable->setSortingEnabled(true);
    layout->addWidget(m_optionsTable);
    PlayerActions::attachToTableWidget(m_context, m_optionsTable);

    // --- Legacy gap analysis, collapsed ---
    m_gapToggle = makeToggle(this, tr("Gap analysis: line-up anomalies"),
                             tr("Starters clearly below the team median (obvious) or not in "
                                "their best position or on their side (hidden) — without "
                                "looking at the market."));
    layout->addWidget(m_gapToggle, 0, Qt::AlignLeft);
    m_gapBody = new QWidget(this);
    m_gapBody->setVisible(false);
    {
        auto *gapLayout = new QVBoxLayout(m_gapBody);
        gapLayout->setContentsMargins(0, 0, 0, 0);
        m_gapCaption = new QLabel(m_gapBody);
        m_gapCaption->setObjectName(QStringLiteral("kpiCaption"));
        m_gapCaption->setWordWrap(true);
        gapLayout->addWidget(m_gapCaption);
        const QStringList headers{tr("Slot"),  tr("Role"),     tr("Player"), tr("DWRS"),
                                  tr("Type"),   tr("Gap Score"), tr("Why")};
        for (auto [status, table, title] :
             {std::tuple{&m_xiGapStatus, &m_xiGapTable, tr("Starting XI")},
              std::tuple{&m_bGapStatus, &m_bGapTable, tr("B-Team")}}) {
            auto *label = new QLabel(title, m_gapBody);
            label->setObjectName(QStringLiteral("sectionTitle"));
            gapLayout->addWidget(label);
            *status = new QLabel(m_gapBody);
            gapLayout->addWidget(*status);
            *table = makeTable(m_gapBody, headers);
            (*table)->setSortingEnabled(true);
            gapLayout->addWidget(*table);
        }
    }
    layout->addWidget(m_gapBody);
    connect(m_gapToggle, &QToolButton::toggled, this, [this](bool shown) {
        m_gapBody->setVisible(shown);
        m_gapToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
    });
    layout->addStretch(1);

    // Filter changes: recompute shortly after the last one.
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        saveFilters();
        recompute();
    });
    const auto filtersChanged = [this] {
        m_budgetSpin->setEnabled(!m_noLimitCheck->isChecked());
        if (!m_loading)
            m_debounce->start();
    };
    connect(m_budgetSpin, &QDoubleSpinBox::valueChanged, this, filtersChanged);
    connect(m_noLimitCheck, &QCheckBox::toggled, this, filtersChanged);
    connect(m_maxAgeSpin, &QSpinBox::valueChanged, this, filtersChanged);
    connect(m_shortlistOnlyCheck, &QCheckBox::toggled, this, filtersChanged);

    // The filters are stored per database.
    loadFilters();
    connect(&m_context, &AppContext::databaseChanged, this, [this] {
        m_debounce->stop();
        loadFilters();
        m_dirty = true;
        // Visible during the switch: the page refresh before this ran with the
        // previous save's filters, so recompute with the loaded ones.
        if (isVisible())
            m_debounce->start();
    });
}

void SigningAdvisorTab::setSquad(const QString &tactic, std::vector<const Player *> squad)
{
    m_tactic = tactic;
    m_squad = std::move(squad);
    m_dirty = true;
    if (isVisible())
        recompute();
}

void SigningAdvisorTab::clear()
{
    m_squad.clear();
    m_analysis = SigningAnalysis();
    m_needsTable->setRowCount(0);
    m_optionsTable->setRowCount(0);
    m_xiGapTable->setRowCount(0);
    m_bGapTable->setRowCount(0);
    m_dirty = true;
}

void SigningAdvisorTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_dirty)
        recompute();
}

void SigningAdvisorTab::loadFilters()
{
    Database &db = m_context.database();
    m_loading = true;
    const QString budget = db.setting(kBudgetKey);
    m_noLimitCheck->setChecked(budget.isEmpty());
    m_budgetSpin->setValue(budget.isEmpty() ? 50.0 : budget.toDouble());
    m_budgetSpin->setEnabled(!budget.isEmpty());
    m_maxAgeSpin->setValue(db.setting(kMaxAgeKey, QStringLiteral("45")).toInt());
    m_shortlistOnlyCheck->setChecked(db.setting(kShortlistKey) == QLatin1String("1"));
    m_loading = false;
}

void SigningAdvisorTab::saveFilters()
{
    Database &db = m_context.database();
    db.setSetting(kBudgetKey, m_noLimitCheck->isChecked()
                                  ? QString()
                                  : QString::number(m_budgetSpin->value(), 'f', 1));
    db.setSetting(kMaxAgeKey, QString::number(m_maxAgeSpin->value()));
    db.setSetting(kShortlistKey,
                  m_shortlistOnlyCheck->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
}

std::vector<const Player *> SigningAdvisorTab::marketPool() const
{
    const QString userClub = m_context.userClub();
    const QString secondClub = m_context.secondTeamClub();
    const PlayerStatus::FreshnessContext freshness = m_context.freshnessContext();
    const bool noLimit = m_noLimitCheck->isChecked();
    const double budget = m_budgetSpin->value() * 1'000'000.0;
    const int maxAge = m_maxAgeSpin->value();
    const bool shortlistOnly = m_shortlistOnlyCheck->isChecked();

    std::vector<const Player *> market;
    for (const Player &player : m_context.store().players()) {
        if (player.club == userClub || (!secondClub.isEmpty() && player.club == secondClub))
            continue;
        if (shortlistOnly && !player.onShortlist)
            continue;
        if (player.age > maxAge)
            continue;
        // "Not for Sale" is never on the market; otherwise the budget caps.
        if (player.transferValue >= kUnbuyableValue
            || (!noLimit && player.transferValue > budget))
            continue;
        if (PlayerStatus::isInactive(player, freshness))
            continue;
        market.push_back(&player);
    }
    return market;
}

void SigningAdvisorTab::recompute()
{
    m_dirty = false;
    m_debounce->stop();

    if (m_tactic.isEmpty() || m_squad.empty()) {
        m_analysis = SigningAnalysis();
        m_summary->setText(tr("Please choose your club first (Overview or Settings)."));
        m_needsTable->setRowCount(0);
        m_optionsTable->setRowCount(0);
        m_optionsTitle->clear();
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto positions = m_context.definitions().tacticRoles().value(m_tactic);
    const QStringList slotOrder = m_context.definitions().tacticSlotOrder(m_tactic);
    const RoleRatings &ratings = m_context.ratings();
    // The user's manual changes to the line-up (Best XI page) count here too.
    const SquadBuilder::Bridges bridges = loadManualBridges(m_context, m_tactic, &m_squad);
    m_analysis = SigningAdvisor::analyze(m_context.squadBuilder(), m_squad, marketPool(),
                                         positions, slotOrder, ratings, 12, 15, m_tactic,
                                         bridges);
    fillNeeds();
    fillGapTables(m_context.squadBuilder().calculateSquadForTactic(m_squad, m_tactic, ratings,
                                                                   true, bridges));
    QApplication::restoreOverrideCursor();
}

void SigningAdvisorTab::fillNeeds()
{
    const auto roleNames = m_context.definitions().roleDisplayMap();
    const std::vector<SlotNeed> &needs = m_analysis.needs;

    // --- Summary: the biggest lever and the next ones ---
    QString summary = tr("Filtered market: %1 players, %2 of them simulated as a signing.")
                          .arg(m_analysis.marketSize)
                          .arg(m_analysis.simulated);
    if (needs.empty() || needs.front().bestGain() <= 0.0) {
        summary += QStringLiteral("<br>")
                   + tr("With these filters the market offers nobody who improves the "
                        "starting XI.");
    } else {
        const SlotNeed &top = needs.front();
        const SigningOption &best = top.options.front();
        summary += QStringLiteral("<br>")
                   + tr("Biggest lever: <b>%1 · %2</b> — %3 (%4, %5) adds %6 DWRS to the starting XI.")
                         .arg(top.slot, roleNames.value(top.role, top.role).toHtmlEscaped(),
                              best.player->name.toHtmlEscaped(),
                              best.player->club.toHtmlEscaped(),
                              Currency::displayText(best.player->transferValueRaw).toHtmlEscaped(),
                              gainText(best.xiGain));
        QStringList next;
        for (size_t i = 1; i < needs.size() && next.size() < 3; ++i) {
            if (needs[i].bestGain() > 0.0)
                next << QStringLiteral("%1 (%2)").arg(needs[i].slot, gainText(needs[i].bestGain()));
        }
        if (!next.isEmpty())
            summary += QLatin1Char(' ') + tr("Then: %1.").arg(next.join(QStringLiteral(", ")));
    }
    m_summary->setText(summary);

    double maxGain = 0.0;
    for (const SlotNeed &need : needs)
        maxGain = std::max(maxGain, need.bestGain());

    const auto dwrsItem = [](double value, bool known) {
        auto *item = new NumericItem(known ? formatDwrsPercent(value)
                                           : QStringLiteral("–"),
                                     known ? value : -1.0);
        const CellStyle style = dwrsCellStyle(value);
        if (known && style.isValid()) {
            item->setBackground(style.background);
            item->setForeground(style.text);
        }
        return item;
    };

    const int previousRow = m_needsTable->currentRow();
    m_needsTable->blockSignals(true);
    m_needsTable->clearContents();
    m_needsTable->setRowCount(static_cast<int>(needs.size()));
    int row = 0;
    for (const SlotNeed &need : needs) {
        auto *slotItem = new QTableWidgetItem(need.slot);
        slotItem->setData(kNeedIndexRole, row);
        m_needsTable->setItem(row, 0, slotItem);
        m_needsTable->setItem(row, 1, new QTableWidgetItem(roleNames.value(need.role, need.role)));
        auto *currentItem = new QTableWidgetItem(need.current.isFilled() ? need.current.name
                                                                        : tr("— unfilled —"));
        currentItem->setData(Qt::UserRole, need.current.playerUid);
        m_needsTable->setItem(row, 2, currentItem);
        m_needsTable->setItem(row, 3, dwrsItem(need.currentRating, need.current.isFilled()));

        if (need.options.empty()) {
            auto *none = new NumericItem(QStringLiteral("–"), 0.0);
            m_needsTable->setItem(row, 4, none);
            m_needsTable->setItem(row, 5, new QTableWidgetItem(tr("nobody better")));
            m_needsTable->setItem(row, 6, new QTableWidgetItem());
            m_needsTable->setItem(row, 7, new QTableWidgetItem());
            m_needsTable->setItem(row, 8, new QTableWidgetItem());
        } else {
            const SigningOption &best = need.options.front();
            auto *gainItem = new NumericItem(gainText(best.xiGain), best.xiGain);
            gainItem->setBackground(gainColor(best.xiGain, maxGain));
            gainItem->setForeground(QColor(Qt::white));
            m_needsTable->setItem(row, 4, gainItem);
            auto *bestItem = new QTableWidgetItem(best.player->name);
            bestItem->setData(Qt::UserRole, best.player->uid);
            m_needsTable->setItem(row, 5, bestItem);
            m_needsTable->setItem(row, 6, dwrsItem(best.rating, true));
            m_needsTable->setItem(row, 7, new NumericItem(Currency::displayText(best.player->transferValueRaw),
                                                          best.player->transferValue));
            m_needsTable->setItem(row, 8, new NumericItem(gainText(best.bTeamGain),
                                                          best.bTeamGain));
        }
        m_needsTable->setItem(row, 9,
                              new NumericItem(tr("%1 of %2").arg(need.marketBetter)
                                                  .arg(need.marketCount),
                                              need.marketBetter));
        ++row;
    }
    fitHeightToRows(m_needsTable);
    m_needsTable->blockSignals(false);
    m_needsTable->selectRow(previousRow >= 0 && previousRow < row ? previousRow : 0);
    showOptions();
}

QString SigningAdvisorTab::moveText(const SquadMove &move) const
{
    const auto place = [](const QString &slot, bool bTeam) {
        if (slot.isEmpty())
            return tr("Squad");
        return bTeam ? tr("B-Team") : slot;
    };
    return QStringLiteral("%1: %2 → %3")
        .arg(getLastName(move.player->name), place(move.fromSlot, move.fromBTeam),
             place(move.toSlot, move.toBTeam));
}

void SigningAdvisorTab::showOptions()
{
    const int row = m_needsTable->currentRow();
    const QTableWidgetItem *slotItem = row >= 0 ? m_needsTable->item(row, 0) : nullptr;
    const int index = slotItem ? slotItem->data(kNeedIndexRole).toInt() : -1;
    m_optionsTable->setSortingEnabled(false);
    m_optionsTable->clearContents();
    if (index < 0 || index >= static_cast<int>(m_analysis.needs.size())) {
        m_optionsTitle->clear();
        m_optionsTable->setRowCount(0);
        return;
    }
    const SlotNeed &need = m_analysis.needs[static_cast<size_t>(index)];
    const auto roleNames = m_context.definitions().roleDisplayMap();
    m_optionsTitle->setText(need.options.empty()
                                ? tr("For %1 (%2) the filtered market has nobody who improves "
                                     "the starting XI.")
                                      .arg(need.slot, roleNames.value(need.role, need.role))
                                : tr("Signing options for %1 (%2)")
                                      .arg(need.slot, roleNames.value(need.role, need.role)));
    m_optionsTable->setVisible(!need.options.empty());

    m_optionsTable->setRowCount(static_cast<int>(need.options.size()));
    int r = 0;
    for (const SigningOption &option : need.options) {
        const Player &p = *option.player;
        int column = 0;
        auto *nameItem = new NameItem(p.name);
        nameItem->setData(Qt::UserRole, p.uid);
        m_optionsTable->setItem(r, column++, nameItem);
        m_optionsTable->setItem(r, column++, new NumericItem(QString::number(p.age), p.age));
        m_optionsTable->setItem(r, column++, new QTableWidgetItem(p.club));
        m_optionsTable->setItem(r, column++, new QTableWidgetItem(GameTerms::positions(p.positionRaw)));
        auto *dwrs = new NumericItem(formatDwrsPercent(option.rating),
                                     option.rating);
        const CellStyle style = dwrsCellStyle(option.rating);
        if (style.isValid()) {
            dwrs->setBackground(style.background);
            dwrs->setForeground(style.text);
        }
        m_optionsTable->setItem(r, column++, dwrs);
        m_optionsTable->setItem(r, column++, new NumericItem(Currency::displayText(p.transferValueRaw), p.transferValue));
        m_optionsTable->setItem(r, column++, new QTableWidgetItem(Currency::displayText(p.wageRaw)));
        m_optionsTable->setItem(r, column++, new QTableWidgetItem(GameTerms::text(QStringLiteral("personality"), p.personality)));
        auto *gain = new NumericItem(gainText(option.xiGain), option.xiGain);
        gain->setBackground(gainColor(option.xiGain, need.options.front().xiGain));
        gain->setForeground(QColor(Qt::white));
        m_optionsTable->setItem(r, column++, gain);
        m_optionsTable->setItem(r, column++,
                                new NumericItem(gainText(option.bTeamGain), option.bTeamGain));
        QStringList moves;
        for (const SquadMove &move : option.moves)
            moves << moveText(move);
        m_optionsTable->setItem(r, column++,
                                new QTableWidgetItem(moves.join(QStringLiteral(" · "))));
        ++r;
    }
    // Already ordered by gain; no initial sort arrow (it would clip the header).
    m_optionsTable->horizontalHeader()->setSortIndicator(-1, Qt::DescendingOrder);
    m_optionsTable->setSortingEnabled(true);
    fitHeightToRows(m_optionsTable);
}

void SigningAdvisorTab::fillGapTables(const SquadResult &squad)
{
    AppConfig &config = m_context.config();
    const double displacement =
        config.gapAnalysisSetting(QStringLiteral("displacement_threshold"));
    const double dropoff = config.gapAnalysisSetting(QStringLiteral("dropoff_threshold"));
    const double wrongSide = config.gapAnalysisSetting(QStringLiteral("wrong_side_penalty"));
    m_gapCaption->setText(
        tr("Thresholds — Hidden: %1 · Obvious: %2 · Wrong-side penalty: %3 "
           "(adjustable under Settings → Squad and analysis)")
            .arg(displacement, 0, 'f', 1)
            .arg(dropoff, 0, 'f', 1)
            .arg(wrongSide, 0, 'f', 1));

    const auto positions = m_context.definitions().tacticRoles().value(m_tactic);
    const QStringList slotOrder = m_context.definitions().tacticSlotOrder(m_tactic);
    const auto roleNames = m_context.definitions().roleDisplayMap();
    QHash<QString, const Player *> playersByUid;
    for (const Player *player : m_squad)
        playersByUid.insert(player->uid, player);

    for (auto [team, status, table] :
         {std::tuple{&squad.startingXi, m_xiGapStatus, m_xiGapTable},
          std::tuple{&squad.bTeam, m_bGapStatus, m_bGapTable}}) {
        const std::vector<Gap> gaps =
            GapAnalysis::analyzeTeamGaps(*team, positions, slotOrder, playersByUid,
                                         m_context.ratings(), displacement, dropoff, wrongSide,
                                         m_context.fmVersionId());
        table->setSortingEnabled(false);
        table->clearContents();
        table->setRowCount(static_cast<int>(gaps.size()));
        table->setVisible(!gaps.empty());
        status->setText(gaps.empty() ? tr("✅ No significant gaps found with the current "
                                          "thresholds.")
                                     : tr("%1 notable position(s) found — worst first.")
                                           .arg(gaps.size()));
        double maxScore = 0.0;
        for (const Gap &gap : gaps)
            maxScore = std::max(maxScore, gap.gapScore);
        int row = 0;
        for (const Gap &gap : gaps) {
            table->setItem(row, 0, new QTableWidgetItem(gap.slot));
            table->setItem(row, 1, new QTableWidgetItem(roleNames.value(gap.role, gap.role)));
            table->setItem(row, 2, new QTableWidgetItem(gap.playerName));
            table->setItem(row, 3,
                           new NumericItem(formatDwrs(gap.assignedDwrs),
                                           gap.assignedDwrs));
            table->setItem(row, 4, new QTableWidgetItem(gapTypeLabel(gap)));
            auto *scoreItem =
                new NumericItem(QString::number(gap.gapScore, 'f', 1), gap.gapScore);
            const double norm = maxScore > 0.0 ? gap.gapScore / maxScore : 0.0;
            scoreItem->setBackground(QColor(static_cast<int>(200 * norm + 40),
                                            static_cast<int>(160 * (1 - norm) + 40), 50, 140));
            table->setItem(row, 5, scoreItem);
            table->setItem(row, 6, new QTableWidgetItem(gap.reason));
            ++row;
        }
        // Already ordered worst first.
        table->horizontalHeader()->setSortIndicator(-1, Qt::DescendingOrder);
        table->setSortingEnabled(true);
        fitHeightToRows(table);
    }
}

} // namespace fm
