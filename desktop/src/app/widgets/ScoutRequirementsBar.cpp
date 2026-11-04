#include "ScoutRequirementsBar.h"

#include "core/Definitions.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace fm {

ScoutRequirementsBar::ScoutRequirementsBar(const Definitions &definitions, QWidget *parent)
    : QGroupBox(tr("🧩 Anforderungen (ein Kandidat muss alle erfüllen)"), parent)
    , m_definitions(definitions)
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(4);

    m_sourceLabel = new QLabel(this);
    m_sourceLabel->setObjectName(QStringLiteral("kpiCaption"));
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setVisible(false);
    layout->addWidget(m_sourceLabel);

    m_emptyLabel = new QLabel(
        tr("Keine. Wähle eine Position, eine Rolle oder beides und füge sie hinzu. Mehrere "
           "Anforderungen gelten zusammen; die DWRS-Spanne oben gilt für jede Rolle. Eine "
           "Bridge-Suche startest du per Rechtsklick auf einen Platz in Best XI."),
        this);
    m_emptyLabel->setObjectName(QStringLiteral("kpiCaption"));
    m_emptyLabel->setWordWrap(true);
    layout->addWidget(m_emptyLabel);

    m_rowsLayout = new QVBoxLayout;
    m_rowsLayout->setSpacing(2);
    layout->addLayout(m_rowsLayout);

    auto *addRow = new QHBoxLayout;
    addRow->addWidget(new QLabel(tr("Position:"), this));
    m_positionCombo = new QComboBox(this);
    m_positionCombo->setMinimumWidth(110);
    addRow->addWidget(m_positionCombo);
    addRow->addSpacing(8);
    addRow->addWidget(new QLabel(tr("Rolle:"), this));
    m_roleCombo = new QComboBox(this);
    m_roleCombo->setMinimumWidth(220);
    addRow->addWidget(m_roleCombo);
    m_addButton = new QPushButton(tr("+ Hinzufügen"), this);
    m_addButton->setToolTip(
        tr("Position und Rolle zusammen: Der Spieler braucht die Position und die Rolle. "
           "Nur eines von beiden: Es zählt nur das."));
    addRow->addWidget(m_addButton);
    m_clearButton = new QPushButton(tr("Alle entfernen"), this);
    addRow->addWidget(m_clearButton);
    addRow->addStretch(1);
    layout->addLayout(addRow);

    setPositionChoices({});
    setRoleChoices({});

    const auto updateAdd = [this] {
        m_addButton->setEnabled(!m_positionCombo->currentData().toString().isEmpty()
                                || !m_roleCombo->currentData().toString().isEmpty());
    };
    connect(m_positionCombo, &QComboBox::currentIndexChanged, this, updateAdd);
    connect(m_roleCombo, &QComboBox::currentIndexChanged, this, updateAdd);
    updateAdd();

    connect(m_addButton, &QPushButton::clicked, this, [this] {
        const ScoutCriterion c = ScoutSearch::criterion(ScoutSearch::positionPlace(
            m_positionCombo->currentData().toString(), m_roleCombo->currentData().toString()));
        if (c.places.front().positions.isEmpty() && c.places.front().role.isEmpty())
            return;
        if (!m_criteria.contains(c))
            m_criteria.append(c);
        m_source.clear(); // the list is the user's own now
        m_positionCombo->setCurrentIndex(0);
        m_roleCombo->setCurrentIndex(0);
        rebuildRows();
        emit criteriaChanged();
    });
    connect(m_clearButton, &QPushButton::clicked, this, [this] {
        m_criteria.clear();
        m_source.clear();
        rebuildRows();
        emit criteriaChanged();
    });
    rebuildRows();
}

void ScoutRequirementsBar::setRoleChoices(const QStringList &roles)
{
    const QString previous = m_roleCombo->currentData().toString();
    const QSignalBlocker blocker(m_roleCombo);
    m_roleCombo->clear();
    m_roleCombo->addItem(tr("beliebig"), QString());
    const auto names = m_definitions.roleDisplayMap();
    for (const QString &role : roles)
        m_roleCombo->addItem(names.value(role, role), role);
    m_roleCombo->setCurrentIndex(std::max(0, m_roleCombo->findData(previous)));
    m_addButton->setEnabled(!m_positionCombo->currentData().toString().isEmpty()
                            || !m_roleCombo->currentData().toString().isEmpty());
}

void ScoutRequirementsBar::setPositionChoices(const QStringList &positions)
{
    const QString previous = m_positionCombo->currentData().toString();
    const QSignalBlocker blocker(m_positionCombo);
    m_positionCombo->clear();
    m_positionCombo->addItem(tr("beliebig"), QString());
    for (const QString &position : positions)
        m_positionCombo->addItem(position, position);
    m_positionCombo->setCurrentIndex(std::max(0, m_positionCombo->findData(previous)));
}

void ScoutRequirementsBar::setCriteria(const QList<ScoutCriterion> &criteria,
                                       const QString &source)
{
    m_criteria = criteria;
    m_source = source;
    rebuildRows();
}

QString ScoutRequirementsBar::describePlace(const ScoutPlace &place) const
{
    const QString roleName = place.role.isEmpty()
                                 ? QString()
                                 : m_definitions.roleDisplayMap().value(place.role, place.role);
    if (place.positionLabel.isEmpty())
        return tr("Rolle %1").arg(roleName);
    if (roleName.isEmpty())
        return tr("Position %1").arg(place.positionLabel);
    return QStringLiteral("%1 · %2").arg(place.positionLabel, roleName);
}

QString ScoutRequirementsBar::describe(const ScoutCriterion &criterion) const
{
    if (criterion.places.size() == 2) {
        // A bridge: the place with ball first, as the Best XI hands it over.
        return tr("Bridge: mit Ball %1 + ohne Ball %2")
            .arg(describePlace(criterion.places[0]), describePlace(criterion.places[1]));
    }
    QStringList parts;
    for (const ScoutPlace &place : criterion.places)
        parts << describePlace(place);
    return parts.join(QStringLiteral(" + "));
}

void ScoutRequirementsBar::rebuildRows()
{
    while (QLayoutItem *item = m_rowsLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    for (qsizetype i = 0; i < m_criteria.size(); ++i) {
        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *remove = new QToolButton(row);
        remove->setText(QStringLiteral("✕"));
        remove->setAutoRaise(true);
        remove->setToolTip(tr("Anforderung entfernen"));
        remove->setCursor(Qt::PointingHandCursor);
        rowLayout->addWidget(remove);
        auto *text = new QLabel(QStringLiteral("<b>%1</b>").arg(describe(m_criteria.at(i)).toHtmlEscaped()), row);
        text->setTextFormat(Qt::RichText);
        rowLayout->addWidget(text, 1);
        m_rowsLayout->addWidget(row);
        const ScoutCriterion criterion = m_criteria.at(i);
        connect(remove, &QToolButton::clicked, this, [this, criterion] {
            m_criteria.removeOne(criterion);
            m_source.clear();
            rebuildRows();
            emit criteriaChanged();
        });
    }
    m_sourceLabel->setText(m_source);
    m_sourceLabel->setVisible(!m_source.isEmpty() && !m_criteria.isEmpty());
    m_emptyLabel->setVisible(m_criteria.isEmpty());
    m_clearButton->setVisible(!m_criteria.isEmpty());
}

} // namespace fm
