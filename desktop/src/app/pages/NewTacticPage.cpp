#include "NewTacticPage.h"

#include "../AppContext.h"
#include "../widgets/TacticPitchWidget.h"
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
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace fm {

namespace {

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

NewTacticPage::NewTacticPage(AppContext &context, ThemeManager &theme, QWidget *parent)
    : PageBase(context, parent)
    , m_theme(theme)
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
    // Side by side like the game's tactics screen: with ball left, without
    // ball right.
    buildPhaseEditor(m_phases[1], content);
    buildPhaseEditor(m_phases[0], content);
    auto *phaseRow = new QHBoxLayout;
    phaseRow->addWidget(m_phases[1].box, 1, Qt::AlignTop);
    phaseRow->addWidget(m_phases[0].box, 1, Qt::AlignTop);
    layout->addLayout(phaseRow);

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

    // The combos hold the choice; the pitch shows and edits it.
    for (const auto &[stratum, specs] : editorRows(pitchGeometry(m_context.fmVersionId()))) {
        Q_UNUSED(stratum);
        for (const PitchSlot &spec : specs) {
            auto *combo = new QComboBox(editor.box);
            combo->setVisible(false);
            editor.slotCombos.insert(spec.key, combo);
            connect(combo, &QComboBox::currentIndexChanged, this,
                    &NewTacticPage::updateFullName);
        }
    }
    auto *gkCombo = new QComboBox(editor.box);
    gkCombo->setVisible(false);
    editor.slotCombos.insert(QStringLiteral("GK"), gkCombo);
    connect(gkCombo, &QComboBox::currentIndexChanged, this, &NewTacticPage::updateFullName);

    editor.pitch = new TacticPitchWidget(m_theme, editor.box);
    editor.pitch->setFmVersionId(m_context.fmVersionId());
    editor.pitch->setToolTip(tr("Klicke auf ein „+“, um dort eine Rolle aufzustellen, oder auf "
                                "eine Rolle, um sie zu ändern."));
    pitchLayout->addWidget(editor.pitch, 0, Qt::AlignHCenter);
    connect(editor.pitch, &TacticPitchWidget::slotClicked, this,
            [this, &editor](const QString &slot, const QPoint &globalPos) {
                showSlotMenu(editor, slot, globalPos);
            });
}

void NewTacticPage::showSlotMenu(PhaseEditor &editor, const QString &slot,
                                 const QPoint &globalPos)
{
    QComboBox *combo = editor.slotCombos.value(slot);
    if (!combo)
        return;
    const PitchSlot *spec = pitchGeometry(m_context.fmVersionId()).slot(slot);
    const bool goalkeeper = slot == QLatin1String("GK");
    QMenu menu(this);
    auto *title = menu.addAction(spec && !spec->label.isEmpty() ? spec->label : slot);
    title->setEnabled(false);
    menu.addSeparator();
    const int current = combo->currentIndex();
    for (int i = goalkeeper ? 0 : 1; i < combo->count(); ++i) {
        QString role = combo->itemData(i).toString();
        if (role.endsWith(QLatin1String("-IP")))
            role.chop(3);
        else if (role.endsWith(QLatin1String("-OOP")))
            role.chop(4);
        auto *action = menu.addAction(QStringLiteral("%1  (%2)").arg(combo->itemText(i), role),
                                      this, [combo, i] { combo->setCurrentIndex(i); });
        action->setCheckable(true);
        action->setChecked(i == current);
    }
    if (!goalkeeper && current > 0) {
        menu.addSeparator();
        menu.addAction(tr("✕ Platz leeren"), this, [combo] { combo->setCurrentIndex(0); });
    }
    menu.exec(globalPos);
}

void NewTacticPage::syncPitch(PhaseEditor &editor)
{
    QHash<QString, QString> roles;
    int outfield = 0;
    for (auto it = editor.slotCombos.constBegin(); it != editor.slotCombos.constEnd(); ++it) {
        const QString role = it.value()->currentData().toString();
        if (role.isEmpty())
            continue;
        roles.insert(it.key(), role);
        if (it.key() != QLatin1String("GK"))
            ++outfield;
    }
    // Ten outfield players: no more "+" boxes, a click on a role swaps it.
    editor.pitch->setEditorSlots(roles, m_context.definitions().roleDisplayMap(), outfield < 10);
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
                    "zusammenpassen; der Analyzer verteilt die Spieler später selbst. Klicke "
                    "auf ein „+“, um dort eine Rolle aufzustellen, oder auf eine Rolle, um sie "
                    "zu ändern. Mit einer Vorlage aus dem Spiel bist du schneller.")
               : tr("Baue deine Formation auf dem Feld unten: Klicke auf ein „+“, um dort "
                    "eine Rolle aufzustellen, oder auf eine Rolle, um sie zu ändern — genau "
                    "ein Torhüter und zehn Feldspieler."));
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
        syncPitch(editor);
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
