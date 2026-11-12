#include "RatingProfileEditor.h"
#include "RatingCurveEditor.h"
#include "FlowLayout.h"
#include "core/Attributes.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QSpinBox>
#include <QToolButton>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QUuid>
#include <QVBoxLayout>

namespace fm {
RatingProfileEditor::RatingProfileEditor(const QString &path, const QString &game, QWidget *parent)
    : QWidget(parent), m_path(path), m_game(game)
{
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
    auto *top = new QHBoxLayout;
    m_profiles = new QComboBox; m_profiles->setObjectName(QStringLiteral("ratingProfiles")); m_profiles->setMinimumWidth(200); m_profiles->setAccessibleName(tr("Bewertungsprofil"));
    top->addWidget(m_profiles, 1);
    const auto button = [this, top](const QString &name, auto callback) {
        auto *b = new QPushButton(name); top->addWidget(b); connect(b, &QPushButton::clicked, this, callback);
    };
    button(tr("Neu"), [this] { newProfile(); });
    button(tr("Importieren"), [this] { importProfile(); });
    button(tr("Exportieren"), [this] { exportProfile(); });
    button(tr("Löschen"), [this] { removeProfile(); });
    layout->addLayout(top);
    m_name = new QLineEdit; m_name->setObjectName(QStringLiteral("ratingProfileName")); m_name->setPlaceholderText(tr("Profilname")); m_name->setAccessibleName(tr("Profilname"));
    layout->addWidget(m_name);
    m_useForSave = new QPushButton(tr("Für diesen Spielstand verwenden"));
    m_useForSave->setObjectName(QStringLiteral("useRatingProfile"));
    connect(m_useForSave, &QPushButton::clicked, this, [this] {
        if (m_dirty) {
            const auto answer = QMessageBox::question(this, tr("Für diesen Spielstand verwenden"),
                tr("Das Profil hat ungespeicherte Änderungen. Erst speichern und dann verwenden?"),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save);
            if (answer != QMessageBox::Save) return;
            save(false);
            if (m_dirty) return;
        }
        emit activateRequested(m_selectedId);
    });
    layout->addWidget(m_useForSave, 0, Qt::AlignLeft);

    // Profile-wide factors. They belong to the profile and change its formula.
    auto *factors = new QFormLayout; factors->setContentsMargins(0, 0, 0, 0);
    m_keyFactor = new QDoubleSpinBox; m_keyFactor->setRange(0, 5); m_keyFactor->setDecimals(2); m_keyFactor->setSingleStep(0.1); m_keyFactor->setMaximumWidth(120);
    m_keyFactor->setAccessibleName(tr("Faktor Schlüssel-Attribute"));
    m_preferableFactor = new QDoubleSpinBox; m_preferableFactor->setRange(0, 5); m_preferableFactor->setDecimals(2); m_preferableFactor->setSingleStep(0.1); m_preferableFactor->setMaximumWidth(120);
    m_preferableFactor->setAccessibleName(tr("Faktor bevorzugte Attribute"));
    factors->addRow(tr("Faktor Schlüssel-Attribute"), m_keyFactor);
    factors->addRow(tr("Faktor bevorzugte Attribute"), m_preferableFactor);
    auto *shareRow = new QHBoxLayout; shareRow->setContentsMargins(0, 0, 0, 0);
    m_roleShareOn = new QCheckBox(tr("Verwenden"));
    m_roleShareOn->setObjectName(QStringLiteral("roleShareEnabled"));
    m_roleShare = new QSpinBox; m_roleShare->setRange(1, int(RatingProfile::kMaxRoleShare * 100)); m_roleShare->setSuffix(QStringLiteral(" %"));
    m_roleShare->setObjectName(QStringLiteral("roleShare")); m_roleShare->setValue(33);
    m_roleShare->setAccessibleName(tr("Rollenanteil"));
    shareRow->addWidget(m_roleShareOn); shareRow->addWidget(m_roleShare); shareRow->addStretch(1);
    factors->addRow(tr("Rollenanteil"), shareRow);
    auto *goalkeeperRow = new QHBoxLayout; goalkeeperRow->setContentsMargins(0, 0, 0, 0);
    m_goalkeeperShareOn = new QCheckBox(tr("Verwenden"));
    m_goalkeeperShareOn->setObjectName(QStringLiteral("goalkeeperRoleShareEnabled"));
    m_goalkeeperShare = new QSpinBox; m_goalkeeperShare->setRange(1, int(RatingProfile::kMaxRoleShare * 100)); m_goalkeeperShare->setSuffix(QStringLiteral(" %"));
    m_goalkeeperShare->setObjectName(QStringLiteral("goalkeeperRoleShare")); m_goalkeeperShare->setValue(20);
    m_goalkeeperShare->setAccessibleName(tr("Rollenanteil Torhüter"));
    goalkeeperRow->addWidget(m_goalkeeperShareOn); goalkeeperRow->addWidget(m_goalkeeperShare); goalkeeperRow->addStretch(1);
    factors->addRow(tr("Rollenanteil Torhüter"), goalkeeperRow);
    layout->addLayout(factors);
    auto *explainToggle = new QToolButton; explainToggle->setText(tr("Was ist der Rollenanteil?"));
    explainToggle->setCheckable(true); explainToggle->setAutoRaise(true); explainToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    explainToggle->setArrowType(Qt::RightArrow);
    auto *explain = new QLabel(tr(
        "Die Grundkurven eines Profils gelten für alle Rollen gleich. Die Faktoren für Schlüssel- "
        "und bevorzugte Attribute verstärken nur Attribute, die schon ein Gewicht haben. "
        "Beim FM27-Profil haben zum Beispiel Abschluss, Passen oder Tackling kaum Grundgewicht, "
        "deshalb unterscheiden sich die Rollen eines Spielers sonst kaum.\n\n"
        "Der Rollenanteil gibt jedem Schlüssel- und bevorzugten Attribut der Rolle zusätzlich ein "
        "eigenes, gleichmäßig steigendes Gewicht (Schlüssel 1,5-fach). Der eingestellte Prozentsatz "
        "ist der Anteil dieses Rollenteils an der fertigen Bewertung; 0 bis 100 bleibt die Skala. "
        "Bei Torhütern stehen in allen Rollen fast dieselben Attribute. Ihr Rollenanteil zählt deshalb "
        "nur die Attribute, in denen sich eine Rolle von den anderen Torhüterrollen derselben Phase "
        "unterscheidet (Exzentrik nie). Der Prozentsatz gilt für die Rolle mit den meisten Unterschieden, "
        "die anderen bekommen anteilig weniger.\n\n"
        "Am Testspielstand sagt das FM27-Profil mit 33 % die Durchschnittsnote besser voraus als "
        "ohne (Zusammenhang 0,207 statt 0,188) und trennt die Rollen deutlicher. "
        "Ohne Haken rechnet das Profil nur mit den Grundkurven."));
    explain->setWordWrap(true); explain->setVisible(false); explain->setObjectName(QStringLiteral("kpiCaption"));
    connect(explainToggle, &QToolButton::toggled, this, [explain, explainToggle](bool on) {
        explain->setVisible(on); explainToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    layout->addWidget(explainToggle, 0, Qt::AlignLeft);
    layout->addWidget(explain);
    const auto factorsEdited = [this] {
        if (m_loading || m_selectedId.isEmpty()) return;
        m_draft.keyFactor = m_keyFactor->value();
        m_draft.preferableFactor = m_preferableFactor->value();
        m_draft.roleShare = m_roleShareOn->isChecked() ? m_roleShare->value() / 100.0 : 0.0;
        m_roleShare->setEnabled(m_roleShareOn->isChecked());
        m_draft.goalkeeperRoleShare = m_goalkeeperShareOn->isChecked() ? m_goalkeeperShare->value() / 100.0 : 0.0;
        m_goalkeeperShare->setEnabled(m_goalkeeperShareOn->isChecked());
        m_dirty = true;
        m_status->setText(tr("Ungespeicherter Entwurf. Bestehende Bewertungen bleiben unverändert."));
    };
    connect(m_keyFactor, &QDoubleSpinBox::valueChanged, this, factorsEdited);
    connect(m_preferableFactor, &QDoubleSpinBox::valueChanged, this, factorsEdited);
    connect(m_roleShareOn, &QCheckBox::toggled, this, factorsEdited);
    connect(m_roleShare, &QSpinBox::valueChanged, this, factorsEdited);
    connect(m_goalkeeperShareOn, &QCheckBox::toggled, this, factorsEdited);
    connect(m_goalkeeperShare, &QSpinBox::valueChanged, this, factorsEdited);
    auto *body = new QHBoxLayout; body->setSpacing(16);
    auto *left = new QVBoxLayout;
    m_search = new QLineEdit; m_search->setPlaceholderText(tr("Attribute suchen")); m_search->setAccessibleName(tr("Attribute suchen"));
    m_attributes = new QListWidget; m_attributes->setObjectName(QStringLiteral("ratingAttributes")); m_attributes->setMinimumWidth(144); m_attributes->setMaximumWidth(200);
    m_attributes->setAccessibleName(tr("Attribute"));
    m_category = new QComboBox; m_category->setAccessibleName(tr("Attributkategorie"));
    m_category->addItems({tr("Alle Attribute"), tr("Technisch"), tr("Mental"), tr("Physisch"), tr("Torhüter")});
    left->addWidget(m_search); left->addWidget(m_category); left->addWidget(m_attributes, 1); body->addLayout(left);
    m_curve = new RatingCurveEditor; body->addWidget(m_curve, 1); layout->addLayout(body, 1);
    auto *actions = new FlowLayout;
    const auto action = [this, actions](const QString &name, auto callback) {
        auto *b = new QPushButton(name); b->setObjectName(QStringLiteral("profileAction%1").arg(actions->count())); actions->addWidget(b); connect(b, &QPushButton::clicked, this, callback);
    };
    action(tr("Profil speichern"), [this] { save(false); });
    action(tr("Als neues Profil speichern"), [this] { save(true); });
    action(tr("Ursprung wiederherstellen"), [this] { resetProfile(); });
    action(tr("Entwurf verwerfen"), [this] { if (discardDraft()) selectProfile(m_selectedId); });
    layout->addLayout(actions);
    m_status = new QLabel; m_status->setObjectName(QStringLiteral("kpiCaption")); m_status->setWordWrap(true); layout->addWidget(m_status);
    connect(m_profiles, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading) return;
        const auto id = m_profiles->currentData().toString();
        if (!discardDraft()) { const QSignalBlocker b(m_profiles); m_profiles->setCurrentIndex(m_profiles->findData(m_selectedId)); return; }
        selectProfile(id);
    });
    connect(m_attributes, &QListWidget::currentRowChanged, this, [this] { selectAttribute(); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { filterAttributes(); });
    connect(m_category, &QComboBox::currentIndexChanged, this, [this] { filterAttributes(); });
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &name) { m_draft.name = name; m_dirty = true; });
    connect(m_curve, &RatingCurveEditor::curveChanged, this, [this] {
        if (m_loading || m_attribute.isEmpty()) return;
        m_draft.attributes[m_attribute] = m_curve->curve(); m_dirty = true;
        m_status->setText(tr("Ungespeicherter Entwurf. Bestehende Bewertungen bleiben unverändert."));
    });
    if (QFile::exists(m_path)) {
        QString error;
        const auto loaded = RatingProfileLibrary::load(m_path, &error);
        if (loaded) m_library = *loaded;
        else { m_storageValid = false; reportError(error); }
    }
    fillProfiles();
}
void RatingProfileEditor::setChartPalette(const QPalette &palette) { m_curve->setChartPalette(palette); }
bool RatingProfileEditor::installBuiltins(const QList<RatingProfile> &profiles)
{
    auto candidate = m_library;
    const auto error = candidate.seedBuiltins(profiles);
    if (!error.isEmpty()) { reportError(error); return false; }
    if (candidate.toJson() == m_library.toJson()) return true;
    return persist(std::move(candidate), m_selectedId);
}
bool RatingProfileEditor::discardDraft()
{
    if (!m_dirty) return true;
    if (QMessageBox::question(this, tr("Entwurf verwerfen"), tr("Ungespeicherte Profiländerungen verwerfen?"),
                              QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) return false;
    selectProfile(m_selectedId); return true;
}
void RatingProfileEditor::fillProfiles(const QString &selected)
{
    m_loading = true; m_profiles->clear();
    for (const auto &p : m_library.profiles())
        m_profiles->addItem(p.name + (m_library.customized(p.id) ? tr(" · Angepasst") : QString()), p.id);
    if (!selected.isEmpty()) m_profiles->setCurrentIndex(m_profiles->findData(selected));
    m_loading = false; selectProfile(m_profiles->currentData().toString());
}
void RatingProfileEditor::selectProfile(const QString &id)
{
    m_selectedId = id; m_dirty = false;
    m_draft = m_library.profiles().value(id);
    m_expectedRevision = RatingProfileLibrary::documentRevision(m_draft);
    m_name->setText(m_draft.name); m_name->setEnabled(!id.isEmpty());
    updateFactors();
    setActiveProfile(m_activeId);
    fillAttributes();
    if (m_storageValid) m_status->setText(tr("Profile bearbeiten und austauschen. Speichern ändert den Spielstand nicht; "
                                             "„Für diesen Spielstand verwenden“ rechnet mit dem gespeicherten Profil neu."));
}
QString RatingProfileEditor::attributeLabel(int index)
{
    static const QStringList labels = {
        tr("Eins gegen Eins"),
        tr("Antritt"),
        tr("Reichweite"),
        tr("Aggressivität"),
        tr("Beweglichkeit"),
        tr("Antizipation"),
        tr("Balance"),
        tr("Mut"),
        tr("Strafraumbeherrschung"),
        tr("Konzentration"),
        tr("Nervenstärke"),
        tr("Ecken"),
        tr("Flanken"),
        tr("Entscheidungen"),
        tr("Zielstrebigkeit"),
        tr("Dribbling"),
        tr("Abschluss"),
        tr("Ballannahme"),
        tr("Flair"),
        tr("Fangsicherheit"),
        tr("Kopfballtechnik"),
        tr("Sprunghöhe"),
        tr("Abschläge"),
        tr("Führungsqualitäten"),
        tr("Weitschüsse"),
        tr("Deckung"),
        tr("Ohne Ball"),
        tr("Schnelligkeit"),
        tr("Passen"),
        tr("Stellungsspiel"),
        tr("Reflexe"),
        tr("Herauslaufen (Tendenz)"),
        tr("Ausdauer"),
        tr("Kraft"),
        tr("Tackling"),
        tr("Teamwork"),
        tr("Technik"),
        tr("Abwürfe"),
        tr("Übersicht"),
        tr("Einsatzfreude"),
        tr("Kommunikation"),
        tr("Exzentrik"),
        tr("Grundfitness"),
        tr("Freistöße"),
        tr("Weite Einwürfe"),
        tr("Elfmeter"),
        tr("Fausten")};
    return index >= 0 && index < labels.size() ? labels[index] : QString();
}
void RatingProfileEditor::updateFactors()
{
    m_loading = true;
    const bool enabled = !m_selectedId.isEmpty();
    m_keyFactor->setEnabled(enabled); m_preferableFactor->setEnabled(enabled); m_roleShareOn->setEnabled(enabled);
    m_keyFactor->setValue(m_draft.keyFactor);
    m_preferableFactor->setValue(m_draft.preferableFactor);
    // Classic keeps 2.2.3 arithmetic: no role part.
    const bool classic = m_draft.calculationVersion == QStringLiteral("classic-midpoint-v1");
    m_roleShareOn->setEnabled(enabled && !classic);
    m_roleShareOn->setChecked(m_draft.roleShare > 0);
    if (m_draft.roleShare > 0) m_roleShare->setValue(qRound(m_draft.roleShare * 100));
    m_roleShare->setEnabled(enabled && !classic && m_draft.roleShare > 0);
    m_roleShareOn->setToolTip(classic ? tr("Das Classic-Verfahren rechnet unverändert wie 2.2.3 und hat keinen Rollenanteil.") : QString());
    m_goalkeeperShareOn->setEnabled(enabled && !classic);
    m_goalkeeperShareOn->setChecked(m_draft.goalkeeperRoleShare > 0);
    if (m_draft.goalkeeperRoleShare > 0) m_goalkeeperShare->setValue(qRound(m_draft.goalkeeperRoleShare * 100));
    m_goalkeeperShare->setEnabled(enabled && !classic && m_draft.goalkeeperRoleShare > 0);
    m_goalkeeperShareOn->setToolTip(m_roleShareOn->toolTip());
    m_loading = false;
}
void RatingProfileEditor::setActiveProfile(const QString &id)
{
    // A newly active profile opens in the editor unless a draft is pending.
    const bool changed = m_activeId != id;
    m_activeId = id;
    if (changed && !m_dirty && id != m_selectedId) {
        const int index = m_profiles->findData(id);
        if (index >= 0) m_profiles->setCurrentIndex(index);
    }
    const auto profile = m_library.profiles().value(m_selectedId);
    const bool compatible = !m_selectedId.isEmpty() && profile.compatibleGames.contains(m_game);
    m_useForSave->setEnabled(compatible);
    m_useForSave->setToolTip(compatible ? QString()
                             : tr("Dieses Profil passt nicht zur Football-Manager-Version des Spielstands."));
    for (int i = 0; i < m_profiles->count(); ++i) {
        const auto itemId = m_profiles->itemData(i).toString();
        const auto &p = m_library.profiles().value(itemId);
        QString label = p.name + (m_library.customized(itemId) ? tr(" · Angepasst") : QString());
        if (itemId == m_activeId) label += tr(" · Aktiv");
        if (!p.compatibleGames.contains(m_game)) label += tr(" · nur %1").arg(p.compatibleGames.join(QStringLiteral("/")).toUpper());
        m_profiles->setItemText(i, label);
    }
}
void RatingProfileEditor::fillAttributes()
{
    const QSignalBlocker blocker(m_attributes); m_attributes->clear();
    const int categories[] = {4, 3, 4, 2, 3, 2, 3, 2, 4, 2, 2, 1, 1, 2, 2, 1, 1, 1, 2, 4, 1, 3, 4, 2, 1, 1, 2, 3, 1, 2, 4, 4, 3, 3, 1, 2, 1, 4, 2, 2, 4, 4, 3, 1, 1, 1, 4};
    if (!m_selectedId.isEmpty()) for (int i = 0; i < kAttrCount; ++i) {
        const auto &key = attrNames()[i];
        const QString label = attributeLabel(i);
        auto *item = new QListWidgetItem(label, m_attributes);
        item->setData(Qt::UserRole, key); item->setData(Qt::UserRole + 1, categories[i]);
        QString evidence;
        const auto state = m_draft.attributeEvidence.value(key);
        if (state == QStringLiteral("measured")) evidence = tr("Untersucht – angepasste Messkurve");
        else if (state == QStringLiteral("noise-filtered")) evidence = tr("Als Rauschen eingestuft – im Editor bearbeitbar");
        else if (state == QStringLiteral("not-investigated")) evidence = tr("Nicht untersucht – im Editor bearbeitbar");
        else if (state == QStringLiteral("legacy")) evidence = tr("Bisherige DWRS-Bewertungsgrundlage");
        const auto active = m_draft.attributes.value(key, RatingCurve::linear(0)).weight == 0
                            ? tr("Deaktiviert – im Editor bearbeitbar") : tr("Aktiv");
        item->setToolTip(label + QLatin1Char('\n') + active + QLatin1Char('\n') + evidence);
    }
    filterAttributes();
    if (m_attributes->count()) {
        int row = 0;
        for (int i = 0; i < m_attributes->count(); ++i)
            if (m_attributes->item(i)->data(Qt::UserRole).toString() == m_attribute) row = i;
        m_attributes->setCurrentRow(row);
    }
    selectAttribute();
}
void RatingProfileEditor::filterAttributes()
{
    for (int i = 0; i < m_attributes->count(); ++i) {
        auto *item = m_attributes->item(i);
        const bool match = item->text().contains(m_search->text(), Qt::CaseInsensitive)
                           || item->data(Qt::UserRole).toString().contains(m_search->text(), Qt::CaseInsensitive);
        item->setHidden(!match || (m_category->currentIndex() != 0
                         && item->data(Qt::UserRole + 1).toInt() != m_category->currentIndex()));
    }
}
void RatingProfileEditor::selectAttribute()
{
    const auto *item = m_attributes->currentItem();
    m_attribute = item ? item->data(Qt::UserRole).toString() : QString();
    m_curve->setEnabled(item != nullptr);
    if (item) { m_loading = true; m_curve->setCurve(m_draft.attributes.value(m_attribute, RatingCurve::linear(0))); m_loading = false; }
}
bool RatingProfileEditor::persist(RatingProfileLibrary candidate, const QString &selected)
{
    if (!m_storageValid) { reportError(QStringLiteral("library-conflict")); return false; }
    const auto error = candidate.save(m_path);
    if (!error.isEmpty()) { reportError(error); return false; }
    m_library = std::move(candidate); m_dirty = false; fillProfiles(selected);
    emit librarySaved();
    return true;
}
void RatingProfileEditor::save(bool asNew)
{
    if (m_draft.id.isEmpty()) return;
    auto candidate = m_library; auto profile = m_draft;
    if (asNew) {
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("Als neues Profil speichern"), tr("Profilname"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        profile.name = name; profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const auto error = asNew ? candidate.insert(profile) : candidate.update(profile, m_expectedRevision);
    if (!error.isEmpty()) { reportError(error); return; }
    persist(std::move(candidate), profile.id);
}
void RatingProfileEditor::newProfile()
{
    if (!discardDraft()) return;
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Neues Profil"), tr("Profilname"), QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    RatingProfile profile; profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces); profile.name = name;
    profile.compatibleGames = {m_game};
    profile.origin = QStringLiteral("personal");
    for (const auto &name : attrNames()) profile.attributes.insert(name, RatingCurve::linear(0));
    auto candidate = m_library; const auto error = candidate.insert(profile);
    if (!error.isEmpty()) { reportError(error); return; }
    persist(std::move(candidate), profile.id);
}
void RatingProfileEditor::removeProfile()
{
    if (m_selectedId.isEmpty()) return;
    if (QMessageBox::question(this, tr("Profil löschen"), tr("Dieses Profil löschen?")) != QMessageBox::Yes) return;
    auto candidate = m_library; const auto error = candidate.remove(m_selectedId);
    if (!error.isEmpty()) { reportError(error); return; }
    persist(std::move(candidate), {});
}
void RatingProfileEditor::resetProfile()
{
    if (m_selectedId.isEmpty() || !discardDraft()) return;
    auto candidate = m_library; const auto error = candidate.restore(m_selectedId);
    if (!error.isEmpty()) { reportError(error); return; }
    // Reset remains a draft until explicitly saved.
    m_draft = candidate.profiles().value(m_selectedId); m_name->setText(m_draft.name); m_dirty = true; fillAttributes();
}
void RatingProfileEditor::importProfile()
{
    if (!discardDraft()) return;
    const auto path = QFileDialog::getOpenFileName(this, tr("Profil importieren"), {}, tr("JSON-Profile (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { reportError(file.errorString()); return; }
    QJsonParseError parse; const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    QString error;
    auto profile = parse.error == QJsonParseError::NoError && json.isObject() ? RatingProfile::fromJson(json.object(), &error) : std::nullopt;
    if (!profile) { reportError(error); return; }
    auto candidate = m_library; error = candidate.insert(*profile);
    if (error == QStringLiteral("id-conflict") || error == QStringLiteral("name-conflict")) {
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("Als neues Profil importieren"), tr("Ein Profil mit dieser Kennung oder diesem Namen existiert bereits. Neuen Namen vergeben:"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        profile->id = QUuid::createUuid().toString(QUuid::WithoutBraces); profile->name = name;
        error = candidate.insert(*profile);
    }
    if (!error.isEmpty()) { reportError(error); return; }
    persist(std::move(candidate), profile->id);
}
void RatingProfileEditor::exportProfile()
{
    if (m_draft.id.isEmpty()) return;
    const auto validation = m_draft.validate();
    if (!validation.isEmpty()) { reportError(validation); return; }
    const auto path = QFileDialog::getSaveFileName(this, tr("Profilentwurf exportieren"), {}, tr("JSON-Profile (*.json)"));
    if (path.isEmpty()) return;
    const auto bytes = QJsonDocument(m_draft.toJson()).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) reportError(file.errorString());
}
void RatingProfileEditor::reportError(const QString &error)
{
    qWarning("Rating profile operation failed: %s", qPrintable(error));
    if (error == QStringLiteral("name-conflict") || error == QStringLiteral("id-conflict"))
        m_status->setText(tr("Name oder Kennung bereits vorhanden. Bitte einen anderen Namen verwenden."));
    else if (error == QStringLiteral("library-conflict") || error == QStringLiteral("concurrent-edit"))
        m_status->setText(tr("Die Profilbibliothek wurde zwischenzeitlich geändert. Bitte die Einstellungen neu öffnen, bevor du speicherst."));
    else m_status->setText(tr("Das Profil konnte nicht übernommen werden. Prüfe Namen, Dateiformat, gültige Kurven und Schreibrechte."));
}
} // namespace fm
