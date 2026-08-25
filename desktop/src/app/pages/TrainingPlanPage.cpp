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

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("Alter"), tr("Position"), tr("Trainingsrolle"),
         tr("Trainingsfokus (in Reihenfolge)")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(false); // per-row combo widgets don't survive sorts
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);

    connect(m_tacticCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_updating)
            rebuild();
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto *item = m_table->item(row, 0))
            PlayerActions::openProfile(m_context, item->data(Qt::UserRole).toString());
    });
}

void TrainingPlanPage::refresh()
{
    m_updating = true;
    const QString previous = m_tacticCombo->currentText();
    m_tacticCombo->clear();
    m_tacticCombo->addItems(favoritesFirstTactics(m_context, false));
    if (!previous.isEmpty() && m_tacticCombo->findText(previous) >= 0)
        m_tacticCombo->setCurrentText(previous);
    m_updating = false;
    rebuild();
}

QString TrainingPlanPage::focusText(const Player &player, const QString &role) const
{
    if (role.isEmpty())
        return QStringLiteral("—");
    const TrainingAdvice::Advice advice = TrainingAdvice::adviseForRole(
        player, role, m_context.dwrsEngine(), trainingWindows(m_context));
    if (!advice.valid || advice.focus.empty())
        return tr("nichts sinnvoll trainierbar");
    QStringList parts;
    const int limit = std::min<int>(3, static_cast<int>(advice.focus.size()));
    for (int i = 0; i < limit; ++i) {
        const auto &r = advice.focus[i];
        parts << QStringLiteral("%1 (%2)").arg(r.attrName).arg(r.currentValue);
    }
    return parts.join(QStringLiteral("  →  "));
}

void TrainingPlanPage::rebuild()
{
    const QString userClub = m_context.userClub();
    if (userClub.isEmpty()) {
        m_hint->setText(tr("⚠️ Kein Verein gesetzt. Wähle unter Einstellungen → Verein "
                           "'Mein Verein', damit dein Kader angezeigt wird."));
        m_table->setRowCount(0);
        return;
    }
    const QString tactic = m_tacticCombo->currentText();
    m_hint->setText(tr("Kader von %1. Rolle wählbar aus <i>Positionen ∩ Taktik</i>; die "
                       "Auswahl wird gespeichert. Reihenfolge = größter DWRS-Gewinn zuerst, "
                       "nur noch trainierbare Attribute.")
                        .arg(userClub.toHtmlEscaped()));

    std::vector<const Player *> squad;
    for (const Player &p : m_context.store().players()) {
        if (p.club == userClub)
            squad.push_back(&p);
    }
    std::sort(squad.begin(), squad.end(), [](const Player *a, const Player *b) {
        return getLastName(a->name).localeAwareCompare(getLastName(b->name)) < 0;
    });

    const auto roleNames = m_context.definitions().roleDisplayMap();
    m_updating = true;
    m_table->clearContents();
    m_table->setRowCount(static_cast<int>(squad.size()));
    for (int row = 0; row < static_cast<int>(squad.size()); ++row) {
        const Player *player = squad[static_cast<size_t>(row)];

        auto *nameItem = new QTableWidgetItem(player->name);
        nameItem->setData(Qt::UserRole, player->uid);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1,
                         new NumericItem(QString::number(player->age), player->age));
        m_table->setItem(row, 2, new QTableWidgetItem(player->positionRaw));

        auto *focusItem = new QTableWidgetItem();
        m_table->setItem(row, 4, focusItem);

        const QStringList choices =
            tactic.isEmpty() ? QStringList() : trainingRoleChoices(m_context, *player, tactic);
        if (choices.isEmpty()) {
            auto *dash = new QTableWidgetItem(QStringLiteral("—"));
            m_table->setItem(row, 3, dash);
            focusItem->setText(tr("keine Rolle in dieser Taktik"));
            continue;
        }

        const QString effective = effectiveTrainingRole(m_context, *player, choices);
        auto *combo = new QComboBox(m_table);
        for (const QString &role : choices)
            combo->addItem(roleNames.value(role, role), role);
        const int idx = combo->findData(effective);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_table->setCellWidget(row, 3, combo);
        focusItem->setText(focusText(*player, effective));

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
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_updating = false;
}

} // namespace fm
