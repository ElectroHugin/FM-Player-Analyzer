#include "NewTacticPage.h"

#include "../AppContext.h"
#include "core/Constants.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace fm {

namespace {

// Row caption of the editor (top = strikers, as in legacy new_tactic.py).
QString stratumLabel(const QString &stratum)
{
    if (stratum == QLatin1String("Strikers"))
        return QObject::tr("Sturm");
    if (stratum == QLatin1String("Attacking Midfield"))
        return QObject::tr("Offensives Mittelfeld");
    if (stratum == QLatin1String("Midfield"))
        return QObject::tr("Mittelfeld");
    if (stratum == QLatin1String("Defensive Midfield"))
        return QObject::tr("Defensives Mittelfeld");
    if (stratum == QLatin1String("Defense"))
        return QObject::tr("Abwehr");
    return stratum;
}

// Editor rows from the pitch geometry, top (strikers) first, without alias
// slots (WBL/WBR/ST).
QList<QPair<QString, QList<PitchSlot>>> editorRows(const PitchGeometry &geometry)
{
    QList<QPair<QString, QList<PitchSlot>>> rows;
    for (auto it = geometry.rows.crbegin(); it != geometry.rows.crend(); ++it) {
        QList<PitchSlot> editorSlots;
        for (const PitchSlot &slot : it->pitchSlots) {
            if (slot.inEditor)
                editorSlots << slot;
        }
        rows.append({it->stratum, editorSlots});
    }
    return rows;
}

} // namespace

NewTacticPage::NewTacticPage(AppContext &context, QWidget *parent)
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

    auto *heading =
        new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Neue Taktik erstellen")), content);
    layout->addWidget(heading);
    m_info = new QLabel(content);
    m_info->setWordWrap(true);
    m_info->setObjectName(QStringLiteral("kpiCaption"));
    auto *info = m_info;
    layout->addWidget(info);

    // --- 1. Naming ---
    auto *nameBox = new QGroupBox(tr("1. Taktik-Name"), content);
    auto *nameRow = new QHBoxLayout(nameBox);
    nameRow->addWidget(new QLabel(tr("Formations-Name:"), nameBox));
    m_nameEdit = new QLineEdit(nameBox);
    m_nameEdit->setPlaceholderText(tr("z. B. 'Mein Gegenpressing'"));
    m_nameEdit->setMinimumWidth(220);
    nameRow->addWidget(m_nameEdit, 2);
    nameRow->addWidget(new QLabel(tr("Formation:"), nameBox));
    m_shapeEdit = new QLineEdit(nameBox);
    m_shapeEdit->setPlaceholderText(QStringLiteral("4-2-3-1"));
    m_shapeEdit->setMaximumWidth(120);
    nameRow->addWidget(m_shapeEdit, 1);
    layout->addWidget(nameBox);
    m_fullNameLabel = new QLabel(content);
    layout->addWidget(m_fullNameLabel);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &NewTacticPage::updateFullName);
    connect(m_shapeEdit, &QLineEdit::textChanged, this, &NewTacticPage::updateFullName);

    // --- 2. Formation designer (two line-ups when roles have phases) ---
    m_phases[0].phase = QStringLiteral("oop");
    m_phases[1].phase = QStringLiteral("ip");
    buildPhaseEditor(m_phases[1], content);
    buildPhaseEditor(m_phases[0], content);
    layout->addWidget(m_phases[1].box);
    layout->addWidget(m_phases[0].box);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    m_createButton = new QPushButton(tr("Neue Taktik erstellen"), content);
    m_createButton->setDefault(true);
    buttonRow->addWidget(m_createButton);
    layout->addLayout(buttonRow);
    layout->addStretch(1);
    connect(m_createButton, &QPushButton::clicked, this, &NewTacticPage::createTactic);
}

void NewTacticPage::buildPhaseEditor(PhaseEditor &editor, QWidget *parent)
{
    editor.box = new QGroupBox(parent);
    auto *pitchLayout = new QVBoxLayout(editor.box);

    editor.templateRow = new QWidget(editor.box);
    auto *templateLayout = new QHBoxLayout(editor.templateRow);
    templateLayout->setContentsMargins(0, 0, 0, 0);
    templateLayout->addWidget(new QLabel(tr("Vorlage aus dem Spiel:"), editor.templateRow));
    editor.templateCombo = new QComboBox(editor.templateRow);
    editor.templateCombo->setMinimumWidth(220);
    editor.templateCombo->setToolTip(
        tr("Setzt die Plätze dieser Formation mit der ersten passenden Rolle. Danach kannst du "
           "jede Rolle anpassen."));
    templateLayout->addWidget(editor.templateCombo);
    auto *applyButton = new QPushButton(tr("Übernehmen"), editor.templateRow);
    templateLayout->addWidget(applyButton);
    templateLayout->addStretch(1);
    pitchLayout->addWidget(editor.templateRow);
    connect(applyButton, &QPushButton::clicked, this, [this, &editor] { applyTemplate(editor); });

    editor.countLabel = new QLabel(editor.box);
    pitchLayout->addWidget(editor.countLabel);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    int gridRow = 0;
    for (const auto &[stratum, specs] : editorRows(pitchGeometry(m_context.fmVersionId()))) {
        auto *stratumTitle = new QLabel(stratumLabel(stratum), editor.box);
        stratumTitle->setAlignment(Qt::AlignHCenter);
        stratumTitle->setObjectName(QStringLiteral("kpiCaption"));
        grid->addWidget(stratumTitle, gridRow++, 0, 1, 5);
        for (const PitchSlot &spec : specs) {
            auto *cell = new QVBoxLayout;
            auto *slotLabel = new QLabel(spec.label, editor.box);
            slotLabel->setAlignment(Qt::AlignHCenter);
            auto *combo = new QComboBox(editor.box);
            combo->setMinimumWidth(150);
            editor.slotCombos.insert(spec.key, combo);
            cell->addWidget(slotLabel);
            cell->addWidget(combo);
            auto *cellWidget = new QWidget(editor.box);
            cellWidget->setLayout(cell);
            grid->addWidget(cellWidget, gridRow, spec.displayColumn);
            connect(combo, &QComboBox::currentIndexChanged, this,
                    &NewTacticPage::updateFullName);
        }
        ++gridRow;
    }
    // Goalkeeper row spanning the width.
    auto *gkTitle = new QLabel(tr("Torhüter"), editor.box);
    gkTitle->setAlignment(Qt::AlignHCenter);
    gkTitle->setObjectName(QStringLiteral("kpiCaption"));
    grid->addWidget(gkTitle, gridRow++, 0, 1, 5);
    auto *gkCombo = new QComboBox(editor.box);
    gkCombo->setMinimumWidth(220);
    editor.slotCombos.insert(QStringLiteral("GK"), gkCombo);
    grid->addWidget(gkCombo, gridRow, 1, 1, 3);
    pitchLayout->addLayout(grid);
}

bool NewTacticPage::twoPhase() const
{
    return m_context.definitions().hasRolePhases();
}

void NewTacticPage::fillSlotCombo(QComboBox *combo, const QStringList &gamePositions,
                                  const QString &phase)
{
    const QString previous = combo->currentData().toString();
    const auto &definitions = m_context.definitions();
    const auto posMap = definitions.positionToRoleMapping();
    const auto roleNames = definitions.roleDisplayMap();
    const bool phased = twoPhase();

    QStringList roles;
    for (const QString &position : gamePositions)
        roles << posMap.value(position);
    roles.removeDuplicates();
    if (phased) {
        roles.erase(std::remove_if(roles.begin(), roles.end(),
                                   [&](const QString &role) {
                                       const QString rolePhase = definitions.rolePhase(role);
                                       return rolePhase != phase && rolePhase != QLatin1String("both");
                                   }),
                    roles.end());
    }
    std::sort(roles.begin(), roles.end(), [&roleNames](const QString &a, const QString &b) {
        return roleNames.value(a, a) < roleNames.value(b, b);
    });

    combo->blockSignals(true);
    combo->clear();
    combo->addItem(tr("— Unbesetzt —"), QString());
    for (const QString &role : roles)
        combo->addItem(roleNames.value(role, role), role);
    if (!previous.isEmpty()) {
        const int index = combo->findData(previous);
        if (index >= 0)
            combo->setCurrentIndex(index);
    }
    combo->blockSignals(false);
}

void NewTacticPage::refreshPhase(PhaseEditor &editor)
{
    const auto &definitions = m_context.definitions();
    for (const auto &[stratum, specs] : editorRows(pitchGeometry(m_context.fmVersionId()))) {
        Q_UNUSED(stratum);
        for (const PitchSlot &spec : specs) {
            if (QComboBox *combo = editor.slotCombos.value(spec.key))
                fillSlotCombo(combo, spec.rolePositions, editor.phase);
        }
    }
    // Goalkeeper: every GK/SK role (legacy), per phase when roles have phases.
    QComboBox *gkCombo = editor.slotCombos.value(QStringLiteral("GK"));
    const QString previous = gkCombo->currentData().toString();
    const auto roleNames = definitions.roleDisplayMap();
    QStringList gkRoles = definitions.gkRoles();
    if (twoPhase()) {
        gkRoles.erase(std::remove_if(gkRoles.begin(), gkRoles.end(),
                                     [&](const QString &role) {
                                         const QString rolePhase = definitions.rolePhase(role);
                                         return rolePhase != editor.phase
                                                && rolePhase != QLatin1String("both");
                                     }),
                      gkRoles.end());
    }
    std::sort(gkRoles.begin(), gkRoles.end(), [&roleNames](const QString &a, const QString &b) {
        return roleNames.value(a, a) < roleNames.value(b, b);
    });
    gkCombo->blockSignals(true);
    gkCombo->clear();
    for (const QString &role : gkRoles)
        gkCombo->addItem(roleNames.value(role, role), role);
    if (!previous.isEmpty()) {
        const int index = gkCombo->findData(previous);
        if (index >= 0)
            gkCombo->setCurrentIndex(index);
    }
    gkCombo->blockSignals(false);

    // Formation templates of this phase (FM27 "formation_templates").
    const QJsonObject templates = definitions.root()
                                      .value(QLatin1String("formation_templates"))
                                      .toObject()
                                      .value(editor.phase)
                                      .toObject();
    const QString previousTemplate = editor.templateCombo->currentText();
    editor.templateCombo->clear();
    for (auto it = templates.constBegin(); it != templates.constEnd(); ++it)
        editor.templateCombo->addItem(it.key());
    const int templateIndex = editor.templateCombo->findText(previousTemplate);
    if (templateIndex >= 0)
        editor.templateCombo->setCurrentIndex(templateIndex);
    editor.templateRow->setVisible(twoPhase() && !templates.isEmpty());
}

void NewTacticPage::applyTemplate(PhaseEditor &editor)
{
    const QJsonArray slotKeys = m_context.definitions()
                                    .root()
                                    .value(QLatin1String("formation_templates"))
                                    .toObject()
                                    .value(editor.phase)
                                    .toObject()
                                    .value(editor.templateCombo->currentText())
                                    .toArray();
    if (slotKeys.isEmpty())
        return;
    QSet<QString> wanted;
    for (const QJsonValue &value : slotKeys)
        wanted.insert(value.toString());
    for (auto it = editor.slotCombos.constBegin(); it != editor.slotCombos.constEnd(); ++it) {
        QComboBox *combo = it.value();
        if (it.key() == QLatin1String("GK"))
            continue;
        if (!wanted.contains(it.key())) {
            combo->setCurrentIndex(0);
            continue;
        }
        // Keep a role the user already picked, otherwise take the first one.
        if (combo->currentData().toString().isEmpty() && combo->count() > 1)
            combo->setCurrentIndex(1);
    }
    if (m_shapeEdit->text().trimmed().isEmpty() && editor.phase == QLatin1String("oop"))
        m_shapeEdit->setText(editor.templateCombo->currentText().section(QLatin1Char(' '), 0, 0));
    updateFullName();
}

void NewTacticPage::refresh()
{
    const bool phased = twoPhase();
    m_info->setText(
        phased ? tr("Eine Taktik hat zwei Aufstellungen: eine mit Ball und eine ohne Ball, "
                    "jede mit einem Torhüter und zehn Feldspielern. Die Plätze müssen nicht "
                    "zusammenpassen; der Analyzer verteilt die Spieler später selbst. Mit "
                    "einer Vorlage aus dem Spiel bist du schneller.")
               : tr("Baue deine Formation auf dem Feld unten. Wähle für jede aktive Position "
                    "eine Rolle — genau ein Torhüter und zehn Feldspieler."));
    m_phases[0].box->setTitle(phased ? tr("2b. Aufstellung ohne Ball")
                                     : tr("2. Formation gestalten"));
    m_phases[1].box->setTitle(tr("2a. Aufstellung mit Ball"));
    m_phases[1].box->setVisible(phased);
    refreshPhase(m_phases[0]);
    if (phased)
        refreshPhase(m_phases[1]);
    updateFullName();
}

QString NewTacticPage::fullName() const
{
    const QString shape = m_shapeEdit->text().trimmed();
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        return QString();
    return QStringLiteral("%1 %2").arg(shape, name).trimmed();
}

void NewTacticPage::updateFullName()
{
    for (PhaseEditor &editor : m_phases) {
        int outfield = 0;
        for (auto it = editor.slotCombos.constBegin(); it != editor.slotCombos.constEnd(); ++it) {
            if (it.key() != QLatin1String("GK") && !it.value()->currentData().toString().isEmpty())
                ++outfield;
        }
        editor.countLabel->setText(outfield == 10
                                       ? tr("✅ 10 Feldspieler ausgewählt.")
                                       : tr("Feldspieler ausgewählt: %1 / 10").arg(outfield));
    }
    const QString name = fullName();
    m_fullNameLabel->setText(
        tr("<b>Vollständiger Taktik-Name:</b> <code>%1</code>")
            .arg(name.isEmpty() ? QStringLiteral("…") : name));
}

bool NewTacticPage::collectPhase(const PhaseEditor &editor, QJsonObject &roles,
                                 QHash<QString, QString> &outfield)
{
    const bool phased = twoPhase();
    const QString lineUp = editor.phase == QLatin1String("ip") ? tr("mit Ball") : tr("ohne Ball");
    for (auto it = editor.slotCombos.constBegin(); it != editor.slotCombos.constEnd(); ++it) {
        if (it.key() == QLatin1String("GK"))
            continue;
        const QString role = it.value()->currentData().toString();
        if (!role.isEmpty())
            outfield.insert(it.key(), role);
    }
    if (outfield.size() != 10) {
        QMessageBox::warning(
            this, tr("Neue Taktik"),
            phased ? tr("Die Aufstellung %1 braucht genau 10 Feldspieler (aktuell: %2).")
                         .arg(lineUp)
                         .arg(outfield.size())
                   : tr("Du musst genau 10 Feldspieler auswählen (aktuell: %1).")
                         .arg(outfield.size()));
        return false;
    }
    const QString gkRole = editor.slotCombos.value(QStringLiteral("GK"))->currentData().toString();
    if (gkRole.isEmpty()) {
        QMessageBox::warning(this, tr("Neue Taktik"),
                             phased ? tr("Bitte wähle eine Torhüter-Rolle %1.").arg(lineUp)
                                    : tr("Bitte wähle eine Torhüter-Rolle."));
        return false;
    }
    // slot -> role, GK first like legacy.
    roles.insert(QStringLiteral("GK"), gkRole);
    for (auto it = outfield.constBegin(); it != outfield.constEnd(); ++it)
        roles.insert(it.key(), it.value());
    return true;
}

void NewTacticPage::createTactic()
{
    const QString name = fullName();
    if (m_nameEdit->text().trimmed().isEmpty() || m_shapeEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Neue Taktik"),
                             tr("Bitte gib sowohl einen Formations-Namen als auch eine "
                                "Formation (z. B. 4-2-3-1) an."));
        return;
    }
    if (m_context.definitions().tacticNames().contains(name)) {
        QMessageBox::warning(this, tr("Neue Taktik"),
                             tr("Eine Taktik namens '%1' existiert bereits. Bitte wähle "
                                "einen anderen Namen.")
                                 .arg(name));
        return;
    }

    // The base shape (and the pitch layout) is the line-up without ball.
    QJsonObject oopRoles;
    QHash<QString, QString> outfield;
    if (!collectPhase(m_phases[0], oopRoles, outfield))
        return;
    QJsonValue tacticRoles = oopRoles;
    if (twoPhase()) {
        QJsonObject ipRoles;
        QHash<QString, QString> ipOutfield;
        if (!collectPhase(m_phases[1], ipRoles, ipOutfield))
            return;
        tacticRoles = QJsonObject{{QStringLiteral("ip"), ipRoles},
                                  {QStringLiteral("oop"), oopRoles}};
    }

    // --- tactic_layouts: stratum -> [slots] from the master position map. ---
    QJsonObject tacticLayout;
    const auto &slotPositions = masterPositionMap(m_context.fmVersionId());
    for (auto it = outfield.constBegin(); it != outfield.constEnd(); ++it) {
        const auto posIt = slotPositions.constFind(it.key());
        if (posIt == slotPositions.constEnd())
            continue;
        const QString stratum = posIt.value().stratum;
        QJsonArray stratumSlots = tacticLayout.value(stratum).toArray();
        stratumSlots.append(it.key());
        tacticLayout.insert(stratum, stratumSlots);
    }

    QJsonObject root = m_context.definitions().root();
    QJsonObject allTactics = root.value(QLatin1String("tactic_roles")).toObject();
    allTactics.insert(name, tacticRoles);
    root.insert(QStringLiteral("tactic_roles"), allTactics);
    QJsonObject allLayouts = root.value(QLatin1String("tactic_layouts")).toObject();
    allLayouts.insert(name, tacticLayout);
    root.insert(QStringLiteral("tactic_layouts"), allLayouts);

    m_context.definitions().setRoot(root);
    if (!m_context.definitions().save()) {
        QMessageBox::critical(this, tr("Neue Taktik"),
                              m_context.definitions().errorString());
        return;
    }
    if (!m_context.reloadConfigAndDefinitions()) {
        QMessageBox::warning(this, tr("Neue Taktik"),
                             tr("Die Taktik wurde gespeichert, aber die Definitionen konnten "
                                "nicht neu geladen werden: %1")
                                 .arg(m_context.definitions().errorString()));
        return;
    }

    QMessageBox::information(this, tr("Neue Taktik"),
                             tr("Taktik '%1' wurde erstellt und ist jetzt überall "
                                "verfügbar.")
                                 .arg(name));
    resetForm();
}

void NewTacticPage::resetForm()
{
    m_nameEdit->clear();
    m_shapeEdit->clear();
    for (PhaseEditor &editor : m_phases) {
        for (auto it = editor.slotCombos.constBegin(); it != editor.slotCombos.constEnd(); ++it) {
            if (it.key() != QLatin1String("GK"))
                it.value()->setCurrentIndex(0);
        }
    }
    updateFullName();
}

} // namespace fm
