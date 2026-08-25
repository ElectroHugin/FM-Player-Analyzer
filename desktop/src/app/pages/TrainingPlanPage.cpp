#include "TrainingPlanPage.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../widgets/NumericTableItem.h"
#include "PageHelpers.h"
#include "core/DwrsEngine.h"
#include "core/TrainingAdvice.h"
#include "core/Utils.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace fm {

namespace {

enum Column { ColName = 0, ColAge, ColPosition, ColRole, ColFocus, ColCount };

// Row data computed once so a section can be sorted before the widgets exist.
struct RowData {
    const Player *player = nullptr;
    QStringList choices;   // trainable roles (positions ∩ tactic)
    QString effectiveRole; // selected/best role
    QString roleDisplay;   // localized role name
    QString focus;         // top training methods text
};

} // namespace

TrainingPlanPage::TrainingPlanPage(AppContext &context, QWidget *parent)
    : PageBase(context, parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(10);

    auto *heading = new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Trainingsplan")), this);
    layout->addWidget(heading);

    auto *tacticRow = new QHBoxLayout;
    tacticRow->addWidget(new QLabel(tr("Taktik:"), this));
    m_tacticCombo = new QComboBox(this);
    m_tacticCombo->setMinimumWidth(240);
    tacticRow->addWidget(m_tacticCombo);
    tacticRow->addStretch(1);
    layout->addLayout(tacticRow);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_first.title = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Erste Mannschaft")), this);
    m_first.table = makeTable();
    layout->addWidget(m_first.title);
    layout->addWidget(m_first.table, 1);

    m_second.title = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Zweitteam")), this);
    m_second.table = makeTable();
    layout->addWidget(m_second.title);
    layout->addWidget(m_second.table, 1);

    connect(m_tacticCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        fillSection(m_first);
        fillSection(m_second);
    });

    // Header clicks drive a manual sort (built-in sorting would scramble the
    // per-row role combos, which live outside the item model).
    for (Section *section : {&m_first, &m_second}) {
        connect(section->table->horizontalHeader(), &QHeaderView::sectionClicked, this,
                [this, section](int column) {
                    if (section->sortColumn == column)
                        section->sortDescending = !section->sortDescending;
                    else {
                        section->sortColumn = column;
                        section->sortDescending = false;
                    }
                    fillSection(*section);
                });
        connect(section->table, &QTableWidget::cellDoubleClicked, this,
                [this, section](int row, int) {
                    if (auto *item = section->table->item(row, ColName))
                        PlayerActions::openProfile(m_context, item->data(Qt::UserRole).toString());
                });
    }
}

QTableWidget *TrainingPlanPage::makeTable()
{
    auto *table = new QTableWidget(this);
    table->setColumnCount(ColCount);
    table->setHorizontalHeaderLabels(
        {tr("Name"), tr("Alter"), tr("Position"), tr("Trainingsrolle"),
         tr("Trainingsmethoden (in Reihenfolge)")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(false); // manual sort; combos are cell widgets
    table->horizontalHeader()->setSectionsClickable(true);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

void TrainingPlanPage::refresh()
{
    m_updating = true;
    const QString previous = m_tacticCombo->currentText();
    m_tacticCombo->clear();
    m_tacticCombo->addItems(favoritesFirstTactics(m_context, false));
    if (!previous.isEmpty() && m_tacticCombo->findText(previous) >= 0)
        m_tacticCombo->setCurrentText(previous);

    m_first.club = m_context.userClub();
    m_second.club = m_context.secondTeamClub();
    m_updating = false;

    const bool haveClub = !m_first.club.isEmpty();
    m_hint->setText(haveClub
                        ? tr("Rolle wählbar aus <i>Positionen ∩ Taktik</i> (wird gespeichert). "
                             "Reihenfolge der Methoden = größter DWRS-Gewinn zuerst; Reha-Methoden "
                             "werden nicht vorgeschlagen. Spaltenkopf klicken sortiert.")
                        : tr("⚠️ Kein Verein gesetzt. Wähle unter Einstellungen → Verein "
                             "'Mein Verein', damit dein Kader angezeigt wird."));

    fillSection(m_first);
    fillSection(m_second);

    // The second-team block only appears when a second team is configured.
    const bool haveSecond = !m_second.club.isEmpty();
    m_second.title->setVisible(haveSecond);
    m_second.table->setVisible(haveSecond);
}

QString TrainingPlanPage::focusText(const Player &player, const QString &role) const
{
    if (role.isEmpty())
        return QStringLiteral("—");
    const TrainingAdvice::FocusAdvice advice = TrainingAdvice::adviseFocusesForRole(
        player, role, m_context.dwrsEngine(), trainingWindows(m_context));
    if (!advice.valid || advice.focus.empty())
        return tr("nichts sinnvoll trainierbar");
    QStringList parts;
    const int limit = std::min<int>(3, static_cast<int>(advice.focus.size()));
    for (int i = 0; i < limit; ++i)
        parts << advice.focus[i].focus;
    return parts.join(QStringLiteral("  →  "));
}

void TrainingPlanPage::fillSection(Section &section)
{
    QTableWidget *table = section.table;
    if (section.club.isEmpty()) {
        table->setRowCount(0);
        return;
    }
    const QString tactic = m_tacticCombo->currentText();
    const auto roleNames = m_context.definitions().roleDisplayMap();

    // 1. Compute row data (so we can sort before creating combos).
    std::vector<RowData> rows;
    for (const Player &p : m_context.store().players()) {
        if (p.club != section.club)
            continue;
        RowData rd;
        rd.player = &p;
        rd.choices = tactic.isEmpty() ? QStringList()
                                      : trainingRoleChoices(m_context, p, tactic);
        rd.effectiveRole = effectiveTrainingRole(m_context, p, rd.choices);
        rd.roleDisplay = rd.effectiveRole.isEmpty()
                             ? QString()
                             : roleNames.value(rd.effectiveRole, rd.effectiveRole);
        rd.focus = focusText(p, rd.effectiveRole);
        rows.push_back(std::move(rd));
    }

    // 2. Sort by the active column.
    const int col = section.sortColumn;
    std::sort(rows.begin(), rows.end(), [col](const RowData &a, const RowData &b) {
        switch (col) {
        case ColAge:
            return a.player->age < b.player->age;
        case ColPosition:
            return a.player->positionRaw.localeAwareCompare(b.player->positionRaw) < 0;
        case ColRole:
            return a.roleDisplay.localeAwareCompare(b.roleDisplay) < 0;
        case ColFocus:
            return a.focus.localeAwareCompare(b.focus) < 0;
        default:
            return getLastName(a.player->name).localeAwareCompare(getLastName(b.player->name)) < 0;
        }
    });
    if (section.sortDescending)
        std::reverse(rows.begin(), rows.end());

    // 3. Populate.
    m_updating = true;
    table->clearContents();
    table->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const RowData &rd = rows[static_cast<size_t>(row)];
        const Player *player = rd.player;

        auto *nameItem = new QTableWidgetItem(player->name);
        nameItem->setData(Qt::UserRole, player->uid);
        table->setItem(row, ColName, nameItem);
        table->setItem(row, ColAge,
                       new NumericItem(QString::number(player->age), player->age));
        table->setItem(row, ColPosition, new QTableWidgetItem(player->positionRaw));

        auto *focusItem = new QTableWidgetItem(rd.focus);
        table->setItem(row, ColFocus, focusItem);

        if (rd.choices.isEmpty()) {
            table->setItem(row, ColRole, new QTableWidgetItem(QStringLiteral("—")));
            focusItem->setText(tr("keine Rolle in dieser Taktik"));
            continue;
        }

        auto *combo = new QComboBox(table);
        for (const QString &role : rd.choices)
            combo->addItem(roleNames.value(role, role), role);
        const int idx = combo->findData(rd.effectiveRole);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
        table->setCellWidget(row, ColRole, combo);

        const int playerId = player->id;
        const QString uid = player->uid;
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this, combo, focusItem, playerId, uid] {
                    if (m_updating)
                        return;
                    const QString role = combo->currentData().toString();
                    if (!m_context.database().setTrainingRole(playerId, role))
                        return;
                    const int storeRow = m_context.store().rowByUid(uid);
                    if (storeRow >= 0)
                        m_context.store().at(storeRow).trainingRole = role;
                    if (const Player *p = m_context.store().findByUid(uid))
                        focusItem->setText(focusText(*p, role));
                });
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    m_updating = false;
}

} // namespace fm
