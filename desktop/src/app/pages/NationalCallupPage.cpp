#include "NationalCallupPage.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "PageHelpers.h"
#include "core/HtmlImporter.h"
#include "core/NationalCallup.h"
#include "core/SquadBuilder.h"
#include "core/Utils.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace fm {

namespace {

// Player display line for the result/injured lists.
QString playerLine(const Player &p)
{
    return QStringLiteral("%1 (%2) — %3 | %4").arg(p.name).arg(p.age).arg(p.club, p.positionRaw);
}

// The user marks players who have left the game by setting their club to
// "Retired"; such players must never be proposed for a call-up.
bool isRetiredClub(const Player &p)
{
    return p.club.trimmed().compare(QLatin1String("Retired"), Qt::CaseInsensitive) == 0;
}

} // namespace

NationalCallupPage::NationalCallupPage(AppContext &context, QWidget *parent)
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
    layout->setSpacing(12);
    scroll->setWidget(content);

    auto *heading =
        new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Nominierungs-Assistent")), content);
    layout->addWidget(heading);

    m_hint = new QLabel(content);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    // --- Step 1: current-squad baseline ---
    auto *baselineBox = new QGroupBox(tr("1. Aktueller Kader"), content);
    auto *baselineLayout = new QVBoxLayout(baselineBox);
    m_savedRadio = new QRadioButton(tr("Gespeicherten Nationalkader verwenden"), baselineBox);
    m_uploadRadio = new QRadioButton(tr("Kader aus FM-HTML-Export hochladen"), baselineBox);
    m_savedRadio->setChecked(true);
    auto *group = new QButtonGroup(this);
    group->addButton(m_savedRadio);
    group->addButton(m_uploadRadio);
    auto *uploadRow = new QHBoxLayout;
    m_uploadButton = new QPushButton(tr("Kader-Datei wählen…"), baselineBox);
    m_baselineLabel = new QLabel(baselineBox);
    m_baselineLabel->setObjectName(QStringLiteral("kpiCaption"));
    uploadRow->addWidget(m_uploadButton);
    uploadRow->addWidget(m_baselineLabel, 1);
    baselineLayout->addWidget(m_savedRadio);
    baselineLayout->addWidget(m_uploadRadio);
    baselineLayout->addLayout(uploadRow);
    layout->addWidget(baselineBox);

    connect(m_uploadButton, &QPushButton::clicked, this, &NationalCallupPage::uploadSquad);
    connect(group, &QButtonGroup::buttonToggled, this, [this] {
        m_uploadButton->setEnabled(m_uploadRadio->isChecked());
        refresh();
    });

    // --- Step 2: parameters ---
    auto *paramBox = new QGroupBox(tr("2. Parameter"), content);
    auto *paramLayout = new QHBoxLayout(paramBox);
    m_totalSpin = new QSpinBox(paramBox);
    m_totalSpin->setRange(11, 40);
    m_totalSpin->setValue(23);
    m_gkSpin = new QSpinBox(paramBox);
    m_gkSpin->setRange(1, 5);
    m_gkSpin->setValue(3);
    m_tacticCombo = new QComboBox(paramBox);
    m_tacticCombo->setMinimumWidth(240);
    paramLayout->addWidget(new QLabel(tr("Kadergröße gesamt:"), paramBox));
    paramLayout->addWidget(m_totalSpin);
    paramLayout->addSpacing(16);
    paramLayout->addWidget(new QLabel(tr("davon Torhüter:"), paramBox));
    paramLayout->addWidget(m_gkSpin);
    paramLayout->addSpacing(16);
    paramLayout->addWidget(new QLabel(tr("Taktik:"), paramBox));
    paramLayout->addWidget(m_tacticCombo, 1);
    layout->addWidget(paramBox);

    // --- Step 3: injured / unavailable ---
    auto *injuredBox = new QGroupBox(tr("3. Verletzt / nicht verfügbar (für diesen Durchlauf)"),
                                     content);
    auto *injuredLayout = new QVBoxLayout(injuredBox);
    m_injuredSearch = new QLineEdit(injuredBox);
    m_injuredSearch->setPlaceholderText(tr("Pool nach Name durchsuchen…"));
    m_injuredSearch->setClearButtonEnabled(true);
    m_injuredList = new QListWidget(injuredBox);
    m_injuredList->setMaximumHeight(200);
    m_injuredCountLabel = new QLabel(injuredBox);
    m_injuredCountLabel->setObjectName(QStringLiteral("kpiCaption"));
    injuredLayout->addWidget(m_injuredSearch);
    injuredLayout->addWidget(m_injuredList);
    injuredLayout->addWidget(m_injuredCountLabel);
    layout->addWidget(injuredBox);

    connect(m_injuredSearch, &QLineEdit::textChanged, this,
            [this] { rebuildInjuredList(); });
    connect(m_injuredList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_updatingInjured)
            return;
        const QString uid = item->data(Qt::UserRole).toString();
        if (item->checkState() == Qt::Checked)
            m_injuredUids.insert(uid);
        else
            m_injuredUids.remove(uid);
        m_injuredCountLabel->setText(tr("%1 Spieler als nicht verfügbar markiert.")
                                         .arg(m_injuredUids.size()));
    });

    // --- Compute ---
    auto *computeRow = new QHBoxLayout;
    computeRow->addStretch(1);
    m_computeButton = new QPushButton(tr("Nominierung berechnen"), content);
    m_computeButton->setDefault(true);
    computeRow->addWidget(m_computeButton);
    layout->addLayout(computeRow);
    connect(m_computeButton, &QPushButton::clicked, this, &NationalCallupPage::compute);

    // --- Results ---
    m_resultsBox = new QWidget(content);
    auto *resultsLayout = new QVBoxLayout(m_resultsBox);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_summaryLabel = new QLabel(m_resultsBox);
    m_summaryLabel->setWordWrap(true);
    resultsLayout->addWidget(m_summaryLabel);

    auto *listsRow = new QHBoxLayout;
    auto *inviteColumn = new QVBoxLayout;
    auto *inviteTitle = new QLabel(tr("✅ Einladen"), m_resultsBox);
    inviteTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_inviteList = new QListWidget(m_resultsBox);
    inviteColumn->addWidget(inviteTitle);
    inviteColumn->addWidget(m_inviteList, 1);
    auto *dropColumn = new QVBoxLayout;
    auto *dropTitle = new QLabel(tr("❌ Ausladen"), m_resultsBox);
    dropTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_dropList = new QListWidget(m_resultsBox);
    dropColumn->addWidget(dropTitle);
    dropColumn->addWidget(m_dropList, 1);
    listsRow->addLayout(inviteColumn, 1);
    listsRow->addLayout(dropColumn, 1);
    resultsLayout->addLayout(listsRow);

    auto *applyRow = new QHBoxLayout;
    applyRow->addStretch(1);
    m_applyButton = new QPushButton(tr("Empfohlenen Kader übernehmen"), m_resultsBox);
    applyRow->addWidget(m_applyButton);
    resultsLayout->addLayout(applyRow);
    connect(m_applyButton, &QPushButton::clicked, this,
            &NationalCallupPage::applyRecommendation);

    layout->addWidget(m_resultsBox);
    layout->addStretch(1);

    for (QListWidget *list : {m_inviteList, m_dropList}) {
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
            PlayerActions::openProfile(m_context, item->data(Qt::UserRole).toString());
        });
        // Copy just the bare player name (for pasting into the in-game search)
        // via right-click or Ctrl+C on the selected row.
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QListWidget::customContextMenuRequested, this,
                [this, list](const QPoint &pos) {
                    QListWidgetItem *item = list->itemAt(pos);
                    const QString name = item ? item->data(Qt::UserRole + 1).toString() : QString();
                    if (name.isEmpty())
                        return;
                    QMenu menu(this);
                    QAction *copyName = menu.addAction(tr("Namen kopieren"));
                    QAction *openProfile = menu.addAction(tr("Profil öffnen"));
                    QAction *chosen = menu.exec(list->viewport()->mapToGlobal(pos));
                    if (chosen == copyName)
                        QApplication::clipboard()->setText(name);
                    else if (chosen == openProfile)
                        PlayerActions::openProfile(m_context, item->data(Qt::UserRole).toString());
                });
        auto *copyShortcut = new QShortcut(QKeySequence::Copy, list);
        copyShortcut->setContext(Qt::WidgetShortcut);
        connect(copyShortcut, &QShortcut::activated, this, [list] {
            if (const QListWidgetItem *item = list->currentItem()) {
                const QString name = item->data(Qt::UserRole + 1).toString();
                if (!name.isEmpty())
                    QApplication::clipboard()->setText(name);
            }
        });
    }

    showResults(false);
    m_uploadButton->setEnabled(false);
}

void NationalCallupPage::refresh()
{
    const QString name = m_context.nationalTeamName();
    const QString code = m_context.nationalTeamCode();
    const int ageLimit = m_context.nationalTeamAgeLimit();
    const bool configured = !name.isEmpty() && !code.isEmpty() && ageLimit > 0;

    if (!configured) {
        m_hint->setText(
            tr("⚠️ Bitte konfiguriere zuerst dein Nationalteam vollständig unter "
               "Einstellungen → Verein (Name, Länder-Code, Altersgrenze)."));
        m_computeButton->setEnabled(false);
        showResults(false);
        return;
    }
    m_computeButton->setEnabled(true);
    m_hint->setText(tr("Pool: Spieler mit Nationalität '%1'%2. Der Assistent stellt aus dem "
                       "gesamten berechtigten Pool den stärksten Kader zusammen und vergleicht "
                       "ihn mit deinem aktuellen Kader.")
                        .arg(code, ageLimit < 99 ? tr(" bis Alter %1").arg(ageLimit)
                                                 : QString()));

    // Tactic list (national favorites first).
    const QString previousTactic = m_tacticCombo->currentText();
    m_tacticCombo->clear();
    m_tacticCombo->addItems(favoritesFirstTactics(m_context, true));
    if (!previousTactic.isEmpty() && m_tacticCombo->findText(previousTactic) >= 0)
        m_tacticCombo->setCurrentText(previousTactic);

    // Baseline label.
    if (m_uploadRadio->isChecked()) {
        m_baselineLabel->setText(m_haveUpload
                                     ? tr("Hochgeladener Kader: %1 Spieler.").arg(m_uploadedUids.size())
                                     : tr("Noch keine Kader-Datei geladen."));
    } else {
        m_baselineLabel->setText(
            tr("Gespeicherter Nationalkader: %1 Spieler.").arg(nationalSquadUids(m_context).size()));
    }

    rebuildInjuredList();
    showResults(false);
}

bool NationalCallupPage::isEligible(const Player &player) const
{
    if (isRetiredClub(player))
        return false;
    const QString code = m_context.nationalTeamCode();
    if (player.nationality != code && player.secondNationality != code)
        return false;
    const int ageLimit = m_context.nationalTeamAgeLimit();
    if (ageLimit < 99 && (player.age <= 0 || player.age > ageLimit))
        return false;
    return true;
}

std::vector<const Player *> NationalCallupPage::eligiblePool(bool excludeInjured) const
{
    std::vector<const Player *> pool;
    for (const Player &player : m_context.store().players()) {
        if (!isEligible(player))
            continue;
        if (excludeInjured && m_injuredUids.contains(player.uid))
            continue;
        pool.push_back(&player);
    }
    return pool;
}

std::vector<const Player *> NationalCallupPage::currentSquad() const
{
    std::vector<const Player *> squad;
    if (m_uploadRadio->isChecked() && m_haveUpload) {
        for (const Player &player : m_context.store().players()) {
            if (m_uploadedUids.contains(player.uid))
                squad.push_back(&player);
        }
    } else {
        for (const Player &player : m_context.store().players()) {
            if (player.inNationalSquad)
                squad.push_back(&player);
        }
    }
    return squad;
}

void NationalCallupPage::rebuildInjuredList()
{
    m_updatingInjured = true;
    m_injuredList->clear();
    const QString query = m_injuredSearch->text().trimmed();

    auto pool = eligiblePool(false);
    std::sort(pool.begin(), pool.end(), [](const Player *a, const Player *b) {
        return getLastName(a->name).localeAwareCompare(getLastName(b->name)) < 0;
    });
    for (const Player *player : pool) {
        if (!query.isEmpty() && !player->name.contains(query, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(playerLine(*player), m_injuredList);
        item->setData(Qt::UserRole, player->uid);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(m_injuredUids.contains(player->uid) ? Qt::Checked : Qt::Unchecked);
    }
    m_injuredCountLabel->setText(
        tr("%1 Spieler als nicht verfügbar markiert.").arg(m_injuredUids.size()));
    m_updatingInjured = false;
}

void NationalCallupPage::uploadSquad()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Nationalkader-Export wählen"), m_context.config().effectiveHtmlExportDir(),
        tr("HTML-Dateien (*.html *.htm);;Alle Dateien (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Nominierung"),
                              tr("Datei konnte nicht geöffnet werden: %1").arg(file.errorString()));
        return;
    }
    const QString html = QString::fromUtf8(file.readAll());

    HtmlTable table;
    QString error;
    if (!HtmlImporter::extractTable(html, &table, &error)) {
        QMessageBox::critical(this, tr("Nominierung"),
                              tr("Kader-Datei konnte nicht gelesen werden: %1").arg(error));
        return;
    }

    const int uidCol = table.headers.indexOf(QStringLiteral("UID"));
    const int nameCol = table.headers.indexOf(QStringLiteral("Name"));
    if (uidCol < 0 && nameCol < 0) {
        QMessageBox::critical(this, tr("Nominierung"),
                              tr("Die Datei enthält weder eine 'UID'- noch eine 'Name'-Spalte."));
        return;
    }

    // Resolve rows to players: prefer the stable UID. Only fall back to name
    // matching when the export carries no UID column — and because two players
    // can share a name (different UIDs), keep ALL players per name and, on a
    // collision, prefer the one who is actually eligible for this national team
    // (a called-up player must be). Genuinely ambiguous names are reported
    // rather than guessed.
    QHash<QString, const Player *> byUid;
    QHash<QString, QList<const Player *>> byName;
    for (const Player &p : m_context.store().players()) {
        byUid.insert(p.uid, &p);
        byName[p.name.trimmed().toLower()].append(&p);
    }
    QSet<QString> matched;
    QStringList unmatched;
    for (const QStringList &row : table.rows) {
        const Player *player = nullptr;
        if (uidCol >= 0 && uidCol < row.size())
            player = byUid.value(row.at(uidCol).trimmed());
        if (!player && nameCol >= 0 && nameCol < row.size()) {
            const QList<const Player *> candidates =
                byName.value(row.at(nameCol).trimmed().toLower());
            if (candidates.size() == 1) {
                player = candidates.first();
            } else if (candidates.size() > 1) {
                // Disambiguate by eligibility; unique eligible match wins.
                const Player *eligibleHit = nullptr;
                int eligibleCount = 0;
                for (const Player *c : candidates) {
                    if (isEligible(*c)) {
                        eligibleHit = c;
                        ++eligibleCount;
                    }
                }
                if (eligibleCount == 1)
                    player = eligibleHit;
            }
        }
        if (player)
            matched.insert(player->uid);
        else if (nameCol >= 0 && nameCol < row.size() && !row.at(nameCol).trimmed().isEmpty())
            unmatched << row.at(nameCol).trimmed();
    }

    if (matched.isEmpty()) {
        QMessageBox::warning(this, tr("Nominierung"),
                             tr("Kein Spieler aus der Datei konnte der Datenbank zugeordnet "
                                "werden. Enthält der Export eine UID-Spalte?"));
        return;
    }

    m_uploadedUids = matched;
    m_haveUpload = true;
    QString message = tr("%1 Spieler aus der Datei als aktueller Kader übernommen.")
                          .arg(matched.size());
    if (!unmatched.isEmpty()) {
        message += QStringLiteral("\n\n")
                   + tr("%1 nicht zugeordnet: %2")
                         .arg(unmatched.size())
                         .arg(unmatched.mid(0, 10).join(QStringLiteral(", ")));
    }
    QMessageBox::information(this, tr("Nominierung"), message);
    refresh();
}

void NationalCallupPage::compute()
{
    const QString tactic = m_tacticCombo->currentText();
    if (tactic.isEmpty()) {
        QMessageBox::warning(this, tr("Nominierung"), tr("Keine Taktik ausgewählt."));
        return;
    }
    if (m_uploadRadio->isChecked() && !m_haveUpload) {
        QMessageBox::warning(this, tr("Nominierung"),
                             tr("Bitte zuerst eine Kader-Datei hochladen oder auf den "
                                "gespeicherten Kader umschalten."));
        return;
    }

    const int total = m_totalSpin->value();
    const int gks = m_gkSpin->value();
    if (gks >= total) {
        QMessageBox::warning(this, tr("Nominierung"),
                             tr("Die Torhüter-Zahl muss kleiner als die Kadergröße sein."));
        return;
    }

    const auto positions = m_context.definitions().tacticRoles().value(tactic);
    const QStringList slotOrder = m_context.definitions().tacticSlotOrder(tactic);

    const auto pool = eligiblePool(true); // injured excluded
    if (pool.empty()) {
        QMessageBox::warning(this, tr("Nominierung"),
                             tr("Kein berechtigter, verfügbarer Spieler im Pool. Prüfe "
                                "Nationalität/Altersgrenze und die Verletzten-Markierungen."));
        return;
    }
    const auto baseline = currentSquad();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const NationalCallup::Recommendation rec = NationalCallup::recommend(
        m_context.squadBuilder(), pool, positions, slotOrder, m_context.ratings(), total, gks,
        baseline);
    QApplication::restoreOverrideCursor();

    // Remember the final squad for "apply".
    m_recommendedUids.clear();
    for (const Player *p : rec.recommended)
        m_recommendedUids << p->uid;

    // Summary.
    QString gkNote;
    if (rec.goalkeepersRecommended < gks) {
        gkNote = QStringLiteral(" ")
                 + tr("(nur %1 Torhüter im Pool verfügbar)").arg(rec.goalkeepersRecommended);
    }
    m_summaryLabel->setText(
        tr("<b>Empfohlener Kader:</b> %1 Spieler (%2 Torhüter%3) — Taktik %4.<br>"
           "<b>%5</b> einladen, <b>%6</b> ausladen.")
            .arg(rec.recommended.size())
            .arg(rec.goalkeepersRecommended)
            .arg(gkNote, tactic.toHtmlEscaped())
            .arg(rec.invites.size())
            .arg(rec.drops.size()));

    // Invite list, best first, with the reason (starting XI / B-team / depth).
    const auto tierLabel = [](const QString &tier) {
        if (tier == QLatin1String("A"))
            return tr("Startelf");
        if (tier == QLatin1String("B"))
            return tr("B-Team");
        return tr("Kadertiefe");
    };
    m_inviteList->clear();
    for (const Player *p : rec.invites) {
        const double rating = SquadBuilder::bestDwrsForPlayer(*p, m_context.ratings());
        auto *item = new QListWidgetItem(
            tr("%1  ·  %2  ·  DWRS %3%")
                .arg(playerLine(*p), tierLabel(rec.tierByUid.value(p->uid)))
                .arg(qRound(rating)),
            m_inviteList);
        item->setData(Qt::UserRole, p->uid);
        item->setData(Qt::UserRole + 1, p->name); // bare name for copying
    }
    if (rec.invites.empty())
        new QListWidgetItem(tr("Keine — der aktuelle Kader ist bereits optimal."), m_inviteList);

    // Drop list with a reason: injured, ineligible/absent from pool, or surplus.
    QSet<QString> eligibleUids;
    for (const Player *p : eligiblePool(false))
        eligibleUids.insert(p->uid);
    m_dropList->clear();
    for (const Player *p : rec.drops) {
        QString reason;
        if (isRetiredClub(*p))
            reason = tr("Retired");
        else if (m_injuredUids.contains(p->uid))
            reason = tr("verletzt/gesperrt");
        else if (!eligibleUids.contains(p->uid))
            reason = tr("nicht verfügbar/berechtigt");
        else
            reason = tr("überzählig");
        auto *item = new QListWidgetItem(
            tr("%1  ·  %2").arg(playerLine(*p), reason), m_dropList);
        item->setData(Qt::UserRole, p->uid);
        item->setData(Qt::UserRole + 1, p->name); // bare name for copying
    }
    if (rec.drops.empty())
        new QListWidgetItem(tr("Keine — kein aktueller Spieler muss weichen."), m_dropList);

    m_applyButton->setEnabled(!m_recommendedUids.isEmpty());
    showResults(true);
}

void NationalCallupPage::applyRecommendation()
{
    if (m_recommendedUids.isEmpty())
        return;
    if (QMessageBox::question(
            this, tr("Nominierung"),
            tr("Den empfohlenen Kader (%1 Spieler) als Nationalkader speichern? "
               "Der bisherige Nationalkader wird ersetzt.")
                .arg(m_recommendedUids.size()))
        != QMessageBox::Yes) {
        return;
    }

    QList<int> ids;
    for (const QString &uid : std::as_const(m_recommendedUids)) {
        if (const Player *player = m_context.store().findByUid(uid))
            ids << player->id;
    }
    if (!m_context.database().setNationalSquadIds(ids)) {
        QMessageBox::critical(this, tr("Nominierung"), m_context.database().errorString());
        return;
    }
    m_context.reloadFromDatabase();
    QMessageBox::information(this, tr("Nominierung"),
                             tr("%1 Spieler im Nationalkader gespeichert.").arg(ids.size()));
    // Baseline is now the applied squad, so re-running shows no pending changes.
    m_savedRadio->setChecked(true);
    refresh();
}

void NationalCallupPage::showResults(bool visible)
{
    m_resultsBox->setVisible(visible);
}

} // namespace fm
