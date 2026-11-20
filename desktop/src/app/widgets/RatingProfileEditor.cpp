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
    m_profiles = new QComboBox; m_profiles->setObjectName(QStringLiteral("ratingProfiles")); m_profiles->setMinimumWidth(200); m_profiles->setAccessibleName(tr("Rating profile"));
    top->addWidget(m_profiles, 1);
    const auto button = [this, top](const QString &name, auto callback) {
        auto *b = new QPushButton(name); top->addWidget(b); connect(b, &QPushButton::clicked, this, callback);
    };
    button(tr("New"), [this] { newProfile(); });
    button(tr("Import"), [this] { importProfile(); });
    button(tr("Export"), [this] { exportProfile(); });
    button(tr("Delete"), [this] { removeProfile(); });
    layout->addLayout(top);
    m_name = new QLineEdit; m_name->setObjectName(QStringLiteral("ratingProfileName")); m_name->setPlaceholderText(tr("Profile name")); m_name->setAccessibleName(tr("Profile name"));
    layout->addWidget(m_name);
    m_useForSave = new QPushButton(tr("Use for this save"));
    m_useForSave->setObjectName(QStringLiteral("useRatingProfile"));
    connect(m_useForSave, &QPushButton::clicked, this, [this] {
        if (m_dirty) {
            const auto answer = QMessageBox::question(this, tr("Use for this save"),
                tr("The profile has unsaved changes. Save first and then use it?"),
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
    m_keyFactor->setAccessibleName(tr("Key attribute factor"));
    m_preferableFactor = new QDoubleSpinBox; m_preferableFactor->setRange(0, 5); m_preferableFactor->setDecimals(2); m_preferableFactor->setSingleStep(0.1); m_preferableFactor->setMaximumWidth(120);
    m_preferableFactor->setAccessibleName(tr("Preferable attribute factor"));
    factors->addRow(tr("Key attribute factor"), m_keyFactor);
    factors->addRow(tr("Preferable attribute factor"), m_preferableFactor);
    auto *shareRow = new QHBoxLayout; shareRow->setContentsMargins(0, 0, 0, 0);
    m_roleShareOn = new QCheckBox(tr("Use"));
    m_roleShareOn->setObjectName(QStringLiteral("roleShareEnabled"));
    m_roleShare = new QSpinBox; m_roleShare->setRange(1, int(RatingProfile::kMaxRoleShare * 100)); m_roleShare->setSuffix(QStringLiteral(" %"));
    m_roleShare->setObjectName(QStringLiteral("roleShare")); m_roleShare->setValue(33);
    m_roleShare->setAccessibleName(tr("Role emphasis"));
    shareRow->addWidget(m_roleShareOn); shareRow->addWidget(m_roleShare); shareRow->addStretch(1);
    factors->addRow(tr("Role emphasis"), shareRow);
    auto *goalkeeperRow = new QHBoxLayout; goalkeeperRow->setContentsMargins(0, 0, 0, 0);
    m_goalkeeperShareOn = new QCheckBox(tr("Use"));
    m_goalkeeperShareOn->setObjectName(QStringLiteral("goalkeeperRoleShareEnabled"));
    m_goalkeeperShare = new QSpinBox; m_goalkeeperShare->setRange(1, int(RatingProfile::kMaxRoleShare * 100)); m_goalkeeperShare->setSuffix(QStringLiteral(" %"));
    m_goalkeeperShare->setObjectName(QStringLiteral("goalkeeperRoleShare")); m_goalkeeperShare->setValue(20);
    m_goalkeeperShare->setAccessibleName(tr("Goalkeeper role emphasis"));
    goalkeeperRow->addWidget(m_goalkeeperShareOn); goalkeeperRow->addWidget(m_goalkeeperShare); goalkeeperRow->addStretch(1);
    factors->addRow(tr("Goalkeeper role emphasis"), goalkeeperRow);
    layout->addLayout(factors);
    auto *explainToggle = new QToolButton; explainToggle->setText(tr("What is role emphasis?"));
    explainToggle->setCheckable(true); explainToggle->setAutoRaise(true); explainToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    explainToggle->setArrowType(Qt::RightArrow);
    auto *explain = new QLabel(tr(
        "The base curves of a profile are the same for every role. The factors for key and preferable "
        "attributes only strengthen attributes that already have a weight. In the FM27 profile, for "
        "example, Finishing, Passing or Tackling have hardly any base weight, so a player's roles would "
        "otherwise barely differ.\n"
        "\n"
        "The role share gives every key and preferable attribute of the role an extra, evenly rising "
        "weight (key 1.5 times). The percentage is the share of this role part in the finished rating; "
        "the scale stays 0 to 100.\n"
        "\n"
        "Goalkeeper roles list almost the same attributes. Their role share therefore only counts the "
        "attributes in which a role differs from the other goalkeeper roles of the same phase (never "
        "Eccentricity). The percentage applies to the role with the most differences; the others get "
        "proportionally less.\n"
        "\n"
        "On the test save the FM27 profile with 33 % predicts the average rating better than without "
        "(correlation 0.207 instead of 0.188) and separates the roles more clearly. Without the tick the "
        "profile only uses the base curves."));
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
        m_status->setText(tr("Unsaved draft. Existing ratings remain unchanged."));
    };
    connect(m_keyFactor, &QDoubleSpinBox::valueChanged, this, factorsEdited);
    connect(m_preferableFactor, &QDoubleSpinBox::valueChanged, this, factorsEdited);
    connect(m_roleShareOn, &QCheckBox::toggled, this, factorsEdited);
    connect(m_roleShare, &QSpinBox::valueChanged, this, factorsEdited);
    connect(m_goalkeeperShareOn, &QCheckBox::toggled, this, factorsEdited);
    connect(m_goalkeeperShare, &QSpinBox::valueChanged, this, factorsEdited);
    auto *body = new QHBoxLayout; body->setSpacing(16);
    auto *left = new QVBoxLayout;
    m_search = new QLineEdit; m_search->setPlaceholderText(tr("Search attributes")); m_search->setAccessibleName(tr("Search attributes"));
    m_attributes = new QListWidget; m_attributes->setObjectName(QStringLiteral("ratingAttributes")); m_attributes->setMinimumWidth(144); m_attributes->setMaximumWidth(200);
    m_attributes->setAccessibleName(tr("Attributes"));
    m_category = new QComboBox; m_category->setAccessibleName(tr("Attribute category"));
    m_category->addItems({tr("All attributes"), tr("Technical"), tr("Mental"), tr("Physical"), tr("Goalkeeping")});
    left->addWidget(m_search); left->addWidget(m_category); left->addWidget(m_attributes, 1); body->addLayout(left);
    m_curve = new RatingCurveEditor; body->addWidget(m_curve, 1); layout->addLayout(body, 1);
    auto *actions = new FlowLayout;
    const auto action = [this, actions](const QString &name, auto callback) {
        auto *b = new QPushButton(name); b->setObjectName(QStringLiteral("profileAction%1").arg(actions->count())); actions->addWidget(b); connect(b, &QPushButton::clicked, this, callback);
    };
    action(tr("Save profile"), [this] { save(false); });
    action(tr("Save as new profile"), [this] { save(true); });
    action(tr("Restore original"), [this] { resetProfile(); });
    action(tr("Discard draft"), [this] { if (discardDraft()) selectProfile(m_selectedId); });
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
        m_status->setText(tr("Unsaved draft. Existing ratings remain unchanged."));
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
    if (QMessageBox::question(this, tr("Discard draft"), tr("Discard unsaved profile changes?"),
                              QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) return false;
    selectProfile(m_selectedId); return true;
}
void RatingProfileEditor::fillProfiles(const QString &selected)
{
    m_loading = true; m_profiles->clear();
    for (const auto &p : m_library.profiles())
        m_profiles->addItem(p.name + (m_library.customized(p.id) ? tr(" · Customized") : QString()), p.id);
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
    if (m_storageValid) m_status->setText(tr("Edit and exchange profiles. Saving does not change the save; “Use for this "
                                             "save” recalculates with the saved profile."));
}
QString RatingProfileEditor::attributeLabel(int index)
{
    static const QStringList labels = {
        tr("One vs One"),
        tr("Acceleration"),
        tr("Aerial Reach"),
        tr("Aggression"),
        tr("Agility"),
        tr("Anticipation"),
        tr("Balance"),
        tr("Bravery"),
        tr("Command of Area"),
        tr("Concentration"),
        tr("Composure"),
        tr("Corners"),
        tr("Crossing"),
        tr("Decisions"),
        tr("Determination"),
        tr("Dribbling"),
        tr("Finishing"),
        tr("First Touch"),
        tr("Flair"),
        tr("Handling"),
        tr("Heading"),
        tr("Jumping Reach"),
        tr("Kicking"),
        tr("Leadership"),
        tr("Long Shots"),
        tr("Marking"),
        tr("Off the Ball"),
        tr("Pace"),
        tr("Passing"),
        tr("Positioning"),
        tr("Reflexes"),
        tr("Rushing Out (Tendency)"),
        tr("Stamina"),
        tr("Strength"),
        tr("Tackling"),
        tr("Teamwork"),
        tr("Technique"),
        tr("Throwing"),
        tr("Vision"),
        tr("Work Rate"),
        tr("Communication"),
        tr("Eccentricity"),
        tr("Natural Fitness"),
        tr("Free Kick Taking"),
        tr("Long Throws"),
        tr("Penalty Taking"),
        tr("Punching")};
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
    m_roleShareOn->setToolTip(classic ? tr("The Classic method calculates exactly as in 2.2.3 and has no role emphasis.") : QString());
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
                             : tr("This profile does not match the save's Football Manager version."));
    for (int i = 0; i < m_profiles->count(); ++i) {
        const auto itemId = m_profiles->itemData(i).toString();
        const auto &p = m_library.profiles().value(itemId);
        QString label = p.name + (m_library.customized(itemId) ? tr(" · Customized") : QString());
        if (itemId == m_activeId) label += tr(" · Active");
        if (!p.compatibleGames.contains(m_game)) label += tr(" · %1 only").arg(p.compatibleGames.join(QStringLiteral("/")).toUpper());
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
        if (state == QStringLiteral("measured")) evidence = tr("Investigated – fitted measurement curve");
        else if (state == QStringLiteral("noise-filtered")) evidence = tr("Classified as noise – editable");
        else if (state == QStringLiteral("not-investigated")) evidence = tr("Not investigated – editable");
        else if (state == QStringLiteral("legacy")) evidence = tr("Previous DWRS rating basis");
        const auto active = m_draft.attributes.value(key, RatingCurve::linear(0)).weight == 0
                            ? tr("Disabled – editable") : tr("Active");
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
        const auto name = QInputDialog::getText(this, tr("Save as new profile"), tr("Profile name"), QLineEdit::Normal, {}, &ok);
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
    const auto name = QInputDialog::getText(this, tr("New profile"), tr("Profile name"), QLineEdit::Normal, {}, &ok);
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
    if (QMessageBox::question(this, tr("Delete profile"), tr("Delete this profile?")) != QMessageBox::Yes) return;
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
    const auto path = QFileDialog::getOpenFileName(this, tr("Import profile"), {}, tr("JSON profiles (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { reportError(file.errorString()); return; }
    QJsonParseError parse; const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    QString error;
    auto profile = parse.error == QJsonParseError::NoError && json.isObject() ? RatingProfile::fromJson(json.object(), &error) : std::nullopt;
    if (!profile) { reportError(error); return; }
    auto candidate = m_library; error = candidate.insert(*profile);
    if (error == QStringLiteral("id-conflict") || error == QStringLiteral("name-conflict")) {
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("Import as new profile"), tr("A profile with this ID or name already exists. Enter a new name:"), QLineEdit::Normal, {}, &ok);
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
    const auto path = QFileDialog::getSaveFileName(this, tr("Export profile draft"), {}, tr("JSON profiles (*.json)"));
    if (path.isEmpty()) return;
    const auto bytes = QJsonDocument(m_draft.toJson()).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) reportError(file.errorString());
}
void RatingProfileEditor::reportError(const QString &error)
{
    qWarning("Rating profile operation failed: %s", qPrintable(error));
    if (error == QStringLiteral("name-conflict") || error == QStringLiteral("id-conflict"))
        m_status->setText(tr("Name or ID already exists. Please use a different name."));
    else if (error == QStringLiteral("library-conflict") || error == QStringLiteral("concurrent-edit"))
        m_status->setText(tr("The profile library has changed. Please reopen settings before saving."));
    else m_status->setText(tr("The profile could not be saved or loaded. Check the name, file format, curve validity and write permissions."));
}
} // namespace fm
