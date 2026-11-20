#include "DwrsProgressPage.h"
#include "widgets/RolePhaseSwitch.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../widgets/CellStyleDelegate.h"
#include "../widgets/Charts.h"
#include "../widgets/NumericTableItem.h"
#include "PageHelpers.h"
#include "core/SquadBuilder.h"
#include "core/TalentEngine.h"
#include "core/Utils.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fm {

namespace {

enum Column {
    ColName,
    ColAge,
    ColChange,
    ColTrend,
    ColCurrent,
    ColSpot,
    ColRole,
    ColTotal,
    ColCount
};
enum Group { AllPlayers = 0, YouthOnly = 1, Established = 2 };

// Sparkline values (QVariantList, invalid = no rating yet) of the trend column.
constexpr int kSparkRole = Qt::UserRole + 1;

// Most series a single player's chart shows on request (his best roles).
constexpr int kMaxRoleLines = 5;

// Role combo entries besides the concrete roles.
const QString kLineupBasis = QStringLiteral("@lineup");
const QString kBestBasis = QStringLiteral("@best");

QString signedNumber(double value)
{
    if (std::abs(value) < 0.05)
        return QStringLiteral("±0");
    const double magnitude = std::abs(value);
    const bool whole = std::abs(magnitude - std::round(magnitude)) < 0.05;
    return (value < 0 ? QStringLiteral("−") : QStringLiteral("+"))
           + QLocale().toString(magnitude, 'f', whole ? 0 : 1);
}

QString percent(double value)
{
    const bool whole = std::abs(value - std::round(value)) < 0.05;
    return QLocale().toString(value, 'f', whole ? 0 : 1) + QStringLiteral(" %");
}

// Green for growth, red for decline; stronger for a bigger share of the
// largest change in the table.
QColor changeColor(double change, double maxAbs)
{
    const double share = maxAbs > 0.0 ? std::clamp(std::abs(change) / maxAbs, 0.0, 1.0) : 0.0;
    const int alpha = static_cast<int>(70 + share * 160);
    return change > 0 ? QColor(40, 150, 70, alpha) : QColor(200, 60, 50, alpha);
}

// Trend column: a small line over the window, green when it ends higher than
// it started, red when lower.
class SparklineDelegate : public CellStyleDelegate
{
public:
    using CellStyleDelegate::CellStyleDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        CellStyleDelegate::paint(painter, option, index); // background + selection
        const QVariantList values = index.data(kSparkRole).toList();
        QList<QPointF> points;
        double low = 100.0, high = 0.0;
        for (int i = 0; i < values.size(); ++i) {
            if (!values.at(i).isValid())
                continue;
            const double v = values.at(i).toDouble();
            points.append({double(i), v});
            low = std::min(low, v);
            high = std::max(high, v);
        }
        if (points.isEmpty())
            return;
        // Keep a flat line flat instead of blowing up 1-point noise.
        if (high - low < 4.0) {
            const double mid = (high + low) / 2.0;
            low = mid - 2.0;
            high = mid + 2.0;
        }
        const QRectF area = QRectF(option.rect).adjusted(10, 6, -10, -6);
        const double span = std::max<qsizetype>(1, values.size() - 1);
        QPolygonF polyline;
        for (const QPointF &point : std::as_const(points)) {
            polyline << QPointF(area.left() + area.width() * point.x() / span,
                                area.bottom() - area.height() * (point.y() - low) / (high - low));
        }
        const double delta = points.last().y() - points.first().y();
        QColor color = option.palette.text().color();
        color.setAlpha(150);
        if (delta >= 0.5)
            color = QColor(0x4C, 0xAF, 0x50);
        else if (delta <= -0.5)
            color = QColor(0xE5, 0x53, 0x50);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolyline(polyline);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawEllipse(polyline.last(), 2.8, 2.8);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return {130, CellStyleDelegate::sizeHint(option, index).height()};
    }
};

QPushButton *addSegment(QButtonGroup *group, QHBoxLayout *layout, const QString &text, int id,
                        const QString &toolTip = QString())
{
    auto *button = new QPushButton(text);
    button->setCheckable(true);
    button->setToolTip(toolTip);
    button->setAutoDefault(false);
    group->addButton(button, id);
    layout->addWidget(button);
    return button;
}

} // namespace

DwrsProgressPage::DwrsProgressPage(AppContext &context, ThemeManager &theme, QWidget *parent)
    : PageBase(context, parent)
    , m_theme(theme)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(10);

    auto *headRow = new QHBoxLayout;
    headRow->addWidget(
        new QLabel(QStringLiteral("<h2 style='font-weight:600'>%1</h2>").arg(tr("DWRS Development")), this));
    headRow->addStretch(1);
    m_explainToggle = new QToolButton(this);
    m_explainToggle->setText(tr("How it is calculated"));
    m_explainToggle->setToolTip(tr("Show or hide the explanation"));
    m_explainToggle->setCheckable(true);
    m_explainToggle->setArrowType(Qt::RightArrow);
    m_explainToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    headRow->addWidget(m_explainToggle);
    layout->addLayout(headRow);

    auto *intro = new QLabel(
        tr("Who is improving, who stalls? Measured in uploads instead of calendar days."), this);
    intro->setObjectName(QStringLiteral("kpiCaption"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_explain = new QLabel(
        tr("<b>Snapshots:</b> Every upload is a snapshot. Uploads at most %1 minutes apart (e.g. "
           "squad, shortlist and full export of the same game week) count as one. The axis "
           "counts snapshots instead of days, because the export carries no in-game "
           "date.<br><b>Δ:</b> Change of the DWRS in percentage points over the chosen period, "
           "in the role of the \"Role\" column. The default is the role the player plays in the "
           "Best XI of your favorite tactic (Settings) — in the starting XI or the B-team. "
           "Anyone not there counts in his best role of that tactic. A new value is only stored "
           "when it changes by at least 1 point; in between the last value stays "
           "valid.<br><b>Recalculations</b> (changed weights, roles or formula) shift values "
           "without any real development. They appear as a dashed line and are factored out: "
           "earlier values are converted to today's calculation, Δ counts real change only. "
           "Recorded from version 1.4.14 on; older ones can no longer be detected. Newly visible "
           "attributes (scouted ranges turn into exact values after a signing) can cause a jump "
           "as well.")
            .arg(Development::kSessionGapMinutes),
        this);
    m_explain->setObjectName(QStringLiteral("kpiCaption"));
    m_explain->setWordWrap(true);
    m_explain->setVisible(false);
    layout->addWidget(m_explain);
    connect(m_explainToggle, &QToolButton::toggled, this, [this](bool shown) {
        m_explain->setVisible(shown);
        m_explainToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
    });

    // --- Filters ---
    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(6);
    filterRow->addWidget(new QLabel(tr("Period:"), this));
    m_windowGroup = new QButtonGroup(this);
    for (const int uploads : {5, 10, 20}) {
        addSegment(m_windowGroup, filterRow, tr("Last %1").arg(uploads), uploads,
                   tr("Change over the last %1 snapshots (uploads)").arg(uploads));
    }
    addSegment(m_windowGroup, filterRow, tr("All"), 0,
               tr("Change since the first snapshot"));
    m_windowGroup->button(10)->setChecked(true);

    filterRow->addSpacing(18);
    filterRow->addWidget(new QLabel(tr("Player:"), this));
    m_groupGroup = new QButtonGroup(this);
    addSegment(m_groupGroup, filterRow, tr("All"), AllPlayers)->setChecked(true);
    addSegment(m_groupGroup, filterRow, tr("Youth"), YouthOnly);
    addSegment(m_groupGroup, filterRow, tr("Established"), Established);

    filterRow->addSpacing(18);
    filterRow->addWidget(new QLabel(tr("Role:"), this));
    m_roleCombo = new QComboBox(this);
    m_roleCombo->setMinimumWidth(240);
    m_roleCombo->setToolTip(tr("The role that values and Δ refer to"));
    filterRow->addWidget(new RolePhaseSwitch(m_context.definitions(), m_roleCombo, m_roleCombo->parentWidget()));
    filterRow->addWidget(m_roleCombo);

    filterRow->addSpacing(18);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search player…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(180);
    filterRow->addWidget(m_searchEdit);
    filterRow->addStretch(1);
    layout->addLayout(filterRow);

    m_notice = new QLabel(this);
    m_notice->setWordWrap(true);
    m_notice->setVisible(false);
    layout->addWidget(m_notice);

    // --- KPI tiles ---
    auto *kpiRow = new QHBoxLayout;
    kpiRow->setSpacing(10);
    const auto makeTile = [&](QLabel **valueOut, QLabel **captionOut) {
        auto *tile = new QFrame(this);
        tile->setObjectName(QStringLiteral("kpiTile"));
        auto *tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(16, 10, 16, 10);
        auto *value = new QLabel(QStringLiteral("–"), tile);
        value->setObjectName(QStringLiteral("kpiValue"));
        auto *caption = new QLabel(tile);
        caption->setObjectName(QStringLiteral("kpiCaption"));
        caption->setWordWrap(true);
        tileLayout->addWidget(value);
        tileLayout->addWidget(caption);
        kpiRow->addWidget(tile, 1);
        *valueOut = value;
        *captionOut = caption;
    };
    makeTile(&m_avgValue, &m_avgCaption);
    makeTile(&m_riserValue, &m_riserCaption);
    makeTile(&m_fallerValue, &m_fallerCaption);
    makeTile(&m_snapshotValue, &m_snapshotCaption);
    layout->addLayout(kpiRow);

    // --- Table | chart ---
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(ColCount);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumWidth(520);
    PlayerActions::attachToTableWidget(m_context, m_table, ColName);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setItemDelegateForColumn(ColTrend, new SparklineDelegate(m_table));
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(ColChange, Qt::DescendingOrder);

    auto *chartPanel = new QWidget(splitter);
    auto *chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(10, 0, 0, 0);
    auto *chartHead = new QHBoxLayout;
    m_chartTitle = new QLabel(chartPanel);
    m_chartTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_chartTitle->setWordWrap(true);
    chartHead->addWidget(m_chartTitle, 1);
    m_moreRoles = new QPushButton(tr("More roles"), chartPanel);
    m_moreRoles->setToolTip(tr("Also show the player's %1 best roles")
                                .arg(kMaxRoleLines));
    m_moreRoles->setCheckable(true);
    m_moreRoles->setAutoDefault(false);
    m_moreRoles->setVisible(false);
    chartHead->addWidget(m_moreRoles);
    m_clearSelection = new QPushButton(tr("Back to the squad trend"), chartPanel);
    m_clearSelection->setAutoDefault(false);
    m_clearSelection->setVisible(false);
    chartHead->addWidget(m_clearSelection);
    chartLayout->addLayout(chartHead);
    m_chart = new LineChartWidget(m_theme, chartPanel);
    m_chart->setMinimumHeight(380);
    chartLayout->addWidget(m_chart, 1);
    auto *chartHint = new QLabel(
        tr("Click a player to see his development in the table's role, Ctrl- or Shift-click "
           "compares several. Double-click opens the profile, right-click shows more actions."),
        chartPanel);
    chartHint->setObjectName(QStringLiteral("kpiCaption"));
    chartHint->setWordWrap(true);
    chartLayout->addWidget(chartHint);

    splitter->addWidget(m_table);
    splitter->addWidget(chartPanel);
    splitter->setStretchFactor(0, 6);
    splitter->setStretchFactor(1, 5);
    layout->addWidget(splitter, 1);

    const auto onFilter = [this] {
        if (!m_updating)
            rebuildRows();
    };
    connect(m_windowGroup, &QButtonGroup::idClicked, this, onFilter);
    connect(m_groupGroup, &QButtonGroup::idClicked, this, onFilter);
    connect(m_roleCombo, &QComboBox::currentIndexChanged, this, onFilter);
    connect(m_searchEdit, &QLineEdit::textChanged, this, onFilter);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (!m_updating)
            rebuildChart();
    });
    connect(m_clearSelection, &QPushButton::clicked, m_table, &QTableWidget::clearSelection);
    connect(m_moreRoles, &QPushButton::toggled, this, [this] { rebuildChart(); });
}

std::vector<const Player *> DwrsProgressPage::poolPlayers() const
{
    std::vector<const Player *> players;
    if (m_context.nationalUiMode()) {
        // National mode: the analysis pool is the saved national squad.
        for (const Player &player : m_context.store().players()) {
            if (player.inNationalSquad)
                players.push_back(&player);
        }
        return players;
    }
    const QString userClub = m_context.userClub();
    if (userClub.isEmpty())
        return players;
    for (const Player &player : m_context.store().players()) {
        if (player.club == userClub)
            players.push_back(&player);
    }
    return players;
}

int DwrsProgressPage::window() const
{
    return std::max(0, m_windowGroup->checkedId());
}

void DwrsProgressPage::refresh()
{
    m_updating = true;
    reloadTimeline();
    reloadLineup();
    fillRoleCombo();
    m_updating = false;
    rebuildRows();
}

void DwrsProgressPage::reloadTimeline()
{
    QList<int> ids;
    for (const Player *player : poolPlayers())
        ids << player->id;
    Database &db = m_context.database();
    m_timeline = Development::buildTimeline(db.dwrsHistory(ids), db.dwrsRecalcTimestamps());
    m_snapshotLabels = developmentSnapshotLabels(m_context, m_timeline);
    m_snapshotGameDates = developmentSnapshotGameDates(m_context, m_timeline);
}

void DwrsProgressPage::reloadLineup()
{
    // The same squad the Best XI page shows by default: the favorite tactic,
    // long-term injured club players left out, national squads without the
    // club playing-time weight.
    m_lineup.clear();
    const bool national = m_context.nationalUiMode();
    m_lineupTactic = favoritesFirstTactics(m_context, national).value(0);
    const std::vector<const Player *> players = poolPlayers();
    if (m_lineupTactic.isEmpty() || players.empty())
        return;
    const auto positions = m_context.definitions().tacticRoles().value(m_lineupTactic);
    const QStringList slotOrder = m_context.definitions().tacticSlotOrder(m_lineupTactic);
    std::vector<const Player *> available;
    for (const Player *player : players) {
        if (national || !player->longTermInjured)
            available.push_back(player);
    }
    const SquadResult squad = m_context.squadBuilder().calculateSquadForTactic(
        available, m_lineupTactic, m_context.ratings(), !national);
    for (const auto &[team, spot] : {std::pair{&squad.startingXi, Spot::StartingXi},
                                     std::pair{&squad.bTeam, Spot::BTeam}}) {
        for (auto it = team->constBegin(); it != team->constEnd(); ++it) {
            if (it.value().isFilled())
                m_lineup.insert(it.value().playerUid, {positions.value(it.key()), spot});
        }
    }
    // Everyone else: his best role among the ones the tactic uses.
    for (const Player *player : players) {
        if (m_lineup.contains(player->uid))
            continue;
        const QStringList choices = trainingRoleChoices(m_context, *player, m_lineupTactic);
        m_lineup.insert(player->uid,
                        {bestTrainingRole(m_context, *player, choices),
                         !national && player->longTermInjured ? Spot::Injured : Spot::Squad});
    }
}

void DwrsProgressPage::fillRoleCombo()
{
    const QString previous = m_roleCombo->currentData().toString();
    const auto roleNames = m_context.definitions().roleDisplayMap();
    QSet<QString> roleSet;
    for (const Player *player : poolPlayers()) {
        for (const QString &role : player->assignedRoles)
            roleSet.insert(role);
    }
    QStringList roles(roleSet.cbegin(), roleSet.cend());
    std::sort(roles.begin(), roles.end(), [&roleNames](const QString &a, const QString &b) {
        return roleNames.value(a, a).localeAwareCompare(roleNames.value(b, b)) < 0;
    });
    m_roleCombo->blockSignals(true);
    m_roleCombo->clear();
    m_roleCombo->addItem(m_lineupTactic.isEmpty()
                             ? tr("Role in the Best XI")
                             : tr("Role in the Best XI (%1)").arg(m_lineupTactic),
                         kLineupBasis);
    m_roleCombo->setItemData(0,
                             tr("The role the player plays in the starting XI or B-team "
                                "of the favorite tactic (Settings)"),
                             Qt::ToolTipRole);
    m_roleCombo->addItem(tr("Best role per player"), kBestBasis);
    for (const QString &role : std::as_const(roles))
        m_roleCombo->addItem(roleNames.value(role, role), role);
    const int index = m_roleCombo->findData(previous);
    m_roleCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_roleCombo->blockSignals(false);
}

QString DwrsProgressPage::chosenRole() const
{
    const QString data = m_roleCombo->currentData().toString();
    return data == kLineupBasis || data == kBestBasis ? QString() : data;
}

QString DwrsProgressPage::basisRole(const Player &player, bool *fromLineup) const
{
    if (fromLineup)
        *fromLineup = false;
    const QString chosen = chosenRole();
    if (!chosen.isEmpty()) {
        return player.assignedRoles.contains(chosen) && m_timeline.track(player.id, chosen)
                   ? chosen
                   : QString();
    }
    if (m_roleCombo->currentData().toString() == kLineupBasis) {
        const QString role = m_lineup.value(player.uid).role;
        const Development::Track *track = role.isEmpty() ? nullptr
                                                         : m_timeline.track(player.id, role);
        if (track && track->isValid()) {
            if (fromLineup)
                *fromLineup = true;
            return role;
        }
        // No history in that role yet: fall back to his best role.
    }
    QString best;
    double bestValue = -1.0;
    for (const QString &role : player.assignedRoles) {
        const Development::Track *track = m_timeline.track(player.id, role);
        if (track && track->isValid() && track->current() > bestValue) {
            bestValue = track->current();
            best = role;
        }
    }
    return best;
}

void DwrsProgressPage::rebuildRows()
{
    m_rows.clear();
    const auto players = poolPlayers();
    const bool noPool = players.empty();
    m_notice->setVisible(noPool);
    if (noPool) {
        m_notice->setText(m_context.nationalUiMode()
                              ? tr("No national squad saved yet.")
                              : tr("Please choose your club first (Overview or "
                                   "Settings)."));
    }

    const int win = window();
    const int group = m_groupGroup->checkedId();
    const QString search = foldForSearch(m_searchEdit->text().trimmed());
    const int outfielderCap = m_context.config().ageThreshold(QStringLiteral("outfielder"));
    const int goalkeeperCap = m_context.config().ageThreshold(QStringLiteral("goalkeeper"));
    m_groupGroup->button(YouthOnly)
        ->setToolTip(tr("Up to %1 years, goalkeepers up to %2 (Settings → Squad and analysis)")
                         .arg(outfielderCap)
                         .arg(goalkeeperCap));

    for (const Player *player : players) {
        const bool youth =
            player->age > 0
            && player->age <= TalentEngine::ageCapForPlayer(*player, outfielderCap, goalkeeperCap);
        if ((group == YouthOnly && !youth) || (group == Established && youth))
            continue;
        if (!search.isEmpty() && !foldForSearch(player->name).contains(search))
            continue;
        bool fromLineup = false;
        const QString role = basisRole(*player, &fromLineup);
        const Development::Track *track = role.isEmpty() ? nullptr
                                                         : m_timeline.track(player->id, role);
        if (!track || !track->isValid())
            continue;
        Row row;
        row.uid = player->uid;
        row.playerId = player->id;
        row.name = player->name;
        row.age = player->age;
        row.youth = youth;
        row.role = role;
        row.inLineup = fromLineup;
        row.spot = m_lineup.value(player->uid).spot;
        row.track = track;
        row.change = Development::change(*track, win);
        row.totalChange = Development::change(*track, 0);
        m_rows.push_back(std::move(row));
    }

    rebuildTable();
    rebuildKpis();
    rebuildChart();
}

void DwrsProgressPage::rebuildTable()
{
    m_updating = true;
    const QStringList previousSelection = selectedUids();
    const auto roleNames = m_context.definitions().roleDisplayMap();
    const int win = window();
    const int n = m_timeline.size();
    const int start = Development::windowStart(n, win);

    m_table->setHorizontalHeaderLabels(
        {tr("Player"), tr("Age"), win > 0 ? tr("Δ last %1").arg(win) : tr("Δ total"),
         tr("Trend"), tr("Current"), tr("Best XI"), tr("Role"), tr("Δ total")});
    m_table->setColumnHidden(ColTotal, win == 0);
    m_table->setColumnHidden(ColSpot, m_roleCombo->currentData().toString() != kLineupBasis);
    // Room for the sort arrow: the styled header leaves it out of its size
    // hint and would clip the centered title instead.
    QFont headerFont = m_table->horizontalHeader()->font();
    headerFont.setBold(true);
    const QFontMetrics headerMetrics(headerFont);
    for (int c = 0; c < ColCount; ++c) {
        QTableWidgetItem *header = m_table->horizontalHeaderItem(c);
        header->setSizeHint(
            QSize(headerMetrics.horizontalAdvance(header->text()) + 48, headerMetrics.height() + 18));
    }
    m_table->horizontalHeaderItem(ColChange)
        ->setToolTip(tr("Change in percentage points over the chosen period"));
    m_table->horizontalHeaderItem(ColTotal)
        ->setToolTip(tr("Change since the player's first snapshot"));
    m_table->horizontalHeaderItem(ColSpot)
        ->setToolTip(tr("Place in the Best XI of the favorite tactic (%1)").arg(m_lineupTactic));

    double maxAbs = 0.0;
    for (const Row &row : m_rows)
        maxAbs = std::max(maxAbs, std::abs(row.change));

    m_table->setSortingEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    for (int r = 0; r < static_cast<int>(m_rows.size()); ++r) {
        const Row &row = m_rows[static_cast<size_t>(r)];
        auto *name = new NameItem(row.name);
        name->setData(Qt::UserRole, row.uid);
        m_table->setItem(r, ColName, name);
        m_table->setItem(r, ColAge, new NumericItem(QString::number(row.age), row.age));
        m_table->setItem(r, ColRole, new QTableWidgetItem(roleNames.value(row.role, row.role)));
        QString spotText = QStringLiteral("–");
        QString spotTip = tr("Not in starting XI or B-team: best role of the tactic");
        switch (row.spot) {
        case Spot::StartingXi:
            spotText = tr("Starting XI");
            spotTip.clear();
            break;
        case Spot::BTeam:
            spotText = tr("B-Team");
            spotTip.clear();
            break;
        case Spot::Injured:
            spotText = tr("injured");
            spotTip = tr("Long-term injured, missing from the Best XI: best role of the tactic");
            break;
        case Spot::Squad:
            break;
        }
        if (!row.inLineup)
            spotTip = tr("No history in his Best XI role yet: values of his best role");
        auto *spot = new NumericItem(spotText, static_cast<double>(row.spot));
        spot->setTextAlignment(Qt::AlignCenter);
        spot->setToolTip(spotTip);
        m_table->setItem(r, ColSpot, spot);
        const double current = row.track->current();
        m_table->setItem(r, ColCurrent, new NumericItem(percent(current), current));

        auto *change = new NumericItem(signedNumber(row.change), row.change);
        change->setTextAlignment(Qt::AlignCenter);
        if (std::abs(row.change) >= 0.05) {
            change->setBackground(changeColor(row.change, maxAbs));
            change->setForeground(QColor(Qt::white));
        }
        m_table->setItem(r, ColChange, change);
        auto *total = new NumericItem(signedNumber(row.totalChange), row.totalChange);
        total->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, ColTotal, total);

        QVariantList spark;
        for (int s = start; s < n; ++s) {
            const double v = row.track->adjusted.at(s);
            spark << (std::isnan(v) ? QVariant() : QVariant(v));
        }
        auto *trend = new NumericItem(QString(), row.change);
        trend->setData(kSparkRole, spark);
        trend->setToolTip(tr("Trend over the chosen period"));
        m_table->setItem(r, ColTrend, trend);
    }
    m_table->setSortingEnabled(true);

    // Keep the selection across filter changes.
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (previousSelection.contains(m_table->item(r, ColName)->data(Qt::UserRole).toString()))
            m_table->selectionModel()->select(
                m_table->model()->index(r, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    m_updating = false;
}

void DwrsProgressPage::rebuildKpis()
{
    const int win = window();
    const auto dash = QStringLiteral("–");
    m_avgCaption->setText(win > 0 ? tr("Ø change over the last %1 uploads").arg(win)
                                  : tr("Ø change since the first snapshot"));
    m_snapshotValue->setText(QString::number(m_timeline.size()));
    m_snapshotCaption->setText(
        m_timeline.size() > 0
            ? tr("Snapshots · latest %1").arg(m_snapshotLabels.constLast())
            : tr("Snapshots"));
    if (m_rows.empty()) {
        m_avgValue->setText(dash);
        m_riserValue->setText(dash);
        m_riserCaption->setText(tr("Biggest jump"));
        m_fallerValue->setText(dash);
        m_fallerCaption->setText(tr("Biggest drop"));
        return;
    }

    double sum = 0.0;
    const Row *riser = &m_rows.front();
    const Row *faller = &m_rows.front();
    for (const Row &row : m_rows) {
        sum += row.change;
        if (row.change > riser->change)
            riser = &row;
        if (row.change < faller->change)
            faller = &row;
    }
    m_avgValue->setText(signedNumber(sum / static_cast<double>(m_rows.size())));
    const auto roleNames = m_context.definitions().roleDisplayMap();
    if (riser->change >= 0.5) {
        m_riserValue->setText(signedNumber(riser->change));
        m_riserCaption->setText(tr("Biggest jump: %1").arg(riser->name));
        m_riserCaption->setToolTip(roleNames.value(riser->role, riser->role));
    } else {
        m_riserValue->setText(dash);
        m_riserCaption->setText(tr("Biggest jump: nobody improved"));
    }
    if (faller->change <= -0.5) {
        m_fallerValue->setText(signedNumber(faller->change));
        m_fallerCaption->setText(tr("Biggest drop: %1").arg(faller->name));
        m_fallerCaption->setToolTip(roleNames.value(faller->role, faller->role));
    } else {
        m_fallerValue->setText(dash);
        m_fallerCaption->setText(tr("Biggest drop: nobody declined"));
    }
}

QStringList DwrsProgressPage::selectedUids() const
{
    QStringList uids;
    for (const QModelIndex &index : m_table->selectionModel()->selectedRows(ColName))
        uids << index.data(Qt::UserRole).toString();
    return uids;
}

QList<QPointF> DwrsProgressPage::trackPoints(const Development::Track &track) const
{
    QList<QPointF> points;
    const int n = static_cast<int>(track.adjusted.size());
    for (int s = Development::windowStart(n, window()); s < n; ++s) {
        if (!std::isnan(track.adjusted.at(s)))
            points.append({double(s), track.adjusted.at(s)});
    }
    return points;
}

void DwrsProgressPage::rebuildChart()
{
    const QStringList uids = selectedUids();
    m_clearSelection->setVisible(!uids.isEmpty());
    m_moreRoles->setVisible(uids.size() == 1);

    if (m_rows.empty()) {
        m_chartTitle->setText(QString());
        m_chart->clearChart(m_timeline.size() == 0
                                ? tr("No DWRS history for these players yet.")
                                : tr("No players for the current selection."));
        return;
    }
    if (m_timeline.size() < 2) {
        m_chartTitle->setText(QString());
        m_chart->clearChart(tr("Only one snapshot so far – after the next upload you "
                               "will see the development here."));
        return;
    }

    const auto roleNames = m_context.definitions().roleDisplayMap();
    const int n = m_timeline.size();
    const int start = Development::windowStart(n, window());
    QList<LineChartWidget::Series> series;
    QList<const Development::Track *> shownTracks;
    LineChartWidget::ValueMode mode = LineChartWidget::ValueMode::Percent;

    if (uids.isEmpty()) {
        // Squad trend: cumulative mean change of the listed players.
        mode = LineChartWidget::ValueMode::Delta;
        QList<const Development::Track *> all, youth, established;
        for (const Row &row : m_rows) {
            all << row.track;
            shownTracks << row.track;
            (row.youth ? youth : established) << row.track;
        }
        const auto addTrend = [&](const QList<const Development::Track *> &tracks,
                                  const QString &name, bool emphasized) {
            if (tracks.isEmpty())
                return;
            const QVector<double> trend = Development::cumulativeMeanChange(tracks, start);
            LineChartWidget::Series line;
            line.name = name;
            line.emphasized = emphasized;
            for (int s = start; s < trend.size(); ++s)
                line.points.append({double(s), trend.at(s)});
            series.append(line);
        };
        addTrend(all, tr("Ø all shown (%1)").arg(all.size()), true);
        if (!youth.isEmpty() && !established.isEmpty()) {
            addTrend(youth, tr("Ø youth (%1)").arg(youth.size()), false);
            addTrend(established, tr("Ø established (%1)").arg(established.size()), false);
        }
        m_chartTitle->setText(tr("Squad trend: Ø change in percentage points"));
    } else if (uids.size() == 1) {
        // One player: the table's role; on request also his best roles, the
        // table's role emphasized.
        const Player *player = m_context.store().findByUid(uids.first());
        if (!player) {
            m_chart->clearChart(tr("Player not found."));
            return;
        }
        QString tableRole;
        for (const Row &row : m_rows) {
            if (row.uid == player->uid)
                tableRole = row.role;
        }
        const bool moreRoles = m_moreRoles->isChecked();
        QStringList roles;
        if (!tableRole.isEmpty())
            roles << tableRole;
        if (moreRoles) {
            QList<std::pair<double, QString>> ranked;
            for (const QString &role : player->assignedRoles) {
                const Development::Track *track = m_timeline.track(player->id, role);
                if (track && track->isValid() && role != tableRole)
                    ranked.append({track->current(), role});
            }
            std::sort(ranked.begin(), ranked.end(),
                      [](const auto &a, const auto &b) { return a.first > b.first; });
            for (const auto &[value, role] : std::as_const(ranked)) {
                if (roles.size() < kMaxRoleLines)
                    roles << role;
            }
        }
        for (const QString &role : std::as_const(roles)) {
            LineChartWidget::Series line;
            line.name = roleNames.value(role, role);
            line.emphasized = moreRoles && role == tableRole;
            const Development::Track *track = m_timeline.track(player->id, role);
            line.points = trackPoints(*track);
            shownTracks << track;
            if (!line.points.isEmpty())
                series.append(line);
        }
        m_chartTitle->setText(moreRoles || tableRole.isEmpty()
                                  ? tr("%1 · best roles").arg(player->name)
                                  : QStringLiteral("%1 · %2").arg(
                                        player->name, roleNames.value(tableRole, tableRole)));
    } else {
        // Comparison: every selected player in the role of his table row.
        const bool sameRole = !chosenRole().isEmpty();
        for (const QString &uid : uids) {
            const auto it = std::find_if(m_rows.cbegin(), m_rows.cend(),
                                         [&uid](const Row &row) { return row.uid == uid; });
            if (it == m_rows.cend())
                continue;
            LineChartWidget::Series line;
            line.name = sameRole ? it->name
                                 : QStringLiteral("%1 (%2)").arg(
                                       it->name, roleNames.value(it->role, it->role));
            line.points = trackPoints(*it->track);
            shownTracks << it->track;
            if (!line.points.isEmpty())
                series.append(line);
        }
        const QString role = chosenRole();
        m_chartTitle->setText(
            sameRole ? tr("Comparison as %1").arg(roleNames.value(role, role))
            : m_roleCombo->currentData().toString() == kLineupBasis
                ? tr("Comparison in each player's Best XI role")
                : tr("Comparison, each in his best role"));
    }

    if (series.isEmpty())
        m_chart->clearChart(tr("No historical data for the current selection."));
    else
        m_chart->setSeries(std::move(series), m_snapshotLabels, mode,
                           Development::recalcMarkers(shownTracks, start), m_snapshotGameDates);
}

} // namespace fm
