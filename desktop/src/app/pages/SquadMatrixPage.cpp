#include "SquadMatrixPage.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../widgets/PersonalityFilterWidget.h"
#include "../widgets/PlayerTableModel.h"
#include "core/Attributes.h"
#include "core/Constants.h"
#include "core/Freshness.h"
#include "core/TalentEngine.h"
#include "core/PlayerStatus.h"
#include "core/Utils.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace fm {

namespace {

enum TalentScope { ScopeDomestic = 0, ScopeForeign = 1, ScopeAny = 2 };
enum SquadScope { SquadFirstTeam = 0, SquadSecondTeam = 1, SquadBoth = 2 };
enum Tab { TabScouting = 0, TabSquad = 1 };

// Per-database settings of the scouting attribute thresholds.
const QString kAttrThresholdsKey = QStringLiteral("scouting_attr_thresholds"); // "Decisions:15,…"
const QString kAttrEnabledKey = QStringLiteral("scouting_attr_enabled");
const QString kAttrOnlyMatchesKey = QStringLiteral("scouting_attr_only_matches");
const QString kTabKey = QStringLiteral("squad_matrix_tab");

constexpr int kDefaultThreshold = 14;

QString millions(double value)
{
    return QStringLiteral("%1 Mio. €").arg(QLocale().toString(value / 1'000'000.0, 'f', 1));
}

// Layout for the contents of an optional (checkable) filter box. The contents
// only show while the box is checked, so switched-off refinements collapse to
// their title and leave the room to the results table.
QVBoxLayout *collapsibleBody(QGroupBox *box)
{
    auto *boxLayout = new QVBoxLayout(box);
    auto *body = new QWidget(box);
    boxLayout->addWidget(body);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    body->setVisible(box->isChecked());
    QObject::connect(box, &QGroupBox::toggled, body, &QWidget::setVisible);
    return bodyLayout;
}

// Highlight for a value that reaches its threshold.
CellStyle thresholdMetStyle()
{
    return {QColor(QStringLiteral("#0da025")), QColorConstants::White};
}

} // namespace

SquadMatrixPage::SquadMatrixPage(AppContext &context, QWidget *parent)
    : PageBase(context, parent)
{
    // Short export headers ("Dec") for the attribute-threshold columns.
    const QHash<QString, QString> &mapping = attributeMapping(m_context.fmVersionId());
    for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it) {
        const int attrIndex = attrIndexByName(it.value());
        if (attrIndex < 0)
            continue;
        const QString current = m_attrShortNames.value(attrIndex);
        if (current.isEmpty() || it.key().size() < current.size())
            m_attrShortNames.insert(attrIndex, it.key());
    }

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

    auto *heading = new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Squad Matrix")), content);
    layout->addWidget(heading);

    // --- Options shared by both tabs ---
    auto *optionsRow = new QHBoxLayout;
    optionsRow->addWidget(new QLabel(tr("Taktik:"), content));
    m_tacticCombo = new QComboBox(content);
    m_tacticCombo->setMinimumWidth(240);
    optionsRow->addWidget(m_tacticCombo);
    m_extraDetailsCheck = new QCheckBox(tr("Extra-Details"), content);
    optionsRow->addWidget(m_extraDetailsCheck);
    optionsRow->addStretch(1);
    layout->addLayout(optionsRow);

    m_tabs = new QTabWidget(content);
    m_tabs->addTab(buildScoutingTab(), tr("🔍 Scouting"));
    m_tabs->addTab(buildSquadTab(), tr("🏠 Mein Kader"));
    layout->addWidget(m_tabs, 1);

    // --- Signals ---
    const auto fullRebuild = [this] {
        if (!m_updating)
            rebuildAll();
    };
    connect(m_tacticCombo, &QComboBox::currentIndexChanged, this, fullRebuild);
    connect(m_extraDetailsCheck, &QCheckBox::toggled, this, fullRebuild);
    connect(m_squadScope, &QButtonGroup::idClicked, this, fullRebuild);
    connect(m_youthOnlyCheck, &QCheckBox::toggled, this, fullRebuild);
    connect(m_hideRetiredCheck, &QCheckBox::toggled, this, fullRebuild);
    connect(m_personalityFilter, &PersonalityFilterWidget::filterChanged, this, fullRebuild);
    connect(m_talentBox, &QGroupBox::toggled, this, fullRebuild);
    connect(m_talentScope, &QButtonGroup::idClicked, this, fullRebuild);
    connect(m_talentGoodOnlyCheck, &QCheckBox::toggled, this, fullRebuild);
    connect(m_talentAgeSlider, &QSlider::valueChanged, this, [this, fullRebuild] {
        m_talentAgeLabel->setText(tr("%1 Jahre").arg(m_talentAgeSlider->value()));
        fullRebuild();
    });
    connect(m_talentMentalitySlider, &QSlider::valueChanged, this, [this, fullRebuild] {
        m_talentMentalityLabel->setText(QString::number(m_talentMentalitySlider->value()));
        fullRebuild();
    });
    m_talentAgeLabel->setText(tr("%1 Jahre").arg(m_talentAgeSlider->value()));
    m_talentMentalityLabel->setText(QString::number(m_talentMentalitySlider->value()));

    const auto scoutedRebuild = [this] {
        if (!m_updating)
            rebuildScoutedOnly();
    };
    connect(m_scoutedFilterCombo, &QComboBox::currentIndexChanged, this, scoutedRebuild);
    connect(m_freeAgentsCheck, &QCheckBox::toggled, this, scoutedRebuild);
    connect(m_scoutedNatCombo, &QComboBox::currentIndexChanged, this, scoutedRebuild);
    connect(m_dwrsMinSpin, &QSpinBox::valueChanged, this, scoutedRebuild);
    connect(m_dwrsMaxSpin, &QSpinBox::valueChanged, this, scoutedRebuild);
    connect(m_minAgeSpin, &QSpinBox::valueChanged, this, scoutedRebuild);
    connect(m_maxAgeSpin, &QSpinBox::valueChanged, this, scoutedRebuild);
    const auto updateMaxValueLabel = [this] {
        // Right end of the slider means "no cap" so that "Not for Sale"
        // players (kUnbuyableValue) stay visible.
        m_maxValueLabel->setText(m_maxValueSlider->value() >= m_maxValueSlider->maximum()
                                     ? tr("Kein Limit")
                                     : millions(m_maxValueSlider->value() * 500'000.0));
    };
    connect(m_maxValueSlider, &QSlider::valueChanged, this,
            [updateMaxValueLabel, scoutedRebuild] {
                updateMaxValueLabel();
                scoutedRebuild();
            });
    updateMaxValueLabel();

    connect(m_attrBox, &QGroupBox::toggled, this, [this](bool on) {
        // Switching the panel on without a row starts with a useful example.
        if (on && m_attrRows.isEmpty() && !m_updating)
            addAttributeRow(idx(Attr::Decisions), kDefaultThreshold);
        attributeThresholdsChanged();
    });
    connect(m_attrOnlyMatchesCheck, &QCheckBox::toggled, this,
            [this] { attributeThresholdsChanged(); });

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (!m_updating)
            m_context.database().setSetting(kTabKey, QString::number(index));
    });
}

QWidget *SquadMatrixPage::buildScoutingTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    // Row 1: what to look at.
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Filter/Sortierung:"), tab));
    m_scoutedFilterCombo = new QComboBox(tab);
    m_scoutedFilterCombo->setMinimumWidth(200);
    m_scoutedFilterCombo->setToolTip(
        tr("Nach einer Rolle sortieren und filtern. Dann zeigt eine Spalte „Δ“, wie weit der "
           "Spieler über oder unter dem besten Spieler deiner ersten Mannschaft liegt."));
    filterRow->addWidget(m_scoutedFilterCombo);
    filterRow->addWidget(new QLabel(tr("Nation:"), tab));
    m_scoutedNatCombo = new QComboBox(tab);
    m_scoutedNatCombo->setMinimumWidth(150);
    m_scoutedNatCombo->setToolTip(
        tr("Nach Nationalität einschränken (unabhängig vom Talent-Filter): eigenes Land, "
           "Ausländer oder eine bestimmte Nation — hilfreich für Registrierungsregeln."));
    filterRow->addWidget(m_scoutedNatCombo);
    m_freeAgentsCheck = new QCheckBox(tr("Nur Free Agents"), tab);
    m_freeAgentsCheck->setToolTip(
        tr("Nur ablösefreie Spieler: Marktwert explizit 0 oder kein Verein gesetzt "
           "(unabhängig davon, wie der Verein benannt ist). Marktwert-Limit wird dabei "
           "ignoriert; nach Rolle/Talent sortierbar."));
    filterRow->addWidget(m_freeAgentsCheck);
    m_hideRetiredCheck = new QCheckBox(tr("Retired/veraltete ausblenden"), tab);
    m_hideRetiredCheck->setToolTip(
        tr("Blendet Spieler aus, deren Verein auf 'Retired' steht oder die in den "
           "letzten Komplett-Exporten fehlten (außer eigenem Verein und Shortlist)."));
    m_hideRetiredCheck->setChecked(true);
    filterRow->addWidget(m_hideRetiredCheck);
    filterRow->addStretch(1);
    layout->addLayout(filterRow);

    // Row 2: ranges.
    auto *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel(tr("DWRS:"), tab));
    m_dwrsMinSpin = new QSpinBox(tab);
    m_dwrsMinSpin->setRange(0, 100);
    m_dwrsMinSpin->setValue(20);
    m_dwrsMaxSpin = new QSpinBox(tab);
    m_dwrsMaxSpin->setRange(0, 100);
    m_dwrsMaxSpin->setValue(100);
    rangeRow->addWidget(m_dwrsMinSpin);
    rangeRow->addWidget(new QLabel(QStringLiteral("–"), tab));
    rangeRow->addWidget(m_dwrsMaxSpin);
    rangeRow->addSpacing(16);
    rangeRow->addWidget(new QLabel(tr("Alter:"), tab));
    m_minAgeSpin = new QSpinBox(tab);
    m_minAgeSpin->setRange(15, 50);
    m_minAgeSpin->setValue(15); // default: as low as possible
    m_maxAgeSpin = new QSpinBox(tab);
    m_maxAgeSpin->setRange(15, 50);
    m_maxAgeSpin->setValue(30);
    rangeRow->addWidget(m_minAgeSpin);
    rangeRow->addWidget(new QLabel(QStringLiteral("–"), tab));
    rangeRow->addWidget(m_maxAgeSpin);
    rangeRow->addSpacing(16);
    rangeRow->addWidget(new QLabel(tr("Max. Wert:"), tab));
    m_maxValueSlider = new QSlider(Qt::Horizontal, tab);
    m_maxValueSlider->setRange(0, 400); // 0.5 Mio € steps
    m_maxValueSlider->setValue(400);
    m_maxValueSlider->setMaximumWidth(160);
    m_maxValueLabel = new QLabel(tab);
    rangeRow->addWidget(m_maxValueSlider);
    rangeRow->addWidget(m_maxValueLabel);
    rangeRow->addStretch(1);
    layout->addLayout(rangeRow);

    m_personalityFilter = new PersonalityFilterWidget(m_context.definitions(), tab);
    layout->addWidget(m_personalityFilter);

    // --- Talent filter ---
    m_talentBox = new QGroupBox(tr("🌱 Talent-Filter"), tab);
    m_talentBox->setCheckable(true);
    m_talentBox->setChecked(false);
    auto *talentLayout = collapsibleBody(m_talentBox);
    auto *scopeRow = new QHBoxLayout;
    scopeRow->addWidget(new QLabel(tr("Nationalität:"), m_talentBox));
    m_talentScope = new QButtonGroup(this);
    const auto addScope = [&](const QString &label, int id, bool checked = false) {
        auto *radio = new QRadioButton(label, m_talentBox);
        radio->setChecked(checked);
        m_talentScope->addButton(radio, id);
        scopeRow->addWidget(radio);
    };
    addScope(tr("Inland"), ScopeDomestic);
    addScope(tr("Ausland"), ScopeForeign);
    addScope(tr("Beliebig"), ScopeAny, true);
    scopeRow->addStretch(1);
    talentLayout->addLayout(scopeRow);

    auto *talentSliders = new QHBoxLayout;
    talentSliders->addWidget(new QLabel(tr("Max. Alter:"), m_talentBox));
    m_talentAgeSlider = new QSlider(Qt::Horizontal, m_talentBox);
    m_talentAgeSlider->setRange(15, 24);
    m_talentAgeSlider->setValue(21);
    m_talentAgeLabel = new QLabel(m_talentBox);
    talentSliders->addWidget(m_talentAgeSlider, 1);
    talentSliders->addWidget(m_talentAgeLabel);
    talentSliders->addSpacing(16);
    talentSliders->addWidget(new QLabel(tr("Min. Ent + Arb:"), m_talentBox));
    m_talentMentalitySlider = new QSlider(Qt::Horizontal, m_talentBox);
    m_talentMentalitySlider->setRange(0, 40);
    m_talentMentalitySlider->setValue(20);
    m_talentMentalityLabel = new QLabel(m_talentBox);
    talentSliders->addWidget(m_talentMentalitySlider, 1);
    talentSliders->addWidget(m_talentMentalityLabel);
    talentSliders->addSpacing(16);
    m_talentGoodOnlyCheck = new QCheckBox(tr("Nur gute Persönlichkeiten"), m_talentBox);
    talentSliders->addWidget(m_talentGoodOnlyCheck);
    talentLayout->addLayout(talentSliders);

    auto *talentHint = new QLabel(
        tr("Talent-Score = bester DWRS der angezeigten Rollen + 2 je Jahr unter der "
           "Altersgrenze + (Ent + Arb − 20) / 4 + 3 (gute) / − 5 (schlechte Persönlichkeit)."),
        m_talentBox);
    talentHint->setObjectName(QStringLiteral("kpiCaption"));
    talentHint->setWordWrap(true);
    talentLayout->addWidget(talentHint);
    layout->addWidget(m_talentBox);

    // --- Attribute thresholds ---
    m_attrBox = new QGroupBox(tr("🎯 Attribut-Schwellen"), tab);
    m_attrBox->setCheckable(true);
    m_attrBox->setChecked(false);
    auto *attrLayout = collapsibleBody(m_attrBox);
    auto *attrHint = new QLabel(
        tr("Hebt Spieler hervor, deren Attribut die Schwelle erreicht, z. B. Decisions für "
           "einen AM(C) oder Crossing für Außenverteidiger. Jede Schwelle bekommt eine "
           "eigene Spalte, „Schwellen“ zählt die erfüllten. Gerechnet wird mit der Mitte "
           "der gescouteten Spanne."),
        m_attrBox);
    attrHint->setObjectName(QStringLiteral("kpiCaption"));
    attrHint->setWordWrap(true);
    attrLayout->addWidget(attrHint);
    m_attrRowsLayout = new QVBoxLayout;
    m_attrRowsLayout->setSpacing(4);
    attrLayout->addLayout(m_attrRowsLayout);
    auto *attrButtons = new QHBoxLayout;
    auto *addButton = new QPushButton(tr("+ Attribut"), m_attrBox);
    connect(addButton, &QPushButton::clicked, this, [this] {
        addAttributeRow(idx(Attr::Decisions), kDefaultThreshold);
        attributeThresholdsChanged();
    });
    attrButtons->addWidget(addButton);
    attrButtons->addSpacing(16);
    m_attrOnlyMatchesCheck =
        new QCheckBox(tr("Nur Spieler zeigen, die alle Schwellen erfüllen"), m_attrBox);
    attrButtons->addWidget(m_attrOnlyMatchesCheck);
    attrButtons->addStretch(1);
    attrLayout->addLayout(attrButtons);
    layout->addWidget(m_attrBox);

    buildTable(&m_scoutedSection, tr("Gescoutete Spieler"), tab, true);
    layout->addWidget(m_scoutedSection.box, 1);
    return tab;
}

QWidget *SquadMatrixPage::buildSquadTab()
{
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *optionsRow = new QHBoxLayout;
    // Team scope, only shown when a second team is configured.
    m_squadScopeBox = new QWidget(tab);
    auto *scopeRow = new QHBoxLayout(m_squadScopeBox);
    scopeRow->setContentsMargins(0, 0, 0, 0);
    scopeRow->addWidget(new QLabel(tr("Anzeigen:"), m_squadScopeBox));
    m_squadScope = new QButtonGroup(this);
    const auto addScope = [&](const QString &label, int id, bool checked = false) {
        auto *radio = new QRadioButton(label, m_squadScopeBox);
        radio->setChecked(checked);
        m_squadScope->addButton(radio, id);
        scopeRow->addWidget(radio);
    };
    addScope(tr("Erste Mannschaft"), SquadFirstTeam);
    addScope(tr("Zweitteam"), SquadSecondTeam);
    addScope(tr("Beide"), SquadBoth, true);
    scopeRow->addSpacing(16);
    optionsRow->addWidget(m_squadScopeBox);
    m_youthOnlyCheck = new QCheckBox(tab);
    m_youthOnlyCheck->setToolTip(
        tr("Nur Spieler bis zur Jugend-Altersgrenze aus den Einstellungen "
           "(Feldspieler und Torhüter getrennt)."));
    optionsRow->addWidget(m_youthOnlyCheck);
    optionsRow->addStretch(1);
    layout->addLayout(optionsRow);

    buildTable(&m_clubSection, tr("Mein Verein"), tab, false);
    layout->addWidget(m_clubSection.box, 1);
    return tab;
}

void SquadMatrixPage::buildTable(TableSection *section, const QString &title, QWidget *parent,
                                 bool scouted)
{
    section->box = new QGroupBox(title, parent);
    auto *layout = new QVBoxLayout(section->box);

    auto *searchRow = new QHBoxLayout;
    section->search = new QLineEdit(section->box);
    section->search->setPlaceholderText(tr("Nach Name suchen…"));
    section->search->setClearButtonEnabled(true);
    auto *exportButton = new QPushButton(tr("CSV exportieren"), section->box);
    searchRow->addWidget(section->search, 1);
    searchRow->addWidget(exportButton);
    layout->addLayout(searchRow);

    section->model = new PlayerTableModel(this);
    section->proxy = new PlayerFilterProxy(section->model, this);
    section->table = new QTableView(section->box);
    section->table->setModel(section->proxy);
    section->table->setSortingEnabled(true);
    // Start A-Z by name (Qt's default indicator would sort the first column Z-A).
    section->table->sortByColumn(scouted ? 1 : 0, Qt::AscendingOrder);
    section->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    section->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    section->table->setAlternatingRowColors(true);
    section->table->verticalHeader()->setVisible(false);
    section->table->setMinimumHeight(520);
    layout->addWidget(section->table, 1);

    connect(section->search, &QLineEdit::textChanged, this,
            [proxy = section->proxy](const QString &text) { proxy->setNameFilter(text); });
    connect(exportButton, &QPushButton::clicked, this,
            [this, section] { exportCsv(*section); });

    // Right-click menu + double-click profile on every table; the scouted
    // star column keeps its single-click shortlist toggle.
    PlayerActions::attachToView(m_context, section->table, section->proxy,
                                scouted ? 0 : -1);
    if (scouted) {
        connect(section->table, &QTableView::clicked, this,
                [this, section](const QModelIndex &index) {
                    if (index.column() != 0)
                        return;
                    if (const Player *player = section->proxy->playerAt(index.row()))
                        toggleShortlist(player);
                });
    }
}

QStringList SquadMatrixPage::selectedRoles() const
{
    const QString tactic = m_tacticCombo->currentData().toString();
    QStringList roles;
    if (tactic.isEmpty()) { // "Alle Rollen"
        roles = m_context.definitions().validRoles();
    } else {
        const auto positions = m_context.definitions().tacticRoles().value(tactic);
        QSet<QString> roleSet;
        for (auto it = positions.constBegin(); it != positions.constEnd(); ++it)
            roleSet.insert(it.value());
        roles = QStringList(roleSet.cbegin(), roleSet.cend());
    }
    return m_context.definitions().sortRolesNaturally(roles);
}

QList<PlayerColumn> SquadMatrixPage::buildColumns(bool clubTable) const
{
    const bool talentOn = !clubTable && m_talentBox->isChecked();
    const bool extraDetails = m_extraDetailsCheck->isChecked();
    const QStringList roles = selectedRoles();
    const RoleRatings &ratings = m_context.ratings();
    const Definitions *definitions = &m_context.definitions();
    const QHash<QString, double> talentScores = m_talentScores;

    QList<PlayerColumn> columns;
    if (!clubTable) {
        columns.append({tr("★"),
                        [](const Player &p) {
                            return p.onShortlist ? QStringLiteral("★") : QStringLiteral("☆");
                        },
                        [](const Player &p) { return p.onShortlist ? 1 : 0; }, nullptr,
                        Qt::AlignCenter});
    }
    columns.append({tr("Name"), [](const Player &p) { return p.name; },
                    [](const Player &p) { return getLastName(p.name); }, nullptr, {}});
    columns.append({tr("Alter"), [](const Player &p) { return p.age; }, nullptr, nullptr,
                    Qt::AlignRight | Qt::AlignVCenter});
    if (talentOn) {
        columns.append({tr("Talent"),
                        [talentScores](const Player &p) {
                            return qRound(talentScores.value(p.uid, 0.0));
                        },
                        nullptr,
                        [talentScores](const Player &p) {
                            return dwrsCellStyle(talentScores.value(p.uid, 0.0));
                        },
                        Qt::AlignRight | Qt::AlignVCenter});
    }
    // Scouting focus (chosen role, delta, attribute thresholds) goes here,
    // right after name and age, so what is being searched for stays in view.
    const qsizetype focusIndex = columns.size();
    columns.append(
        {tr("Position"), [](const Player &p) { return p.positionRaw; }, nullptr, nullptr, {}});
    columns.append({tr("Persönlichkeit"), [](const Player &p) { return p.personality; },
                    nullptr,
                    [definitions](const Player &p) {
                        return personalityCellStyle(
                            definitions->personalityCategory(p.personality));
                    },
                    {}});
    if (!clubTable) {
        columns.append(
            {tr("Verein"), [](const Player &p) { return p.club; }, nullptr, nullptr, {}});
        columns.append({tr("Marktwert"), [](const Player &p) { return p.transferValueRaw; },
                        [](const Player &p) { return p.transferValue; }, nullptr,
                        Qt::AlignRight | Qt::AlignVCenter});
        columns.append(makeFreshnessColumn(
            m_context.updateCounter(),
            m_context.config().freshnessSetting(QStringLiteral("retirement_age")),
            m_context.config().freshnessSetting(QStringLiteral("stale_after_full_uploads")),
            m_context.userClub()));
    }
    columns.append({tr("Gehalt"), [](const Player &p) { return p.wageRaw; }, nullptr, nullptr,
                    Qt::AlignRight | Qt::AlignVCenter});
    if (extraDetails) {
        columns.append({tr("Linker Fuß"), [](const Player &p) { return p.leftFoot; }, nullptr,
                        nullptr, {}});
        columns.append({tr("Rechter Fuß"), [](const Player &p) { return p.rightFoot; }, nullptr,
                        nullptr, {}});
        columns.append({tr("Größe"), [](const Player &p) { return p.heightRaw; },
                        [](const Player &p) { return p.heightCm; }, nullptr,
                        Qt::AlignRight | Qt::AlignVCenter});
        if (clubTable) {
            columns.append({tr("Marktwert"),
                            [](const Player &p) { return p.transferValueRaw; },
                            [](const Player &p) { return p.transferValue; }, nullptr,
                            Qt::AlignRight | Qt::AlignVCenter});
        }
    }
    if (talentOn) {
        const auto attrColumn = [](const QString &header, Attr attr) {
            const int attrIndex = idx(attr);
            return PlayerColumn{header,
                                [attrIndex](const Player &p) {
                                    return static_cast<int>(p.attrMean(attrIndex));
                                },
                                nullptr,
                                [attrIndex](const Player &p) {
                                    return attributeCellStyle(
                                        static_cast<int>(p.attrMean(attrIndex)));
                                },
                                Qt::AlignRight | Qt::AlignVCenter};
        };
        columns.append(attrColumn(tr("Ent"), Attr::Determination));
        columns.append(attrColumn(tr("Arb"), Attr::WorkRate));
    }

    // Attribute thresholds: a met/total counter plus one column per threshold,
    // the value highlighted once it reaches the minimum.
    const QList<QPair<int, int>> thresholds =
        clubTable ? QList<QPair<int, int>>() : activeThresholds();
    QList<PlayerColumn> thresholdColumns;
    if (!thresholds.isEmpty()) {
        const auto metCount = [thresholds](const Player &p) {
            int met = 0;
            for (const auto &[attrIndex, minimum] : thresholds) {
                if (p.attrMean(attrIndex) >= minimum)
                    ++met;
            }
            return met;
        };
        const int total = static_cast<int>(thresholds.size());
        thresholdColumns.append({tr("Schwellen"),
                        [metCount, total](const Player &p) {
                            return QStringLiteral("%1/%2").arg(metCount(p)).arg(total);
                        },
                        [metCount](const Player &p) { return metCount(p); },
                        [metCount, total](const Player &p) {
                            return metCount(p) == total ? thresholdMetStyle() : CellStyle{};
                        },
                        Qt::AlignCenter});
        for (const auto &[attrIndex, minimum] : thresholds) {
            const QString shortName = m_attrShortNames.value(attrIndex, attrNames()[attrIndex]);
            thresholdColumns.append({QStringLiteral("%1 ≥%2").arg(shortName).arg(minimum),
                            [attrIndex](const Player &p) -> QVariant {
                                const double value = p.attrMean(attrIndex);
                                if (value <= 0.0)
                                    return QStringLiteral("–");
                                return QLocale().toString(value, 'g', 3);
                            },
                            [attrIndex](const Player &p) { return p.attrMean(attrIndex); },
                            [attrIndex, minimum](const Player &p) {
                                return p.attrMean(attrIndex) >= minimum ? thresholdMetStyle()
                                                                        : CellStyle{};
                            },
                            Qt::AlignRight | Qt::AlignVCenter});
        }
    }

    // Scouting by role: delta to the first team's best available player in
    // that role (a long-term injured one is no benchmark).
    const QString focusRole = clubTable ? QString() : m_scoutedFilterCombo->currentData().toString();
    double benchmark = -1.0;
    QString benchmarkName;
    if (!focusRole.isEmpty() && !focusRole.startsWith(QLatin1String("__"))) {
        const QHash<QString, double> roleRatings = ratings.value(focusRole);
        for (const Player *player : m_firstTeam) {
            if (player->longTermInjured)
                continue;
            const auto it = roleRatings.constFind(player->uid);
            if (it != roleRatings.constEnd() && it.value() > benchmark) {
                benchmark = it.value();
                benchmarkName = getLastName(player->name);
            }
        }
    }

    QList<PlayerColumn> focusColumns;
    for (const QString &role : roles) {
        const QHash<QString, double> roleRatings = ratings.value(role);
        QList<PlayerColumn> &target = role == focusRole ? focusColumns : columns;
        target.append({role,
                        [roleRatings](const Player &p) -> QVariant {
                            const auto it = roleRatings.constFind(p.uid);
                            if (it == roleRatings.constEnd())
                                return QStringLiteral("–");
                            return qRound(it.value());
                        },
                        [roleRatings](const Player &p) {
                            return roleRatings.value(p.uid, -1.0);
                        },
                        [roleRatings](const Player &p) -> CellStyle {
                            const auto it = roleRatings.constFind(p.uid);
                            if (it == roleRatings.constEnd())
                                return {};
                            return dwrsCellStyle(it.value());
                        },
                        Qt::AlignRight | Qt::AlignVCenter});
        if (role == focusRole && benchmark >= 0.0) {
            const int best = qRound(benchmark);
            focusColumns.append(
                {tr("Δ %1 (%2)").arg(benchmarkName).arg(best),
                 [roleRatings, best](const Player &p) -> QVariant {
                     const auto it = roleRatings.constFind(p.uid);
                     if (it == roleRatings.constEnd())
                         return QStringLiteral("–");
                     const int delta = qRound(it.value()) - best;
                     return delta > 0 ? QStringLiteral("+%1").arg(delta) : QString::number(delta);
                 },
                 [roleRatings, best](const Player &p) {
                     const auto it = roleRatings.constFind(p.uid);
                     return it == roleRatings.constEnd() ? -1000 : qRound(it.value()) - best;
                 },
                 [roleRatings, best](const Player &p) -> CellStyle {
                     const auto it = roleRatings.constFind(p.uid);
                     if (it == roleRatings.constEnd() || qRound(it.value()) <= best)
                         return {};
                     return thresholdMetStyle();
                 },
                 Qt::AlignRight | Qt::AlignVCenter});
        }
    }
    focusColumns.append(thresholdColumns);
    for (qsizetype i = 0; i < focusColumns.size(); ++i)
        columns.insert(focusIndex + i, focusColumns.at(i));
    return columns;
}

void SquadMatrixPage::releaseStoreRows()
{
    for (TableSection *section : {&m_clubSection, &m_scoutedSection}) {
        if (section->model)
            section->model->setRows({});
    }
    m_scoutedPool.clear(); // also caches raw Player*
    m_firstTeam.clear();
}

void SquadMatrixPage::refresh()
{
    m_updating = true;

    // Tactic list: favorites first (legacy order), then the rest.
    const QString previous = m_tacticCombo->currentData().toString();
    const bool hadSelection = m_tacticCombo->count() > 0;
    const QString fav1 = m_context.database().setting(QStringLiteral("favorite_tactic_1"));
    const QString fav2 = m_context.database().setting(QStringLiteral("favorite_tactic_2"));
    QStringList tactics = m_context.definitions().tacticNames();
    std::sort(tactics.begin(), tactics.end());
    QStringList ordered;
    if (!fav1.isEmpty() && tactics.contains(fav1))
        ordered << fav1;
    if (!fav2.isEmpty() && tactics.contains(fav2) && fav2 != fav1)
        ordered << fav2;
    for (const QString &tactic : tactics) {
        if (!ordered.contains(tactic))
            ordered << tactic;
    }
    m_tacticCombo->clear();
    m_tacticCombo->addItem(tr("Alle Rollen"), QString());
    for (const QString &tactic : ordered)
        m_tacticCombo->addItem(tactic, tactic);
    if (hadSelection && !previous.isEmpty()) {
        const int index = m_tacticCombo->findData(previous);
        m_tacticCombo->setCurrentIndex(index >= 0 ? index : 0);
    } else if (!hadSelection && !fav1.isEmpty()) {
        m_tacticCombo->setCurrentIndex(1); // first favorite (legacy default)
    }

    QSet<QString> personalitySet;
    QSet<QString> nationalitySet;
    for (const Player &player : m_context.store().players()) {
        if (!player.personality.trimmed().isEmpty())
            personalitySet.insert(player.personality);
        if (!player.nationality.trimmed().isEmpty())
            nationalitySet.insert(player.nationality.trimmed());
    }
    QStringList personalities(personalitySet.cbegin(), personalitySet.cend());
    std::sort(personalities.begin(), personalities.end());
    m_personalityFilter->setAvailablePersonalities(personalities);

    const QString clubCountry = m_context.database().setting(QStringLiteral("club_country_code"));

    // Scouted nationality filter combo: Any / Domestic / Foreign, then every
    // nationality present in the DB. Independent of the talent filter.
    QStringList nationalities(nationalitySet.cbegin(), nationalitySet.cend());
    std::sort(nationalities.begin(), nationalities.end());
    const QString previousNat = m_scoutedNatCombo->currentData().toString();
    m_scoutedNatCombo->clear();
    m_scoutedNatCombo->addItem(tr("Alle"), QStringLiteral("__any"));
    if (!clubCountry.isEmpty())
        m_scoutedNatCombo->addItem(tr("Inland (%1)").arg(clubCountry), QStringLiteral("__domestic"));
    m_scoutedNatCombo->addItem(tr("Ausländer"), QStringLiteral("__foreign"));
    m_scoutedNatCombo->insertSeparator(m_scoutedNatCombo->count());
    for (const QString &nat : nationalities)
        m_scoutedNatCombo->addItem(nat, nat);
    const int previousNatIndex =
        previousNat.isEmpty() ? 0 : m_scoutedNatCombo->findData(previousNat);
    m_scoutedNatCombo->setCurrentIndex(previousNatIndex >= 0 ? previousNatIndex : 0);

    if (auto *domestic = m_talentScope->button(ScopeDomestic)) {
        domestic->setText(clubCountry.isEmpty() ? tr("Inland (Land fehlt in Einstellungen)")
                                                : tr("Inland (%1)").arg(clubCountry));
        domestic->setEnabled(!clubCountry.isEmpty());
        if (clubCountry.isEmpty() && domestic->isChecked())
            m_talentScope->button(ScopeAny)->setChecked(true);
    }

    // Team scopes only make sense with a second team configured.
    m_squadScopeBox->setVisible(!m_context.secondTeamClub().isEmpty());
    m_youthOnlyCheck->setText(
        tr("Nur Jugend (bis %1, TW bis %2)")
            .arg(m_context.config().ageThreshold(QStringLiteral("outfielder")))
            .arg(m_context.config().ageThreshold(QStringLiteral("goalkeeper"))));

    // Per-database state: attribute thresholds and the last opened tab.
    if (m_attrLoadedDb != m_context.currentDbName()) {
        m_attrLoadedDb = m_context.currentDbName();
        loadAttributeThresholds();
        const int tab = m_context.database().setting(kTabKey, QString::number(TabScouting)).toInt();
        m_tabs->setCurrentIndex(tab == TabSquad ? TabSquad : TabScouting);
    }

    m_updating = false;
    rebuildAll();
}

void SquadMatrixPage::rebuildAll()
{
    const QString userClub = m_context.userClub();
    const QString secondClub = m_context.secondTeamClub();
    const QStringList roles = selectedRoles();
    const RoleRatings &ratings = m_context.ratings();
    const bool talentOn = m_talentBox->isChecked();
    const bool hideRetired = m_hideRetiredCheck->isChecked();
    const QString clubCountry = m_context.database().setting(QStringLiteral("club_country_code"));

    const int talentAgeCap = m_talentAgeSlider->value();
    const int talentMinMentality = m_talentMentalitySlider->value();
    const bool talentGoodOnly = m_talentGoodOnlyCheck->isChecked();
    const int talentScope = m_talentScope->checkedId();

    m_talentScores.clear();
    const auto passesTalent = [&](const Player &p) -> bool {
        if (!talentOn)
            return true;
        if (p.age <= 0 || p.age > talentAgeCap)
            return false;
        const double mentality =
            p.attrMean(idx(Attr::Determination)) + p.attrMean(idx(Attr::WorkRate));
        if (mentality < talentMinMentality)
            return false;
        if (talentScope == ScopeDomestic) {
            if (p.nationality != clubCountry && p.secondNationality != clubCountry)
                return false;
        } else if (talentScope == ScopeForeign) {
            if (p.nationality.isEmpty())
                return false;
            if (!clubCountry.isEmpty()
                && (p.nationality == clubCountry || p.secondNationality == clubCountry))
                return false;
        }
        if (talentGoodOnly
            && m_context.definitions().personalityCategory(p.personality)
                   != QLatin1String("good")) {
            return false;
        }
        return true;
    };

    // The club tabs are never filtered; the scouting filters only shape the
    // scouting pool.
    const PlayerStatus::FreshnessContext freshness = m_context.freshnessContext();
    std::vector<const Player *> mine, second, scouted;
    for (const Player &player : m_context.store().players()) {
        if (!userClub.isEmpty() && player.club == userClub) {
            mine.push_back(&player);
            continue;
        }
        if (!secondClub.isEmpty() && player.club == secondClub) {
            second.push_back(&player);
            continue;
        }
        if (hideRetired && PlayerStatus::isInactive(player, freshness))
            continue;
        if (!m_personalityFilter->allows(player.personality))
            continue;
        if (!passesTalent(player))
            continue;
        if (talentOn) {
            double bestDwrs = 0.0;
            for (const QString &role : roles) {
                const auto roleIt = ratings.constFind(role);
                if (roleIt != ratings.constEnd())
                    bestDwrs = std::max(bestDwrs, roleIt.value().value(player.uid, 0.0));
            }
            m_talentScores.insert(player.uid,
                                  TalentEngine::talentForPlayer(m_context.definitions(), player,
                                                                bestDwrs, talentAgeCap));
        }
        scouted.push_back(&player);
    }
    m_firstTeam = mine;

    // --- "Mein Kader" ---
    const int scope = secondClub.isEmpty() ? SquadFirstTeam : m_squadScope->checkedId();
    std::vector<const Player *> clubRows;
    if (scope != SquadSecondTeam)
        clubRows.insert(clubRows.end(), mine.begin(), mine.end());
    if (scope != SquadFirstTeam)
        clubRows.insert(clubRows.end(), second.begin(), second.end());
    if (m_youthOnlyCheck->isChecked()) {
        const int outfielderCap = m_context.config().ageThreshold(QStringLiteral("outfielder"));
        const int goalkeeperCap = m_context.config().ageThreshold(QStringLiteral("goalkeeper"));
        std::erase_if(clubRows, [&](const Player *p) {
            return p->age <= 0
                   || p->age > TalentEngine::ageCapForPlayer(*p, outfielderCap, goalkeeperCap);
        });
    }
    std::sort(clubRows.begin(), clubRows.end(), [](const Player *a, const Player *b) {
        const int cmp = getLastName(a->name).localeAwareCompare(getLastName(b->name));
        return cmp != 0 ? cmp < 0 : a->name.localeAwareCompare(b->name) < 0;
    });
    const QString clubName = userClub.isEmpty() ? tr("Mein Verein") : userClub;
    QString clubTitle;
    if (scope == SquadSecondTeam)
        clubTitle = tr("Spieler von %1 (Zweitteam, %2)").arg(secondClub).arg(clubRows.size());
    else if (scope == SquadBoth)
        clubTitle = tr("Spieler von %1 & Zweitteam (%2)").arg(clubName).arg(clubRows.size());
    else
        clubTitle = tr("Spieler von %1 (%2)").arg(clubName).arg(clubRows.size());
    m_clubSection.box->setTitle(clubTitle);
    m_clubSection.model->setColumns(buildColumns(true));
    m_clubSection.model->setRows(std::move(clubRows));
    m_clubSection.table->resizeColumnsToContents();

    // --- Scouted filter combo (keep selection across rebuilds). ---
    m_updating = true;
    const auto roleNames = m_context.definitions().roleDisplayMap();
    const QString previousFilter = m_scoutedFilterCombo->currentData().toString();
    m_scoutedFilterCombo->clear();
    m_scoutedFilterCombo->addItem(tr("Name"), QStringLiteral("__name"));
    m_scoutedFilterCombo->addItem(tr("Shortlist"), QStringLiteral("__shortlist"));
    if (talentOn)
        m_scoutedFilterCombo->addItem(tr("Talent"), QStringLiteral("__talent"));
    for (const QString &role : roles)
        m_scoutedFilterCombo->addItem(roleNames.value(role, role), role);
    const int previousIndex = m_scoutedFilterCombo->findData(previousFilter);
    if (previousIndex >= 0)
        m_scoutedFilterCombo->setCurrentIndex(previousIndex);
    m_updating = false;

    m_scoutedPool = std::move(scouted);
    rebuildScoutedOnly();
}

void SquadMatrixPage::rebuildScoutedOnly()
{
    const RoleRatings &ratings = m_context.ratings();
    const QString filterKey = m_scoutedFilterCombo->currentData().toString();
    const int minDwrs = m_dwrsMinSpin->value();
    const int maxDwrs = m_dwrsMaxSpin->value();
    const int minAge = m_minAgeSpin->value();
    const int maxAge = m_maxAgeSpin->value();
    const bool freeAgentsOnly = m_freeAgentsCheck->isChecked();
    const QString natScope = m_scoutedNatCombo->currentData().toString();
    const QString clubCountry = m_context.database().setting(QStringLiteral("club_country_code"));
    // Slider at the right end = no value cap; otherwise "Not for Sale"
    // players (transferValue = kUnbuyableValue) could never pass the filter.
    // The value cap is also meaningless once "free agents only" is on (those
    // players are defined partly BY their value), so it is bypassed then.
    const bool noValueCap =
        freeAgentsOnly || m_maxValueSlider->value() >= m_maxValueSlider->maximum();
    const double maxValue = m_maxValueSlider->value() * 500'000.0;
    const QList<QPair<int, int>> thresholds =
        m_attrOnlyMatchesCheck->isChecked() ? activeThresholds() : QList<QPair<int, int>>();

    // Identity filters (free agent + nationality scope + attribute thresholds)
    // applied to every row, independent of the sort/DWRS/age/value filters.
    const auto passesNationality = [&](const Player *p) -> bool {
        if (natScope.isEmpty() || natScope == QLatin1String("__any"))
            return true;
        if (natScope == QLatin1String("__domestic"))
            return p->nationality == clubCountry || p->secondNationality == clubCountry;
        if (natScope == QLatin1String("__foreign")) {
            if (p->nationality.isEmpty())
                return false;
            return clubCountry.isEmpty()
                   || (p->nationality != clubCountry && p->secondNationality != clubCountry);
        }
        return p->nationality == natScope || p->secondNationality == natScope;
    };
    const auto passesIdentity = [&](const Player *p) -> bool {
        if (freeAgentsOnly && !isFreeAgent(p->club, p->transferValueRaw))
            return false;
        for (const auto &[attrIndex, minimum] : thresholds) {
            if (p->attrMean(attrIndex) < minimum)
                return false;
        }
        return passesNationality(p);
    };

    std::vector<const Player *> rows;
    if (filterKey == QLatin1String("__shortlist")) {
        for (const Player *player : m_scoutedPool) {
            if (!passesIdentity(player))
                continue;
            if (player->onShortlist)
                rows.push_back(player);
        }
    } else {
        const bool roleFilter = !filterKey.startsWith(QLatin1String("__"));
        const QHash<QString, double> roleRatings =
            roleFilter ? ratings.value(filterKey) : QHash<QString, double>();
        for (const Player *player : m_scoutedPool) {
            if (!passesIdentity(player))
                continue;
            // Unknown age (0) is never excluded by the age bounds.
            if ((player->age > 0 && player->age < minAge) || player->age > maxAge
                || (!noValueCap && player->transferValue > maxValue))
                continue;
            if (roleFilter) {
                const auto it = roleRatings.constFind(player->uid);
                if (it == roleRatings.constEnd() || it.value() < minDwrs
                    || it.value() > maxDwrs)
                    continue;
            }
            rows.push_back(player);
        }
    }

    m_scoutedSection.box->setTitle(freeAgentsOnly
                                       ? tr("Free Agents (%1)").arg(rows.size())
                                       : tr("Gescoutete Spieler (%1)").arg(rows.size()));
    m_scoutedSection.model->setColumns(buildColumns(false));
    m_scoutedSection.model->setRows(std::move(rows));

    // Sort by the chosen role/talent column, otherwise by name.
    int sortColumn = 1; // Name (column 0 is the shortlist star)
    Qt::SortOrder order = Qt::AscendingOrder;
    const QString wantedHeader = filterKey == QLatin1String("__talent")
                                     ? tr("Talent")
                                     : (!filterKey.startsWith(QLatin1String("__"))
                                            ? filterKey
                                            : QString());
    if (!wantedHeader.isEmpty()) {
        for (int c = 0; c < m_scoutedSection.model->columnCount(); ++c) {
            if (m_scoutedSection.model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString()
                == wantedHeader) {
                sortColumn = c;
                order = Qt::DescendingOrder;
                break;
            }
        }
    }
    m_scoutedSection.table->sortByColumn(sortColumn, order);
    m_scoutedSection.table->resizeColumnsToContents();
}

void SquadMatrixPage::addAttributeRow(int attrIndex, int minimum)
{
    auto *rowWidget = new QWidget(m_attrBox);
    auto *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    AttributeRow row;
    row.widget = rowWidget;
    row.attribute = new QComboBox(rowWidget);
    QList<int> order(kAttrCount);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [](int a, int b) {
        return attrNames()[a].localeAwareCompare(attrNames()[b]) < 0;
    });
    for (int index : order)
        row.attribute->addItem(attrNames()[index], index);
    row.attribute->setCurrentIndex(std::max(0, row.attribute->findData(attrIndex)));
    row.minimum = new QSpinBox(rowWidget);
    row.minimum->setRange(1, 20);
    row.minimum->setValue(minimum);
    auto *removeButton = new QToolButton(rowWidget);
    removeButton->setText(QStringLiteral("✕"));
    removeButton->setToolTip(tr("Schwelle entfernen"));

    rowLayout->addWidget(row.attribute);
    rowLayout->addWidget(new QLabel(QStringLiteral("≥"), rowWidget));
    rowLayout->addWidget(row.minimum);
    rowLayout->addWidget(removeButton);
    rowLayout->addStretch(1);
    m_attrRowsLayout->addWidget(rowWidget);
    m_attrRows.append(row);

    connect(row.attribute, &QComboBox::currentIndexChanged, this,
            [this] { attributeThresholdsChanged(); });
    connect(row.minimum, &QSpinBox::valueChanged, this, [this] { attributeThresholdsChanged(); });
    connect(removeButton, &QToolButton::clicked, this, [this, rowWidget] {
        removeAttributeRow(rowWidget);
        attributeThresholdsChanged();
    });
}

void SquadMatrixPage::removeAttributeRow(QWidget *rowWidget)
{
    for (qsizetype i = 0; i < m_attrRows.size(); ++i) {
        if (m_attrRows.at(i).widget == rowWidget) {
            m_attrRows.removeAt(i);
            break;
        }
    }
    rowWidget->deleteLater();
}

QList<QPair<int, int>> SquadMatrixPage::activeThresholds() const
{
    QList<QPair<int, int>> thresholds;
    if (!m_attrBox->isChecked())
        return thresholds;
    for (const AttributeRow &row : m_attrRows)
        thresholds.append({row.attribute->currentData().toInt(), row.minimum->value()});
    return thresholds;
}

void SquadMatrixPage::attributeThresholdsChanged()
{
    if (m_updating)
        return;
    QStringList entries;
    for (const AttributeRow &row : m_attrRows) {
        entries << QStringLiteral("%1:%2")
                       .arg(attrNames()[row.attribute->currentData().toInt()])
                       .arg(row.minimum->value());
    }
    Database &db = m_context.database();
    db.setSetting(kAttrThresholdsKey, entries.join(QLatin1Char(',')));
    db.setSetting(kAttrEnabledKey, m_attrBox->isChecked() ? QStringLiteral("1")
                                                          : QStringLiteral("0"));
    db.setSetting(kAttrOnlyMatchesKey, m_attrOnlyMatchesCheck->isChecked() ? QStringLiteral("1")
                                                                           : QStringLiteral("0"));
    rebuildScoutedOnly();
}

void SquadMatrixPage::loadAttributeThresholds()
{
    // Called with m_updating set: restoring the controls must not save back.
    for (const AttributeRow &row : std::as_const(m_attrRows))
        row.widget->deleteLater();
    m_attrRows.clear();

    Database &db = m_context.database();
    const QStringList entries =
        db.setting(kAttrThresholdsKey).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const qsizetype colon = entry.lastIndexOf(QLatin1Char(':'));
        const int attrIndex = attrIndexByName(entry.left(colon));
        const int minimum = entry.mid(colon + 1).toInt();
        if (colon > 0 && attrIndex >= 0 && minimum >= 1 && minimum <= 20)
            addAttributeRow(attrIndex, minimum);
    }
    m_attrBox->setChecked(db.setting(kAttrEnabledKey) == QLatin1String("1"));
    m_attrOnlyMatchesCheck->setChecked(db.setting(kAttrOnlyMatchesKey) == QLatin1String("1"));
}

void SquadMatrixPage::toggleShortlist(const Player *player)
{
    const int row = m_context.store().rowByUid(player->uid);
    if (row < 0)
        return;
    Player &mutablePlayer = m_context.store().at(row);
    mutablePlayer.onShortlist = !mutablePlayer.onShortlist;

    QList<int> ids;
    for (const Player &p : m_context.store().players()) {
        if (p.onShortlist)
            ids << p.id;
    }
    if (!m_context.database().setShortlistIds(ids)) {
        mutablePlayer.onShortlist = !mutablePlayer.onShortlist;
        QMessageBox::critical(this, tr("Shortlist"), m_context.database().errorString());
        return;
    }
    rebuildScoutedOnly();
}

void SquadMatrixPage::exportCsv(const TableSection &section)
{
    const QString path = QFileDialog::getSaveFileName(this, tr("CSV exportieren"),
                                                      QStringLiteral("matrix.csv"),
                                                      tr("CSV-Dateien (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("CSV exportieren"), file.errorString());
        return;
    }
    file.write("\xEF\xBB\xBF", 3); // UTF-8 BOM for Excel
    // Separator follows the Windows regional settings like Excel does (';' on a
    // German system), so the file opens in columns with a double-click.
    file.write(section.model
                   ->toCsv(section.proxy->orderedPlayers(), csvSeparator(QLocale::system()))
                   .toUtf8());
}

} // namespace fm
