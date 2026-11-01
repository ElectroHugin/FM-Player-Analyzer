#include "StartScreen.h"

#include "AboutDialog.h"
#include "AppContext.h"
#include "MigrationWizard.h"
#include "NewSaveWizard.h"
#include "theming/ThemeManager.h"
#include "widgets/LineIcons.h"
#include "core/Constants.h"
#include "core/Version.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace fm {

namespace {

constexpr int kCrestSize = 40;

QString relativeTime(const QDateTime &when)
{
    if (!when.isValid())
        return {};
    const QDate today = QDate::currentDate();
    const QString time = when.time().toString(QStringLiteral("HH:mm"));
    if (when.date() == today)
        return StartScreen::tr("heute, %1").arg(time);
    if (when.date() == today.addDays(-1))
        return StartScreen::tr("gestern, %1").arg(time);
    return when.date().toString(QStringLiteral("dd.MM.yyyy"));
}

// "vor 2 Std.", "vor 3 Tagen", "vor 3 Wochen" … (German is the source
// language without plural forms, so singular and plural are separate texts).
QString ageText(const QDateTime &when)
{
    const qint64 minutes = qMax<qint64>(1, when.secsTo(QDateTime::currentDateTime()) / 60);
    if (minutes < 60)
        return StartScreen::tr("vor %1 Min.").arg(minutes);
    if (minutes < 24 * 60)
        return StartScreen::tr("vor %1 Std.").arg(minutes / 60);
    const qint64 days = minutes / (24 * 60);
    if (days == 1)
        return StartScreen::tr("vor 1 Tag");
    if (days < 14)
        return StartScreen::tr("vor %1 Tagen").arg(days);
    if (days < 60)
        return StartScreen::tr("vor %1 Wochen").arg(days / 7);
    return StartScreen::tr("am %1").arg(when.date().toString(QStringLiteral("dd.MM.yyyy")));
}

} // namespace

StartScreen::StartScreen(AppContext &context, ThemeManager &theme, Mode mode, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
    , m_theme(theme)
    , m_mode(mode)
{
    setWindowTitle(mode == Mode::Startup ? appName() : tr("Spielstand wechseln"));
    setObjectName(QStringLiteral("startScreen"));
    resize(1040, 640);
    setMinimumSize(820, 520);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildHero());
    layout->addWidget(buildList(), 1);

    reload();
    m_list->setFocus();
}

QWidget *StartScreen::buildHero()
{
    auto *hero = new QFrame(this);
    hero->setObjectName(QStringLiteral("startHero"));
    hero->setFixedWidth(350);
    auto *layout = new QVBoxLayout(hero);
    layout->setContentsMargins(30, 36, 30, 22);
    layout->setSpacing(8);

    auto *logo = new QLabel(hero);
    logo->setPixmap(QApplication::windowIcon().pixmap(72, 72));
    layout->addWidget(logo);
    layout->addSpacing(6);
    auto *title = new QLabel(appName(), hero);
    title->setObjectName(QStringLiteral("startTitle"));
    title->setWordWrap(true);
    layout->addWidget(title);
    QStringList versions;
    for (const FmDataVersion &version : fmDataVersions()) {
        if (version.supported)
            versions.prepend(SaveLibrary::shortVersionLabel(version.id));
    }
    auto *version = new QLabel(tr("Version %1 · %2")
                                   .arg(appVersion(), versions.join(tr(" und "))),
                               hero);
    version->setObjectName(QStringLiteral("kpiCaption"));
    layout->addWidget(version);
    layout->addSpacing(18);

    // Big two-line action: icon, title and a short description underneath.
    const auto addAction = [&](const QString &iconName, const QString &text,
                               const QString &description, bool primary) {
        auto *button = new QPushButton(hero);
        button->setObjectName(primary ? QStringLiteral("heroPrimary")
                                      : QStringLiteral("heroAction"));
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoDefault(false);
        button->setAccessibleName(text);
        auto *row = new QHBoxLayout(button);
        row->setContentsMargins(16, 11, 14, 11);
        row->setSpacing(12);
        auto *icon = new QLabel(button);
        icon->setObjectName(QStringLiteral("heroIcon"));
        icon->setProperty("iconName", iconName);
        icon->setFixedSize(18, 18);
        row->addWidget(icon, 0, Qt::AlignTop);
        auto *texts = new QVBoxLayout;
        texts->setSpacing(1);
        auto *titleLabel = new QLabel(text, button);
        titleLabel->setObjectName(QStringLiteral("heroActionTitle"));
        auto *descLabel = new QLabel(description, button);
        descLabel->setObjectName(QStringLiteral("heroActionText"));
        descLabel->setWordWrap(true);
        texts->addWidget(titleLabel);
        texts->addWidget(descLabel);
        row->addLayout(texts, 1);
        for (QLabel *label : {icon, titleLabel, descLabel})
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        button->setMinimumHeight(64);
        layout->addWidget(button);
        return button;
    };
    connect(addAction(QStringLiteral("plus"), tr("Neuer Spielstand"),
                      tr("FM-Version, erster Import und Verein"), true),
            &QPushButton::clicked, this, &StartScreen::newSave);
    connect(addAction(QStringLiteral("folder"), tr("Spielstand-Datei öffnen …"),
                      tr("Datei aus anderem Ordner oder Backup"), false),
            &QPushButton::clicked, this, &StartScreen::openExternalFile);
    connect(addAction(QStringLiteral("migrate"), tr("FM24-Daten übernehmen …"),
                      tr("Alte Streamlit-Version (Migration)"), false),
            &QPushButton::clicked, this, &StartScreen::migrateLegacy);

    layout->addStretch(1);

    auto *foot = new QHBoxLayout;
    foot->setSpacing(2);
    const auto footButton = [&](const QString &iconName) {
        auto *button = new QToolButton(hero);
        button->setObjectName(QStringLiteral("flatButton"));
        button->setProperty("iconName", iconName);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(15, 15));
        button->setCursor(Qt::PointingHandCursor);
        foot->addWidget(button);
        return button;
    };
    auto *language = footButton(QStringLiteral("globe"));
    language->setPopupMode(QToolButton::InstantPopup);
    auto *languageMenu = new QMenu(language);
    auto *group = new QActionGroup(languageMenu);
    const QString current = m_context.paths().language();
    for (const auto &[code, label] :
         {std::pair{QStringLiteral("en"), QStringLiteral("English")},
          std::pair{QStringLiteral("de"), QStringLiteral("Deutsch")}}) {
        auto *action = languageMenu->addAction(label);
        action->setCheckable(true);
        const bool active =
            code == current || (code == QLatin1String("en") && current.isEmpty());
        action->setChecked(active);
        if (active)
            language->setText(label + QStringLiteral(" ▾"));
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] { changeLanguage(code); });
    }
    language->setMenu(languageMenu);
    language->setToolTip(tr("Sprache"));

    m_themeButton = footButton(QStringLiteral("moon"));
    m_themeButton->setToolTip(tr("Tag/Nacht wechseln"));
    connect(m_themeButton, &QToolButton::clicked, this, [this] { m_theme.toggle(); });

    auto *help = footButton(QStringLiteral("help"));
    help->setText(tr("Hilfe"));
    help->setToolTip(tr("Über %1, Support und Kontakt").arg(appName()));
    connect(help, &QToolButton::clicked, this, [this] {
        AboutDialog dialog(m_theme, this);
        dialog.exec();
    });
    foot->addStretch(1);
    layout->addLayout(foot);

    updateThemedIcons();
    connect(&m_theme, &ThemeManager::themeChanged, this, &StartScreen::updateThemedIcons);
    return hero;
}

void StartScreen::updateThemedIcons()
{
    const bool night = m_theme.mode() == QLatin1String("night");
    if (m_themeButton) {
        m_themeButton->setProperty("iconName", night ? QStringLiteral("moon")
                                                     : QStringLiteral("sun"));
        m_themeButton->setText(night ? tr("Nacht") : tr("Tag"));
    }
    QColor muted = m_theme.text();
    muted.setAlphaF(0.7);
    const qreal dpr = devicePixelRatioF();
    for (QToolButton *button : findChildren<QToolButton *>(QStringLiteral("flatButton")))
        button->setIcon(LineIcons::icon(button->property("iconName").toString(), muted, 15, dpr));
    for (QLabel *icon : findChildren<QLabel *>(QStringLiteral("heroIcon"))) {
        const bool primary = icon->parentWidget()->objectName() == QLatin1String("heroPrimary");
        const QColor color = primary ? QColorConstants::White : m_theme.text();
        icon->setPixmap(LineIcons::icon(icon->property("iconName").toString(), color, 18, dpr)
                            .pixmap(18, 18));
    }
}

QWidget *StartScreen::buildList()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(28, 30, 28, 18);
    layout->setSpacing(14);

    auto *head = new QHBoxLayout;
    auto *heading = new QLabel(tr("Zuletzt verwendet"), panel);
    heading->setObjectName(QStringLiteral("sectionTitle"));
    head->addWidget(heading);
    head->addStretch(1);
    m_search = new QLineEdit(panel);
    m_search->setPlaceholderText(tr("Spielstand suchen"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(240);
    head->addWidget(m_search);
    layout->addLayout(head);
    connect(m_search, &QLineEdit::textChanged, this, &StartScreen::applyFilter);

    m_listStack = new QStackedWidget(panel);
    m_list = new QListWidget(panel);
    m_list->setObjectName(QStringLiteral("saveList"));
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSpacing(3);
    m_listStack->addWidget(m_list);
    m_emptyLabel = new QLabel(panel);
    m_emptyLabel->setObjectName(QStringLiteral("emptyState"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_listStack->addWidget(m_emptyLabel);
    layout->addWidget(m_listStack, 1);

    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) { openSave(item->data(Qt::UserRole).toString()); });
    connect(m_list, &QListWidget::currentItemChanged, this, &StartScreen::updateOpenButton);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (QListWidgetItem *item = m_list->itemAt(pos)) {
            m_list->setCurrentItem(item);
            showSaveMenu(item->data(Qt::UserRole).toString(), m_list->mapToGlobal(pos));
        }
    });

    auto *footer = new QFrame(panel);
    footer->setObjectName(QStringLiteral("startFooter"));
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 12, 0, 0);
    m_autoOpenCheck =
        new QCheckBox(tr("Beim Start immer den zuletzt verwendeten Spielstand öffnen"), footer);
    m_autoOpenCheck->setChecked(m_context.config().autoOpenLastDatabase());
    m_autoOpenCheck->setToolTip(
        tr("Der Startbildschirm wird dann übersprungen. Mit gedrückter Umschalttaste beim "
           "Start erscheint er trotzdem. Abschaltbar in Einstellungen → Datenbank."));
    connect(m_autoOpenCheck, &QCheckBox::toggled, this,
            [this](bool on) { m_context.config().setAutoOpenLastDatabase(on); });
    auto *autoOpenText = new QVBoxLayout;
    autoOpenText->setSpacing(0);
    autoOpenText->addWidget(m_autoOpenCheck);
    auto *autoOpenHint = new QLabel(tr("Jederzeit in den Einstellungen wieder abschaltbar"), footer);
    autoOpenHint->setObjectName(QStringLiteral("kpiCaption"));
    autoOpenHint->setIndent(28);
    autoOpenText->addWidget(autoOpenHint);
    footerLayout->addLayout(autoOpenText);
    footerLayout->addStretch(1);
    if (m_mode == Mode::Switch) {
        auto *close = new QPushButton(tr("Schließen"), footer);
        close->setAutoDefault(false);
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        footerLayout->addWidget(close);
    }
    m_openButton = new QPushButton(tr("Öffnen"), footer);
    m_openButton->setDefault(true);
    connect(m_openButton, &QPushButton::clicked, this, [this] { openSave(selectedName()); });
    footerLayout->addWidget(m_openButton);
    layout->addWidget(footer);
    return panel;
}

QWidget *StartScreen::buildSaveCard(const SaveInfo &save)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("saveCard"));
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 10, 8, 10);
    layout->setSpacing(14);

    auto *crest = new QLabel(card);
    crest->setFixedSize(kCrestSize, kCrestSize);
    crest->setPixmap(SaveLibrary::crest(save, kCrestSize, devicePixelRatioF()));
    layout->addWidget(crest);

    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *nameRow = new QHBoxLayout;
    nameRow->setSpacing(8);
    auto *name = new QLabel(save.name, card);
    name->setObjectName(QStringLiteral("saveName"));
    nameRow->addWidget(name);
    auto *chip = new QLabel(SaveLibrary::shortVersionLabel(save.fmVersionId), card);
    chip->setObjectName(save.fmVersionId == newestFmVersionId() ? QStringLiteral("chipAccent")
                                                                : QStringLiteral("chip"));
    nameRow->addWidget(chip);
    if (m_mode == Mode::Switch && save.name == m_context.currentDbName()) {
        auto *open = new QLabel(tr("geöffnet"), card);
        open->setObjectName(QStringLiteral("chip"));
        nameRow->addWidget(open);
    }
    nameRow->addStretch(1);
    text->addLayout(nameRow);

    QStringList meta;
    meta << (save.clubName.isEmpty() ? tr("Noch kein Verein") : save.clubName);
    if (save.playerCount == 0)
        meta << tr("leer");
    else if (save.playerCount > 0)
        meta << tr("%1 Spieler").arg(QLocale().toString(save.playerCount));
    else
        meta << tr("nicht lesbar");
    if (!save.tactic.isEmpty())
        meta << tr("Taktik %1").arg(save.tactic);
    auto *metaLabel = new QLabel(meta.join(QStringLiteral(" · ")), card);
    metaLabel->setObjectName(QStringLiteral("kpiCaption"));
    text->addWidget(metaLabel);
    layout->addLayout(text, 1);

    const QString opened = relativeTime(save.lastOpened.isValid() ? save.lastOpened
                                                                   : save.modified);
    QString imported;
    if (save.lastImport.isValid())
        imported = tr("Import %1").arg(ageText(save.lastImport));
    else if (save.playerCount == 0)
        imported = tr("noch kein Import");
    const QString when = imported.isEmpty() ? opened : opened + QLatin1Char('\n') + imported;
    auto *whenLabel = new QLabel(when, card);
    whenLabel->setObjectName(QStringLiteral("kpiCaption"));
    whenLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(whenLabel);

    auto *more = new QToolButton(card);
    more->setObjectName(QStringLiteral("cardMenuButton"));
    more->setText(QStringLiteral("⋯"));
    more->setToolTip(tr("Weitere Aktionen"));
    more->setCursor(Qt::PointingHandCursor);
    more->setFocusPolicy(Qt::NoFocus);
    connect(more, &QToolButton::clicked, this, [this, more, saveName = save.name] {
        showSaveMenu(saveName, more->mapToGlobal(QPoint(0, more->height())));
    });
    layout->addWidget(more);

    // Clicks on the labels fall through to the list (selection, double-click).
    for (QLabel *label : card->findChildren<QLabel *>())
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    return card;
}

void StartScreen::reload()
{
    const QString keep = selectedName();
    m_saves = SaveLibrary::scan(m_context);
    m_list->clear();
    for (const SaveInfo &save : std::as_const(m_saves)) {
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, save.name);
        item->setData(Qt::UserRole + 1,
                      QStringLiteral("%1 %2 %3").arg(save.name, save.clubName,
                                                     SaveLibrary::shortVersionLabel(save.fmVersionId)));
        QWidget *card = buildSaveCard(save);
        item->setSizeHint(card->sizeHint());
        m_list->setItemWidget(item, card);
    }
    m_emptyLabel->setText(
        tr("Noch keine Spielstände.\nLege links mit „Neuer Spielstand“ deinen ersten an."));
    m_listStack->setCurrentIndex(m_saves.isEmpty() ? 1 : 0);

    int row = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == keep)
            row = i;
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(row);
    applyFilter();
    updateOpenButton();
}

void StartScreen::applyFilter()
{
    const QString needle = m_search->text().trimmed();
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        item->setHidden(!needle.isEmpty()
                        && !item->data(Qt::UserRole + 1).toString().contains(
                            needle, Qt::CaseInsensitive));
    }
    if (m_list->currentItem() && m_list->currentItem()->isHidden()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (!m_list->item(i)->isHidden()) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
}

void StartScreen::updateOpenButton()
{
    m_openButton->setEnabled(!selectedName().isEmpty());
}

QString StartScreen::selectedName() const
{
    const QListWidgetItem *item = m_list ? m_list->currentItem() : nullptr;
    return item && !item->isHidden() ? item->data(Qt::UserRole).toString() : QString();
}

bool StartScreen::openSave(const QString &name)
{
    if (name.isEmpty())
        return false;
    if (m_context.hasDatabase() && name == m_context.currentDbName()) {
        accept();
        return true;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = m_context.openDatabase(name, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, tr("Spielstand öffnen"),
                              tr("„%1“ konnte nicht geöffnet werden:\n\n%2").arg(name, error));
        return false;
    }
    accept();
    return true;
}

void StartScreen::showSaveMenu(const QString &name, const QPoint &globalPos)
{
    const bool isOpen = m_context.hasDatabase() && name == m_context.currentDbName();
    QMenu menu(this);
    menu.addAction(tr("Öffnen"), this, [this, name] { openSave(name); });
    menu.addSeparator();
    QAction *rename = menu.addAction(tr("Umbenennen …"), this, [this, name] { renameSave(name); });
    rename->setEnabled(!isOpen);
    menu.addAction(tr("Duplizieren …"), this, [this, name] { duplicateSave(name); });
    menu.addAction(tr("Im Ordner zeigen"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_context.paths().databasesDir()));
    });
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Löschen …"), this, [this, name] { deleteSave(name); });
    remove->setEnabled(!isOpen);
    if (isOpen) {
        rename->setToolTip(tr("Der geöffnete Spielstand kann nicht umbenannt werden."));
        remove->setToolTip(tr("Der geöffnete Spielstand kann nicht gelöscht werden."));
        menu.setToolTipsVisible(true);
    }
    menu.exec(globalPos);
}

void StartScreen::newSave()
{
    NewSaveWizard wizard(m_context, this);
    if (wizard.exec() == QDialog::Accepted && !wizard.createdName().isEmpty()) {
        accept(); // the wizard leaves the new save open
        return;
    }
    if (!wizard.createdName().isEmpty())
        accept();
}

void StartScreen::openExternalFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Spielstand-Datei öffnen"), QDir::homePath(),
        tr("Spielstände (*.db);;Alle Dateien (*)"));
    if (path.isEmpty())
        return;
    QString name = QFileInfo(path).completeBaseName();
    while (!SaveLibrary::validateName(m_context, name).isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(
            this, tr("Spielstand-Datei öffnen"),
            tr("%1\nUnter welchem Namen soll der Spielstand übernommen werden?")
                .arg(SaveLibrary::validateName(m_context, name)),
            QLineEdit::Normal, name, &ok);
        if (!ok)
            return;
    }
    QString error;
    if (!SaveLibrary::importFile(m_context, path, name, &error)) {
        QMessageBox::critical(this, tr("Spielstand-Datei öffnen"), error);
        return;
    }
    reload();
    openSave(name);
}

void StartScreen::migrateLegacy()
{
    MigrationWizard wizard(m_context, this);
    wizard.exec();
    reload();
    const QString migrated = wizard.migratedDbName();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == migrated)
            m_list->setCurrentRow(i);
    }
}

void StartScreen::renameSave(const QString &name)
{
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Spielstand umbenennen"),
                                                  tr("Neuer Name:"), QLineEdit::Normal, name, &ok);
    if (!ok || newName.trimmed() == name)
        return;
    QString error;
    if (!SaveLibrary::rename(m_context, name, newName, &error))
        QMessageBox::warning(this, tr("Spielstand umbenennen"), error);
    reload();
}

void StartScreen::duplicateSave(const QString &name)
{
    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, tr("Spielstand duplizieren"), tr("Name der Kopie:"),
                              QLineEdit::Normal, tr("%1 (Kopie)").arg(name), &ok);
    if (!ok)
        return;
    QString error;
    if (!SaveLibrary::duplicate(m_context, name, newName, &error))
        QMessageBox::warning(this, tr("Spielstand duplizieren"), error);
    reload();
}

void StartScreen::deleteSave(const QString &name)
{
    const auto answer = QMessageBox::question(
        this, tr("Spielstand löschen"),
        tr("„%1“ wirklich löschen?\n\nVorher wird automatisch ein Backup im Ordner "
           "„backups“ angelegt. Von dort lässt er sich über „Spielstand-Datei öffnen“ "
           "zurückholen.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!SaveLibrary::remove(m_context, name, &error))
        QMessageBox::warning(this, tr("Spielstand löschen"), error);
    reload();
}

void StartScreen::changeLanguage(const QString &language)
{
    if (language == m_context.paths().language())
        return;
    m_context.paths().setLanguage(language);
    const auto answer = QMessageBox::question(
        this, tr("Sprache"),
        tr("Die Sprache wird nach einem Neustart geändert. Jetzt neu starten?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes)
        relaunchApplication();
}

void StartScreen::relaunchApplication()
{
    QStringList args = QApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst(); // drop the program path itself
    args.removeAll(QStringLiteral("--smoke"));
    QProcess::startDetached(QApplication::applicationFilePath(), args);
    QApplication::exit(0);
    // A modal start screen runs its own loop: leave it, too.
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *dialog = qobject_cast<QDialog *>(widget))
            dialog->reject();
    }
}

void StartScreen::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::New)) {
        newSave();
        return;
    }
    if (event->key() == Qt::Key_Delete && m_list->hasFocus() && !selectedName().isEmpty()) {
        deleteSave(selectedName());
        return;
    }
    QDialog::keyPressEvent(event);
}

} // namespace fm
