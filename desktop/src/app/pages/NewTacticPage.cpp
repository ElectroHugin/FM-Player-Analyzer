#include "NewTacticPage.h"

#include "../AppContext.h"
#include "../widgets/TacticPitchWidget.h"
#include "core/Constants.h"
#include "core/TacticLibrary.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QStandardPaths>
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
#include <QRegularExpression>
#include <QDir>
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

    m_heading = new QLabel(content);
    layout->addWidget(m_heading);

    // --- The save's tactics: open, rename, duplicate, delete, exchange ---
    auto *libraryBox = new QGroupBox(tr("Deine Taktiken"), content);
    auto *libraryLayout = new QVBoxLayout(libraryBox);
    auto *libraryRow = new QHBoxLayout;
    m_tacticList = new QComboBox(libraryBox);
    m_tacticList->setMinimumWidth(280);
    m_tacticList->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    libraryRow->addWidget(m_tacticList);
    const auto addButton = [&](const QString &text, const QString &tip, auto slot,
                               bool needsSelection) {
        auto *button = new QPushButton(text, libraryBox);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, slot);
        libraryRow->addWidget(button);
        if (needsSelection)
            m_selectionButtons << button;
        return button;
    };
    addButton(tr("Bearbeiten"), tr("Öffnet die Taktik unten im Editor."),
              [this] { loadTactic(selectedTactic()); }, true);
    addButton(tr("Umbenennen…"), tr("Favoriten und gespeicherte Bridges ziehen mit."),
              &NewTacticPage::renameSelected, true);
    addButton(tr("Duplizieren…"), tr("Legt eine Kopie unter neuem Namen an."),
              &NewTacticPage::duplicateSelected, true);
    addButton(tr("Löschen…"),
              tr("Entfernt die Taktik aus diesem Spielstand. Auch mitgelieferte Beispiele "
                 "lassen sich löschen; Updates bringen sie nicht zurück."),
              &NewTacticPage::deleteSelected, true);
    libraryRow->addSpacing(12);
    addButton(tr("Exportieren…"),
              tr("Speichert die Taktik als Datei (.%1) für einen anderen Spielstand oder "
                 "andere Nutzer, samt der verwendeten Rollen.")
                  .arg(TacticLibrary::exchangeSuffix()),
              &NewTacticPage::exportSelected, true);
    addButton(tr("Importieren…"),
              tr("Liest eine Taktik-Datei (.%1) in diesen Spielstand ein.")
                  .arg(TacticLibrary::exchangeSuffix()),
              &NewTacticPage::importTactic, false);
    libraryRow->addStretch(1);
    libraryLayout->addLayout(libraryRow);
    auto *libraryHint = new QLabel(
        tr("Taktiken gehören zum geöffneten Spielstand. Änderungen, Löschungen und Importe "
           "bleiben bei Updates erhalten; ein Update ergänzt höchstens neue Beispieltaktiken."),
        libraryBox);
    libraryHint->setWordWrap(true);
    libraryHint->setObjectName(QStringLiteral("kpiCaption"));
    libraryLayout->addWidget(libraryHint);
    layout->addWidget(libraryBox);
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
    m_newButton = new QPushButton(tr("Bearbeitung abbrechen"), content);
    m_newButton->setToolTip(tr("Leert den Editor für eine neue Taktik; nichts wird gespeichert."));
    connect(m_newButton, &QPushButton::clicked, this, &NewTacticPage::resetForm);
    buttonRow->addWidget(m_newButton);
    m_createButton = new QPushButton(tr("Neue Taktik erstellen"), content);
    m_createButton->setDefault(true);
    buttonRow->addWidget(m_createButton);
    layout->addLayout(buttonRow);
    layout->addStretch(1);
    connect(m_createButton, &QPushButton::clicked, this, &NewTacticPage::createTactic);
    updateEditMode();
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
    // The edited tactic may be gone (other save opened, deleted elsewhere).
    if (!m_editingName.isEmpty() && !m_context.definitions().tacticNames().contains(m_editingName))
        m_editingName.clear();
    refreshTacticList();
    updateEditMode();
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
    const bool editing = !m_editingName.isEmpty();
    const QString title = editing ? tr("Taktik speichern") : tr("Neue Taktik");
    if (m_nameEdit->text().trimmed().isEmpty()
        || (!editing && m_shapeEdit->text().trimmed().isEmpty())) {
        QMessageBox::warning(this, title,
                             tr("Bitte gib sowohl einen Formations-Namen als auch eine "
                                "Formation (z. B. 4-2-3-1) an."));
        return;
    }
    if (name != m_editingName && m_context.definitions().tacticNames().contains(name)) {
        QMessageBox::warning(this, title,
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
    QJsonObject tacticRoles = oopRoles;
    QStringList ipSlots = oopRoles.keys();
    if (twoPhase()) {
        QJsonObject ipRoles;
        QHash<QString, QString> ipOutfield;
        if (!collectPhase(m_phases[1], ipRoles, ipOutfield))
            return;
        ipSlots = ipRoles.keys();
        tacticRoles = QJsonObject{{QStringLiteral("ip"), ipRoles},
                                  {QStringLiteral("oop"), oopRoles}};
    }

    QJsonObject root = m_context.definitions().root();
    const bool renamed = editing && name != m_editingName;
    if (renamed)
        TacticLibrary::removeTactic(root, m_editingName);
    TacticLibrary::putTactic(root, m_context.fmVersionId(), name, tacticRoles);
    if (!commitDefinitions(root, title))
        return;
    Database &db = m_context.database();
    if (renamed)
        TacticLibrary::renameTacticReferences(db, m_editingName, name);
    if (editing)
        TacticLibrary::pruneBridges(db, name, ipSlots, oopRoles.keys());

    QMessageBox::information(this, title,
                             editing ? tr("Taktik '%1' wurde gespeichert.").arg(name)
                                     : tr("Taktik '%1' wurde erstellt und ist jetzt überall "
                                          "verfügbar.")
                                           .arg(name));
    resetForm();
    refreshTacticList();
    const int index = m_tacticList->findText(name);
    if (index >= 0)
        m_tacticList->setCurrentIndex(index);
}

bool NewTacticPage::commitDefinitions(const QJsonObject &root, const QString &title)
{
    const QJsonObject before = m_context.definitions().root();
    m_context.definitions().setRoot(root);
    QString error;
    if (!m_context.saveDefinitions(&error)) {
        // Nothing half-saved stays in memory.
        m_context.definitions().setRoot(before);
        QMessageBox::critical(this, title,
                              tr("Die Taktiken konnten nicht gespeichert werden: %1").arg(error));
        return false;
    }
    return true;
}

void NewTacticPage::refreshTacticList()
{
    const QString previous = m_tacticList->currentText();
    QStringList names = m_context.definitions().tacticNames();
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    m_tacticList->blockSignals(true);
    m_tacticList->clear();
    m_tacticList->addItems(names);
    const int index = m_tacticList->findText(previous);
    m_tacticList->setCurrentIndex(index >= 0 ? index : 0);
    m_tacticList->blockSignals(false);
    for (QPushButton *button : std::as_const(m_selectionButtons))
        button->setEnabled(!names.isEmpty());
}

QString NewTacticPage::selectedTactic() const
{
    return m_tacticList->currentText();
}

void NewTacticPage::updateEditMode()
{
    const bool editing = !m_editingName.isEmpty();
    m_heading->setText(QStringLiteral("<h2>%1</h2>")
                           .arg(editing ? tr("Taktik bearbeiten: %1").arg(m_editingName.toHtmlEscaped())
                                        : tr("Neue Taktik erstellen")));
    m_createButton->setText(editing ? tr("Änderungen speichern") : tr("Neue Taktik erstellen"));
    m_newButton->setVisible(editing);
}

void NewTacticPage::loadTactic(const QString &name)
{
    const Definitions &definitions = m_context.definitions();
    if (!definitions.tacticNames().contains(name))
        return;
    const auto fill = [](PhaseEditor &editor, const QHash<QString, QString> &roles) {
        for (auto it = editor.slotCombos.constBegin(); it != editor.slotCombos.constEnd(); ++it)
            it.value()->setCurrentIndex(it.key() == QLatin1String("GK") ? -1 : 0);
        for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
            // Alias slots of older tactics: ST = STC, WBL/WBR = DML/DMR.
            QString slot = it.key();
            if (slot == QLatin1String("ST"))
                slot = QStringLiteral("STC");
            else if (slot == QLatin1String("WBL"))
                slot = QStringLiteral("DML");
            else if (slot == QLatin1String("WBR"))
                slot = QStringLiteral("DMR");
            QComboBox *combo = editor.slotCombos.value(slot);
            if (!combo)
                continue;
            int index = combo->findData(it.value());
            if (index < 0) {
                // A role this slot no longer offers: keep it selectable.
                combo->addItem(it.value(), it.value());
                index = combo->count() - 1;
            }
            combo->setCurrentIndex(index);
        }
    };
    for (PhaseEditor &editor : m_phases) {
        for (QComboBox *combo : std::as_const(editor.slotCombos))
            combo->blockSignals(true);
    }
    if (definitions.isTwoPhaseTactic(name)) {
        const auto &lineUps = definitions.twoPhaseTactics().value(name);
        fill(m_phases[0], lineUps.oop);
        fill(m_phases[1], lineUps.ip);
    } else {
        fill(m_phases[0], definitions.tacticRoles().value(name));
    }
    for (PhaseEditor &editor : m_phases) {
        for (QComboBox *combo : std::as_const(editor.slotCombos))
            combo->blockSignals(false);
    }
    m_editingName = name;
    m_nameEdit->blockSignals(true);
    m_shapeEdit->blockSignals(true);
    m_nameEdit->setText(name);
    m_shapeEdit->clear();
    m_nameEdit->blockSignals(false);
    m_shapeEdit->blockSignals(false);
    updateEditMode();
    updateFullName();
}

void NewTacticPage::renameSelected()
{
    const QString oldName = selectedTactic();
    if (oldName.isEmpty())
        return;
    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, tr("Taktik umbenennen"), tr("Neuer Name für '%1':").arg(oldName),
                              QLineEdit::Normal, oldName, &ok)
            .trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    if (m_context.definitions().tacticNames().contains(newName)) {
        QMessageBox::warning(this, tr("Taktik umbenennen"),
                             tr("Eine Taktik namens '%1' existiert bereits.").arg(newName));
        return;
    }
    QJsonObject root = m_context.definitions().root();
    if (!TacticLibrary::renameTactic(root, oldName, newName)
        || !commitDefinitions(root, tr("Taktik umbenennen")))
        return;
    TacticLibrary::renameTacticReferences(m_context.database(), oldName, newName);
    if (m_editingName == oldName) {
        m_editingName = newName;
        m_nameEdit->setText(newName);
        updateEditMode();
    }
    refreshTacticList();
    m_tacticList->setCurrentIndex(m_tacticList->findText(newName));
}

void NewTacticPage::duplicateSelected()
{
    const QString source = selectedTactic();
    if (source.isEmpty())
        return;
    const QStringList taken = m_context.definitions().tacticNames();
    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, tr("Taktik duplizieren"), tr("Name der Kopie:"),
                              QLineEdit::Normal,
                              TacticLibrary::uniqueName(source + tr(" (Kopie)"), taken), &ok)
            .trimmed();
    if (!ok || newName.isEmpty())
        return;
    if (taken.contains(newName)) {
        QMessageBox::warning(this, tr("Taktik duplizieren"),
                             tr("Eine Taktik namens '%1' existiert bereits.").arg(newName));
        return;
    }
    QJsonObject root = m_context.definitions().root();
    const QJsonObject roles =
        root.value(QLatin1String("tactic_roles")).toObject().value(source).toObject();
    const QJsonObject layout =
        root.value(QLatin1String("tactic_layouts")).toObject().value(source).toObject();
    TacticLibrary::putTactic(root, m_context.fmVersionId(), newName, roles, layout);
    if (!commitDefinitions(root, tr("Taktik duplizieren")))
        return;
    refreshTacticList();
    m_tacticList->setCurrentIndex(m_tacticList->findText(newName));
}

void NewTacticPage::deleteSelected()
{
    const QString name = selectedTactic();
    if (name.isEmpty())
        return;
    if (m_context.definitions().tacticNames().size() <= 1) {
        QMessageBox::information(this, tr("Taktik löschen"),
                                 tr("Die letzte Taktik eines Spielstands kann nicht gelöscht "
                                    "werden. Lege zuerst eine weitere an oder importiere eine."));
        return;
    }
    if (QMessageBox::question(
            this, tr("Taktik löschen"),
            tr("Taktik '%1' aus diesem Spielstand löschen?\n\nIst sie ein Favorit, wird der "
               "Favorit zurückgesetzt; gespeicherte Bridges dieser Taktik werden entfernt. "
               "Updates stellen sie nicht wieder her. Wer sie später braucht, exportiert sie "
               "vorher.")
                .arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    QJsonObject root = m_context.definitions().root();
    if (!TacticLibrary::removeTactic(root, name) || !commitDefinitions(root, tr("Taktik löschen")))
        return;
    TacticLibrary::removeTacticReferences(m_context.database(), name);
    if (m_editingName == name)
        resetForm();
    refreshTacticList();
}

void NewTacticPage::exportSelected()
{
    const QString name = selectedTactic();
    if (name.isEmpty())
        return;
    QString fileName = name;
    static const QRegularExpression unsafe(QStringLiteral("[\\\\/:*?\"<>|]+"));
    fileName.replace(unsafe, QStringLiteral("_"));
    const QString suffix = TacticLibrary::exchangeSuffix();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Taktik exportieren"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(fileName + QLatin1Char('.') + suffix),
        tr("Analyzer-Taktik (*.%1)").arg(suffix));
    if (path.isEmpty())
        return;
    QFile file(path);
    const QByteArray data = TacticLibrary::exportTactic(m_context.definitions(), name);
    if (data.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        QMessageBox::critical(this, tr("Taktik exportieren"),
                              tr("Die Datei %1 konnte nicht geschrieben werden.")
                                  .arg(QDir::toNativeSeparators(path)));
        return;
    }
    QMessageBox::information(this, tr("Taktik exportieren"),
                             tr("Taktik '%1' wurde nach %2 exportiert.")
                                 .arg(name, QDir::toNativeSeparators(path)));
}

void NewTacticPage::importTactic()
{
    const QString title = tr("Taktik importieren");
    const QString suffix = TacticLibrary::exchangeSuffix();
    const QString path = QFileDialog::getOpenFileName(
        this, title, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Analyzer-Taktik (*.%1);;Alle Dateien (*)").arg(suffix));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, title,
                              tr("Die Datei %1 konnte nicht gelesen werden.")
                                  .arg(QDir::toNativeSeparators(path)));
        return;
    }
    const Definitions &definitions = m_context.definitions();
    const TacticLibrary::ImportCheck check = TacticLibrary::checkImport(file.readAll(), definitions);
    switch (check.status) {
    case TacticLibrary::ImportCheck::InvalidFile:
        QMessageBox::warning(this, title,
                             tr("Die Datei ist keine gültige Taktik-Datei des Analyzers oder "
                                "beschädigt.\n\nDetails: %1")
                                 .arg(check.error));
        return;
    case TacticLibrary::ImportCheck::TooNew:
        QMessageBox::warning(this, title,
                             tr("Die Datei stammt aus einer neueren Version des Analyzers. "
                                "Bitte aktualisiere den Analyzer, um sie zu importieren."));
        return;
    case TacticLibrary::ImportCheck::WrongFmVersion:
        QMessageBox::warning(
            this, title,
            tr("Die Taktik '%1' gehört zu %2, dieser Spielstand nutzt %3. Rollen und Plätze "
               "der FM-Versionen passen nicht zueinander; importiere sie in einen Spielstand "
               "derselben Version.")
                .arg(check.name, fmDataVersion(check.fileFmVersion).displayName,
                     fmDataVersion(definitions.fmVersionId()).displayName));
        return;
    case TacticLibrary::ImportCheck::MissingRoles:
        QMessageBox::warning(this, title,
                             tr("Die Taktik '%1' nutzt Rollen, die dieser Spielstand nicht kennt "
                                "und die in der Datei nicht beschrieben sind: %2")
                                 .arg(check.name, check.unknownRoles.join(QStringLiteral(", "))));
        return;
    case TacticLibrary::ImportCheck::Ok:
        break;
    }

    QStringList notes;
    if (!check.rolesToAdd.isEmpty()) {
        QStringList names;
        for (const TacticLibrary::ImportedRole &role : check.rolesToAdd)
            names << QStringLiteral("%1 (%2)").arg(role.name, role.id);
        notes << tr("Neue eigene Rollen werden mit angelegt: %1").arg(names.join(QStringLiteral(", ")));
    }
    if (!check.differingRoles.isEmpty())
        notes << tr("Diese Rollen gibt es hier schon, mit anderen Gewichten oder Positionen; "
                    "der Spielstand behält seine eigene Fassung: %1")
                     .arg(check.differingRoles.join(QStringLiteral(", ")));

    QString finalName = check.name;
    if (check.nameTaken) {
        QMessageBox box(QMessageBox::Question, title,
                        tr("Es gibt schon eine Taktik namens '%1'.").arg(check.name),
                        QMessageBox::NoButton, this);
        auto *rename = box.addButton(tr("Unter neuem Namen importieren"), QMessageBox::AcceptRole);
        auto *replace = box.addButton(tr("Vorhandene ersetzen"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(rename);
        box.exec();
        if (box.clickedButton() == rename) {
            bool ok = false;
            finalName = QInputDialog::getText(this, title, tr("Name der importierten Taktik:"),
                                              QLineEdit::Normal,
                                              TacticLibrary::uniqueName(check.name,
                                                                        definitions.tacticNames()),
                                              &ok)
                            .trimmed();
            if (!ok || finalName.isEmpty())
                return;
            if (definitions.tacticNames().contains(finalName)) {
                QMessageBox::warning(this, title,
                                     tr("Eine Taktik namens '%1' existiert bereits.").arg(finalName));
                return;
            }
        } else if (box.clickedButton() != replace) {
            return;
        }
    } else if (!notes.isEmpty()) {
        if (QMessageBox::question(this, title,
                                  tr("Taktik '%1' importieren?").arg(check.name) + QStringLiteral("\n\n")
                                      + notes.join(QStringLiteral("\n\n")))
            != QMessageBox::Yes)
            return;
        notes.clear();
    }
    QJsonObject root = definitions.root();
    const bool replaced = definitions.tacticNames().contains(finalName);
    TacticLibrary::applyImport(root, m_context.fmVersionId(), check, finalName);
    if (!commitDefinitions(root, title))
        return;
    if (replaced) {
        QStringList ipSlots, oopSlots;
        if (check.roles.contains(QLatin1String("ip"))) {
            ipSlots = check.roles.value(QLatin1String("ip")).toObject().keys();
            oopSlots = check.roles.value(QLatin1String("oop")).toObject().keys();
        } else {
            ipSlots = oopSlots = check.roles.keys();
        }
        TacticLibrary::pruneBridges(m_context.database(), finalName, ipSlots, oopSlots);
    }
    refreshTacticList();
    m_tacticList->setCurrentIndex(m_tacticList->findText(finalName));
    QMessageBox::information(this, title,
                             tr("Taktik '%1' wurde importiert.").arg(finalName)
                                 + (notes.isEmpty() ? QString()
                                                    : QStringLiteral("\n\n")
                                                          + notes.join(QStringLiteral("\n\n"))));
}

void NewTacticPage::resetForm()
{
    m_editingName.clear();
    updateEditMode();
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
