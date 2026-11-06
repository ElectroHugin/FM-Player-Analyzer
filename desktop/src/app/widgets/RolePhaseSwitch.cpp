#include "RolePhaseSwitch.h"

#include "core/Definitions.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QComboBox>
#include <QListView>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>

namespace fm {

namespace {

constexpr int HiddenByPhaseRole = Qt::UserRole + 77;

QBoxLayout *findBoxLayout(QLayout *layout, QWidget *widget, int *index)
{
    if (!layout)
        return nullptr;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (item->widget() == widget) {
            if (auto *box = qobject_cast<QBoxLayout *>(layout)) {
                *index = i;
                return box;
            }
            return nullptr;
        }
        if (QLayout *child = item->layout()) {
            if (QBoxLayout *found = findBoxLayout(child, widget, index))
                return found;
        }
    }
    return nullptr;
}

} // namespace

RolePhaseSwitch::RolePhaseSwitch(const Definitions &definitions, QComboBox *combo, QWidget *parent)
    : QWidget(parent)
    , m_definitions(definitions)
    , m_combo(combo)
{
    setObjectName(QStringLiteral("phaseSwitch"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);
    m_ipButton = new QToolButton(this);
    m_ipButton->setText(tr("Mit Ball"));
    m_ipButton->setToolTip(tr("Rollen mit Ball (IP) zeigen"));
    m_ipButton->setObjectName(QStringLiteral("phaseSwitchLeft"));
    m_oopButton = new QToolButton(this);
    m_oopButton->setText(tr("Ohne Ball"));
    m_oopButton->setToolTip(tr("Rollen ohne Ball (OOP) zeigen"));
    m_oopButton->setObjectName(QStringLiteral("phaseSwitchRight"));
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (QToolButton *button : {m_ipButton, m_oopButton}) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        group->addButton(button);
        layout->addWidget(button);
    }
    m_ipButton->setChecked(true);
    connect(m_ipButton, &QToolButton::clicked, this, [this] { setPhase(QStringLiteral("ip")); });
    connect(m_oopButton, &QToolButton::clicked, this, [this] { setPhase(QStringLiteral("oop")); });

    if (m_combo) {
        QAbstractItemModel *model = m_combo->model();
        connect(model, &QAbstractItemModel::rowsInserted, this, &RolePhaseSwitch::scheduleRefresh);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &RolePhaseSwitch::scheduleRefresh);
        connect(model, &QAbstractItemModel::modelReset, this, &RolePhaseSwitch::scheduleRefresh);
        connect(m_combo, &QComboBox::currentIndexChanged, this, &RolePhaseSwitch::syncToCurrent);
    }
    refresh();
}

RolePhaseSwitch *RolePhaseSwitch::attach(const Definitions &definitions, QComboBox *combo)
{
    if (!combo || !combo->parentWidget())
        return nullptr;
    auto *phaseSwitch = new RolePhaseSwitch(definitions, combo, combo->parentWidget());
    int index = -1;
    if (QBoxLayout *box = findBoxLayout(combo->parentWidget()->layout(), combo, &index))
        box->insertWidget(index, phaseSwitch);
    return phaseSwitch;
}

QString RolePhaseSwitch::phaseOf(int row) const
{
    const QString role = m_combo->itemData(row).toString();
    if (role.isEmpty())
        return {};
    const QString phase = m_definitions.rolePhase(role);
    return (phase == QLatin1String("ip") || phase == QLatin1String("oop")) ? phase : QString();
}

void RolePhaseSwitch::setPhase(const QString &phase)
{
    if (phase != QLatin1String("ip") && phase != QLatin1String("oop"))
        return;
    m_phase = phase;
    m_ipButton->setChecked(phase == QLatin1String("ip"));
    m_oopButton->setChecked(phase == QLatin1String("oop"));
    refresh();
    if (!m_combo || m_combo->currentIndex() < 0 || isHidden())
        return;
    const QString current = phaseOf(m_combo->currentIndex());
    if (current.isEmpty() || current == m_phase)
        return;
    // The selected role belongs to the other phase: take the first entry
    // without a role ("any"), otherwise the first role of this phase.
    int target = -1;
    for (int row = 0; row < m_combo->count() && target < 0; ++row) {
        if (m_combo->itemData(row).toString().isEmpty())
            target = row;
    }
    for (int row = 0; row < m_combo->count() && target < 0; ++row) {
        if (phaseOf(row) == m_phase)
            target = row;
    }
    if (target >= 0) {
        m_applying = true;
        m_combo->setCurrentIndex(target);
        m_applying = false;
    }
}

void RolePhaseSwitch::scheduleRefresh()
{
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        syncToCurrent();
        refresh();
    });
}

void RolePhaseSwitch::syncToCurrent()
{
    if (m_applying || !m_combo || m_combo->currentIndex() < 0)
        return;
    const QString current = phaseOf(m_combo->currentIndex());
    if (!current.isEmpty() && current != m_phase) {
        m_phase = current;
        m_ipButton->setChecked(current == QLatin1String("ip"));
        m_oopButton->setChecked(current == QLatin1String("oop"));
        refresh();
    }
}

void RolePhaseSwitch::refresh()
{
    if (!m_combo)
        return;
    auto *model = qobject_cast<QStandardItemModel *>(m_combo->model());
    auto *view = qobject_cast<QListView *>(m_combo->view());
    bool phased = false;
    for (int row = 0; row < m_combo->count() && !phased; ++row)
        phased = !phaseOf(row).isEmpty();
    setVisible(phased);
    for (int row = 0; row < m_combo->count(); ++row) {
        const QString rowPhase = phaseOf(row);
        const bool hide = phased && !rowPhase.isEmpty() && rowPhase != m_phase;
        if (view)
            view->setRowHidden(row, hide);
        if (!model)
            continue;
        QStandardItem *item = model->item(row);
        if (!item)
            continue;
        // Disabled items are skipped by the keys and the mouse wheel too.
        const bool hiddenBefore = item->data(HiddenByPhaseRole).toBool();
        if (hide && !hiddenBefore) {
            item->setData(true, HiddenByPhaseRole);
            item->setEnabled(false);
        } else if (!hide && hiddenBefore) {
            item->setData(QVariant(), HiddenByPhaseRole);
            item->setEnabled(true);
        }
    }
}

} // namespace fm
