#include "MainWindow.h"

#include "AboutDialog.h"
#include "AppContext.h"
#include "NewSaveWizard.h"
#include "SaveLibrary.h"
#include "StartScreen.h"
#include "widgets/DropZone.h"
#include "pages/AssignRolesPage.h"
#include "pages/BestXiPage.h"
#include "pages/DashboardPage.h"
#include "pages/DwrsProgressPage.h"
#include "pages/EditPlayerPage.h"
#include "pages/NationalBestXiPage.h"
#include "pages/NationalCallupPage.h"
#include "pages/RegistrationPage.h"
#include "pages/NationalDashboardPage.h"
#include "pages/NationalSquadMatrixPage.h"
#include "pages/NationalSquadSelectionPage.h"
#include "pages/TrainingPlanPage.h"
#include "pages/NewRolePage.h"
#include "pages/NewTacticPage.h"
#include "pages/PlaceholderPage.h"
#include "pages/PlayerComparisonPage.h"
#include "pages/PlayerProfilePage.h"
#include "pages/RoleAnalysisPage.h"
#include "pages/SettingsPage.h"
#include "pages/SquadMatrixPage.h"
#include "pages/TacticExplorerPage.h"
#include "pages/TransfersPage.h"
#include "theming/ThemeManager.h"
#include "widgets/BusyProgressDialog.h"
#include "widgets/CommandPalette.h"
#include "widgets/LineIcons.h"
#include "core/Utils.h"
#include "core/Database.h"
#include "core/Version.h"

#include <QAction>
#include <QTimer>
#include <QActionGroup>
#include <QButtonGroup>
#include <QSet>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QIcon>
#include <QLocale>
#include <QMimeData>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QProcess>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace fm {

MainWindow::MainWindow(AppContext &context, ThemeManager &theme, QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
    , m_theme(theme)
{
    setWindowTitle(appName());
    resize(1600, 950);
    setMinimumSize(1100, 700);
    setAcceptDrops(true); // drop an FM export anywhere to import it
    if (!m_context.paths().windowGeometry().isEmpty())
        restoreGeometry(m_context.paths().windowGeometry());

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    buildMenuBar();

    buildSidebar();
    rootLayout->addWidget(findChild<QFrame *>(QStringLiteral("sidebar")));

    // Header bar (club/national identity + active save) above the pages —
    // the port of legacy display_custom_header.
    auto *contentColumn = new QVBoxLayout;
    contentColumn->setContentsMargins(0, 0, 0, 0);
    contentColumn->setSpacing(0);
    auto *headerBar = new QFrame(central);
    headerBar->setObjectName(QStringLiteral("headerBar"));
    auto *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(18, 8, 18, 8);
    headerLayout->setSpacing(12);

    // Browser-style navigation, present above every page: Back / Forward / Home.
    auto *navLayout = new QHBoxLayout;
    navLayout->setSpacing(4);
    const auto makeNavButton = [&](const QString &glyph, const QString &toolTip,
                                   void (MainWindow::*slot)()) {
        auto *button = new QToolButton(headerBar);
        button->setObjectName(QStringLiteral("navButton"));
        button->setText(glyph);
        button->setToolTip(toolTip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus); // never steal focus from page inputs
        connect(button, &QToolButton::clicked, this, slot);
        navLayout->addWidget(button);
        return button;
    };
    m_backButton = makeNavButton(QStringLiteral("←"), tr("Zurück (Alt+←)"), &MainWindow::goBack);
    m_forwardButton =
        makeNavButton(QStringLiteral("→"), tr("Vorwärts (Alt+→)"), &MainWindow::goForward);
    m_homeButton =
        makeNavButton(QStringLiteral("⌂"), tr("Startseite (Alt+Pos1)"), &MainWindow::goHome);
    headerLayout->addLayout(navLayout);
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this, this, &MainWindow::goBack);
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this, this, &MainWindow::goForward);
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Home), this, this, &MainWindow::goHome);
    qApp->installEventFilter(this);

    m_headerLogo = new QLabel(headerBar);
    m_headerLogo->setFixedHeight(34);
    m_headerLogo->hide();
    m_headerIdentity = new QLabel(headerBar);
    m_breadcrumb = new QLabel(headerBar);
    m_breadcrumb->setObjectName(QStringLiteral("breadcrumb"));
    headerLayout->addWidget(m_headerLogo);
    headerLayout->addWidget(m_headerIdentity);
    headerLayout->addWidget(m_breadcrumb, 1);

    // Ctrl+K: search players, pages and actions in one field.
    m_paletteButton = new QPushButton(headerBar);
    m_paletteButton->setObjectName(QStringLiteral("paletteButton"));
    m_paletteButton->setText(tr("Spieler, Seiten, Aktionen …    Strg+K"));
    m_paletteButton->setCursor(Qt::PointingHandCursor);
    m_paletteButton->setFocusPolicy(Qt::NoFocus);
    connect(m_paletteButton, &QPushButton::clicked, this, &MainWindow::openPalette);
    headerLayout->addWidget(m_paletteButton);
    auto *paletteShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(paletteShortcut, &QShortcut::activated, this, &MainWindow::openPalette);
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, &MainWindow::openPalette);

    m_importButton = new QPushButton(tr("Importieren"), headerBar);
    m_importButton->setObjectName(QStringLiteral("headerPrimary"));
    m_importButton->setToolTip(tr("FM-Export importieren (Strg+I). Du kannst eine Datei auch "
                                  "einfach ins Fenster ziehen."));
    m_importButton->setCursor(Qt::PointingHandCursor);
    m_importButton->setFocusPolicy(Qt::NoFocus);
    connect(m_importButton, &QPushButton::clicked, this, [this] { importExport({}); });
    headerLayout->addWidget(m_importButton);
    contentColumn->addWidget(headerBar);

    // Tabs of the active sidebar group (Navigation): one per page of the group.
    m_tabStrip = new QFrame(central);
    m_tabStrip->setObjectName(QStringLiteral("pageTabStrip"));
    auto *tabLayout = new QHBoxLayout(m_tabStrip);
    tabLayout->setContentsMargins(18, 6, 18, 0);
    m_tabBar = new QTabBar(m_tabStrip);
    m_tabBar->setExpanding(false);
    m_tabBar->setDrawBase(false);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setCursor(Qt::PointingHandCursor);
    m_tabBar->setFocusPolicy(Qt::NoFocus);
    tabLayout->addWidget(m_tabBar);
    tabLayout->addStretch(1);
    m_tabStrip->setVisible(false);
    contentColumn->addWidget(m_tabStrip);
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_updatingTabs && index >= 0)
            navigateTo(m_tabBar->tabData(index).toString());
    });

    // Non-blocking hints (e.g. outdated ratings) instead of pop-ups.
    m_noticeBar = new QFrame(central);
    m_noticeBar->setObjectName(QStringLiteral("noticeBar"));
    auto *noticeLayout = new QHBoxLayout(m_noticeBar);
    noticeLayout->setContentsMargins(14, 8, 10, 8);
    noticeLayout->setSpacing(10);
    m_noticeText = new QLabel(m_noticeBar);
    m_noticeText->setWordWrap(true);
    noticeLayout->addWidget(m_noticeText, 1);
    m_noticeLater = new QPushButton(tr("Später"), m_noticeBar);
    m_noticeAction = new QPushButton(m_noticeBar);
    m_noticeAction->setObjectName(QStringLiteral("headerPrimary"));
    noticeLayout->addWidget(m_noticeLater);
    noticeLayout->addWidget(m_noticeAction);
    connect(m_noticeLater, &QPushButton::clicked, m_noticeBar, &QWidget::hide);
    connect(m_noticeAction, &QPushButton::clicked, this, [this] {
        m_noticeBar->hide();
        if (m_noticeCallback)
            m_noticeCallback();
    });
    m_noticeBar->hide();
    auto *noticeWrap = new QHBoxLayout;
    noticeWrap->setContentsMargins(18, 10, 18, 0);
    noticeWrap->addWidget(m_noticeBar);
    contentColumn->addLayout(noticeWrap);

    m_stack = new QStackedWidget(central);
    contentColumn->addWidget(m_stack, 1);

    m_toast = new QLabel(central);
    m_toast->setObjectName(QStringLiteral("toast"));
    m_toast->setWordWrap(true);
    m_toast->setMaximumWidth(460);
    m_toast->hide();
    m_toastTimer.setSingleShot(true);
    connect(&m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);

    m_palette = new CommandPalette(this);
    rootLayout->addLayout(contentColumn, 1);
    setCentralWidget(central);

    updateDbLabel();
    updateHeader();

    connect(&m_context, &AppContext::navigationRequested, this, &MainWindow::navigateTo);
    connect(&m_context, &AppContext::databaseChanged, this, [this](const QString &) {
        updateDbLabel();
        updateHeader();
        m_searchModelDirty = true;
        m_noticeBar->hide(); // hints belong to the previous save
        for (PageBase *page : std::as_const(m_pages))
            page->refresh();
        // Another database: earlier steps (e.g. a player's profile) may not
        // exist here, so the history starts over.
        resetHistory();
        offerRecalcIfOutdated();
    });
    connect(&m_context, &AppContext::dataChanged, this, [this] {
        m_searchModelDirty = true;
        updateDbLabel();
        updateHeader();
        if (m_context.registrationSettings().active() != m_registrationInMenu)
            refreshMenuKeepingPage();
        // The store was just replaced; drop stale Player* held by hidden pages
        // before anything can access them (they rebuild on next activation).
        auto *current = qobject_cast<PageBase *>(m_stack->currentWidget());
        for (PageBase *page : std::as_const(m_pages)) {
            if (page != current)
                page->releaseStoreRows();
        }
        if (current)
            current->refresh();
    });
    connect(&m_recalcWatcher, &QFutureWatcher<RatingsUpdater::Result>::finished, this, [this] {
        const RatingsUpdater::Result result = m_recalcWatcher.result();
        if (m_recalcDialog) {
            m_recalcDialog->finish();
            m_recalcDialog = nullptr;
        }
        if (!result.success) {
            QMessageBox::critical(this, tr("Neuberechnung"), result.error);
            return;
        }
        showToast(tr("DWRS neu berechnet: %1 Bewertungen, %2 geänderte Einträge gespeichert.")
                      .arg(result.computed)
                      .arg(result.inserted));
        // Every stored rating now follows the current formula.
        m_context.database().setSetting(QStringLiteral("dwrs_scheme"),
                                        QString::number(kDwrsSchemeVersion));
        // A recalc only writes ratings; the players are unchanged.
        m_context.reloadRatings();
    });

    navigateTo(QStringLiteral("dashboard"));
    // After the window is up, so the question appears on top of it.
    QTimer::singleShot(0, this, &MainWindow::offerRecalcIfOutdated);
}

void MainWindow::offerRecalcIfOutdated()
{
    Database &db = m_context.database();
    // Databases without the setting predate it: their ratings are scheme 1.
    if (db.setting(QStringLiteral("dwrs_scheme"), QStringLiteral("1")).toInt()
        >= kDwrsSchemeVersion)
        return;
    if (m_context.store().size() == 0) {
        // Nothing stored yet: every future rating uses the current formula.
        db.setSetting(QStringLiteral("dwrs_scheme"), QString::number(kDwrsSchemeVersion));
        return;
    }
    showNotice(tr("Die DWRS-Berechnung wurde verbessert (Torhüter werden nach allen "
                  "Torwart-Attributen bewertet). Die Bewertungen dieses Spielstands stammen "
                  "noch aus der alten Berechnung."),
               tr("Jetzt neu berechnen"), [this] { startDwrsRecalc(); });
}

void MainWindow::buildSidebar()
{
    auto *sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(256);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(10, 14, 10, 12);
    layout->setSpacing(8);

    // The open save, click for recent saves / all saves / a new one.
    m_saveSwitcher = new QToolButton(sidebar);
    m_saveSwitcher->setObjectName(QStringLiteral("saveSwitcher"));
    m_saveSwitcher->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_saveSwitcher->setIconSize(QSize(32, 32));
    m_saveSwitcher->setPopupMode(QToolButton::InstantPopup);
    m_saveSwitcher->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_saveSwitcher->setCursor(Qt::PointingHandCursor);
    m_saveSwitcher->setToolTip(tr("Spielstand wechseln (Strg+O)"));
    auto *saveMenu = new QMenu(m_saveSwitcher);
    connect(saveMenu, &QMenu::aboutToShow, this, [this, saveMenu] { fillSaveMenu(saveMenu); });
    m_saveSwitcher->setMenu(saveMenu);
    layout->addWidget(m_saveSwitcher);
    layout->addSpacing(2);

    // Club / national management as a two-way switch.
    auto *segmented = new QFrame(sidebar);
    segmented->setObjectName(QStringLiteral("segmented"));
    auto *segLayout = new QHBoxLayout(segmented);
    segLayout->setContentsMargins(3, 3, 3, 3);
    segLayout->setSpacing(3);
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    for (const auto &[id, label] : {std::pair{0, tr("Verein")}, std::pair{1, tr("Nationalteam")}}) {
        auto *button = new QPushButton(label, segmented);
        button->setObjectName(QStringLiteral("segmentButton"));
        button->setCheckable(true);
        button->setChecked(id == 0);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        m_modeGroup->addButton(button, id);
        segLayout->addWidget(button, 1);
    }
    layout->addWidget(segmented);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if ((id == 1) == m_context.nationalUiMode())
            return;
        // Pages shared between both modes (Taktik-Explorer, Profil, …) scope
        // their pools by this flag; the menu decides which pages are shown.
        m_context.setNationalUiMode(id == 1);
        rebuildMenu(); // navigates to the mode's first page (a history step)
        updateHeader();
    });

    m_menu = new QListWidget(sidebar);
    m_menu->setCursor(Qt::PointingHandCursor);
    m_menu->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_menu->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_menu->setIconSize(QSize(18, 18));
    layout->addWidget(m_menu, 1);
    connect(m_menu, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
                // m_stack does not exist yet while the constructor builds the
                // sidebar (rebuildMenu selects the first row).
                if (!current || !m_stack)
                    return;
                const QString groupId = current->data(Qt::UserRole).toString();
                if (!groupId.isEmpty())
                    navigateToGroup(groupId);
            });

    auto *foot = new QHBoxLayout;
    foot->setSpacing(4);
    auto *themeButton = new QToolButton(sidebar);
    themeButton->setObjectName(QStringLiteral("sidebarIconButton"));
    themeButton->setToolTip(tr("Tag/Nacht wechseln"));
    themeButton->setCursor(Qt::PointingHandCursor);
    themeButton->setFocusPolicy(Qt::NoFocus);
    themeButton->setIconSize(QSize(18, 18));
    connect(themeButton, &QToolButton::clicked, this, [this] { m_theme.toggle(); });
    foot->addWidget(themeButton);

    // Quiet author/version credit at the foot of the sidebar; opens Help → About.
    auto *credit = new QToolButton(sidebar);
    credit->setObjectName(QStringLiteral("creditButton"));
    credit->setText(tr("v%1 · ElectroHugin").arg(appVersion()));
    credit->setToolTip(tr("Über %1").arg(appName()));
    credit->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    credit->setIconSize(QSize(16, 16));
    credit->setCursor(Qt::PointingHandCursor);
    credit->setFocusPolicy(Qt::NoFocus);
    const auto tintCredit = [this, credit] {
        QColor muted = m_theme.text();
        muted.setAlphaF(0.60);
        credit->setIcon(AboutDialog::brandMark(muted, 16, devicePixelRatioF()));
    };
    const auto tintIcons = [this, themeButton, tintCredit] {
        tintCredit();
        QColor muted = m_theme.text();
        muted.setAlphaF(0.70);
        themeButton->setIcon(LineIcons::icon(QStringLiteral("moon"), muted, 18,
                                             devicePixelRatioF()));
        updateMenuIcons();
    };
    tintIcons();
    connect(&m_theme, &ThemeManager::themeChanged, this, tintIcons);
    connect(credit, &QToolButton::clicked, this, &MainWindow::showAbout);
    foot->addStretch(1);
    foot->addWidget(credit);
    layout->addLayout(foot);

    rebuildMenu();
}

void MainWindow::rebuildMenu(bool selectFirst)
{
    const bool national = m_context.nationalUiMode();

    m_menu->blockSignals(true);
    m_menu->clear();

    const auto addSection = [this](const QString &heading) {
        auto *item = new QListWidgetItem(heading, m_menu);
        item->setFlags(Qt::NoItemFlags);
        QFont font = item->font();
        font.setBold(true);
        font.setPointSizeF(font.pointSizeF() * 0.8);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        item->setFont(font);
    };
    // The registration page only exists while registration rules are active.
    m_registrationInMenu = m_context.registrationSettings().active();
    m_sidebar = Navigation::sidebar(national, m_registrationInMenu);
    for (const Navigation::Section &section : std::as_const(m_sidebar)) {
        addSection(section.heading);
        for (const Navigation::Group &group : section.groups) {
            auto *item = new QListWidgetItem(group.label, m_menu);
            item->setData(Qt::UserRole, group.id);
            item->setData(Qt::UserRole + 1, LineIcons::forGroup(group.id));
        }
    }

    updateMenuIcons();
    m_menu->blockSignals(false);
    if (selectFirst)
        m_menu->setCurrentRow(1); // first real entry below the section header
}

void MainWindow::buildMenuBar()
{
    // Language selection ("optional im Menü auf Deutsch") and Help → About.
    // Switching is restart-based: the whole UI (including statically initialized
    // navigation labels) is built once with the translator already installed.
    auto *fileMenu = menuBar()->addMenu(tr("&Datei"));
    auto *newSaveAction = fileMenu->addAction(tr("Neuer Spielstand …"), this, &MainWindow::newSave);
    newSaveAction->setShortcut(QKeySequence::New);
    auto *switchAction =
        fileMenu->addAction(tr("Spielstand wechseln …"), this, &MainWindow::switchSave);
    switchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    fileMenu->addSeparator();
    auto *importAction =
        fileMenu->addAction(tr("Export importieren …"), this, [this] { importExport({}); });
    importAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("Beenden"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);

    auto *languageMenu = menuBar()->addMenu(tr("&Sprache"));
    auto *group = new QActionGroup(this);
    group->setExclusive(true);

    const QString current = m_context.paths().language();
    struct LangOption {
        QString code;
        QString label; // endonym — shown the same regardless of UI language
    };
    for (const LangOption &option :
         {LangOption{QStringLiteral("en"), QStringLiteral("English")},
          LangOption{QStringLiteral("de"), QStringLiteral("Deutsch")}}) {
        auto *action = languageMenu->addAction(option.label);
        action->setCheckable(true);
        action->setChecked(option.code == current
                           || (option.code == QLatin1String("en") && current.isEmpty()));
        group->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, code = option.code] { changeLanguage(code); });
    }

    auto *helpMenu = menuBar()->addMenu(tr("&Hilfe"));
    auto *about = helpMenu->addAction(tr("Über %1 …").arg(appName()));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout()
{
    AboutDialog dialog(m_theme, this);
    dialog.exec();
}

void MainWindow::changeLanguage(const QString &language)
{
    if (language == m_context.paths().language())
        return;
    m_context.paths().setLanguage(language);

    const auto answer = QMessageBox::question(
        this, tr("Sprache"),
        tr("Die Sprache wird nach einem Neustart geändert. Jetzt neu starten?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    // The window geometry is persisted in closeEvent so the new instance restores it.
    StartScreen::relaunchApplication();
}

PageBase *MainWindow::createPage(const QString &pageId)
{
    if (pageId == QLatin1String("dashboard"))
        return new DashboardPage(m_context, m_theme, this);
    if (pageId == QLatin1String("assign_roles"))
        return new AssignRolesPage(m_context, this);
    if (pageId == QLatin1String("role_analysis"))
        return new RoleAnalysisPage(m_context, this);
    if (pageId == QLatin1String("squad_matrix"))
        return new SquadMatrixPage(m_context, this);
    if (pageId == QLatin1String("transfers"))
        return new TransfersPage(m_context, this);
    if (pageId == QLatin1String("player_profile"))
        return new PlayerProfilePage(m_context, m_theme, this);
    if (pageId == QLatin1String("player_comparison"))
        return new PlayerComparisonPage(m_context, m_theme, this);
    if (pageId == QLatin1String("dwrs_development"))
        return new DwrsProgressPage(m_context, m_theme, this);
    if (pageId == QLatin1String("edit_player"))
        return new EditPlayerPage(m_context, this);
    if (pageId == QLatin1String("best_xi"))
        return new BestXiPage(m_context, m_theme, this);
    if (pageId == QLatin1String("tactic_explorer"))
        return new TacticExplorerPage(m_context, this);
    if (pageId == QLatin1String("national_dashboard"))
        return new NationalDashboardPage(m_context, m_theme, this);
    if (pageId == QLatin1String("national_squad_selection"))
        return new NationalSquadSelectionPage(m_context, this);
    if (pageId == QLatin1String("national_callup"))
        return new NationalCallupPage(m_context, this);
    if (pageId == QLatin1String("training_plan"))
        return new TrainingPlanPage(m_context, this);
    if (pageId == QLatin1String("registration"))
        return new RegistrationPage(m_context, this);
    if (pageId == QLatin1String("national_squad_matrix"))
        return new NationalSquadMatrixPage(m_context, this);
    if (pageId == QLatin1String("national_best_xi"))
        return new NationalBestXiPage(m_context, m_theme, this);
    if (pageId == QLatin1String("new_role"))
        return new NewRolePage(m_context, this);
    if (pageId == QLatin1String("new_tactic"))
        return new NewTacticPage(m_context, this);
    if (pageId == QLatin1String("settings")) {
        auto *page = new SettingsPage(m_context, m_theme, this);
        connect(page, &SettingsPage::recalcRequested, this, &MainWindow::startDwrsRecalc);
        return page;
    }

    // Defensive guard: every menu page above is wired, so this is only reached
    // for an unknown/misspelled page id. Show a neutral placeholder instead of
    // returning nullptr (which navigateTo would dereference).
    QString label = pageId;
    if (const Navigation::Group *group = Navigation::findGroup(m_sidebar, pageId))
        label = group->label;
    return new PlaceholderPage(m_context, label, this);
}

void MainWindow::navigateTo(const QString &pageId)
{
    // Remember what the page being left shows (e.g. the profile's player) so
    // Back returns to exactly that — unless the history itself is navigating.
    if (!m_restoringHistory)
        captureHistoryState();

    const QString hostId = Navigation::hostPage(pageId);
    PageBase *page = m_pages.value(hostId);
    if (!page) {
        page = createPage(hostId);
        m_pages.insert(hostId, page);
        m_stack->addWidget(page);
    }
    page->showSection(pageId);
    m_stack->setCurrentWidget(page);
    m_currentPageId = pageId;
    if (const Navigation::Group *group = Navigation::findGroup(m_sidebar, pageId))
        m_groupTabs.insert(group->id, pageId);

    // Keep sidebar and tabs in sync (e.g. jump from the player search).
    selectMenuEntry(pageId);
    updateTabBar(pageId);
    updateBreadcrumb(pageId);

    page->refresh();

    if (!m_restoringHistory)
        pushHistory(pageId, page);
    updateNavButtons();
}

void MainWindow::selectMenuEntry(const QString &pageId)
{
    const Navigation::Group *group = Navigation::findGroup(m_sidebar, pageId);
    const QString groupId = group ? group->id : QString();
    if (m_menu->currentItem() && m_menu->currentItem()->data(Qt::UserRole).toString() == groupId)
        return;
    int row = -1; // a page outside this mode's sidebar highlights nothing
    for (int i = 0; i < m_menu->count() && group; ++i) {
        if (m_menu->item(i)->data(Qt::UserRole).toString() == groupId) {
            row = i;
            break;
        }
    }
    m_menu->blockSignals(true);
    m_menu->setCurrentRow(row);
    m_menu->blockSignals(false);
}

void MainWindow::updateTabBar(const QString &pageId)
{
    const Navigation::Group *group = Navigation::findGroup(m_sidebar, pageId);
    const bool tabbed = group && group->tabs.size() > 1;
    m_updatingTabs = true;
    while (m_tabBar->count() > 0)
        m_tabBar->removeTab(0);
    if (tabbed) {
        for (const Navigation::Tab &tab : group->tabs) {
            // "&" would mark a shortcut letter ("Startelf & B-Team").
            QString label = tab.label;
            const int index = m_tabBar->addTab(label.replace(QLatin1Char('&'), QStringLiteral("&&")));
            m_tabBar->setTabData(index, tab.pageId);
            if (tab.pageId == pageId)
                m_tabBar->setCurrentIndex(index);
        }
    }
    m_updatingTabs = false;
    m_tabStrip->setVisible(tabbed);
}

void MainWindow::navigateToGroup(const QString &groupId)
{
    for (const Navigation::Section &section : std::as_const(m_sidebar)) {
        for (const Navigation::Group &group : section.groups) {
            if (group.id != groupId || group.tabs.isEmpty())
                continue;
            const QString last = m_groupTabs.value(groupId);
            for (const Navigation::Tab &tab : group.tabs) {
                if (tab.pageId == last) {
                    navigateTo(last);
                    return;
                }
            }
            navigateTo(group.tabs.first().pageId);
            return;
        }
    }
}

void MainWindow::refreshMenuKeepingPage()
{
    rebuildMenu(/*selectFirst*/ false);
    selectMenuEntry(m_currentPageId);
    updateTabBar(m_currentPageId);
}

void MainWindow::captureHistoryState()
{
    if (m_historyIndex < 0 || m_historyIndex >= m_history.size())
        return;
    HistoryEntry &entry = m_history[m_historyIndex];
    auto *current = qobject_cast<PageBase *>(m_stack->currentWidget());
    if (current && entry.pageId == m_currentPageId)
        entry.state = current->historyState();
}

void MainWindow::pushHistory(const QString &pageId, PageBase *page)
{
    const HistoryEntry entry{pageId, m_context.nationalUiMode(), page->historyState()};
    if (m_historyIndex >= 0) {
        const HistoryEntry &current = m_history[m_historyIndex];
        if (current.pageId == entry.pageId && current.national == entry.national
            && current.state == entry.state) {
            return; // same place again (e.g. the active menu entry re-clicked)
        }
    }
    // A new step discards the forward branch, like a browser.
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(entry);
    constexpr int kMaxHistory = 100;
    while (m_history.size() > kMaxHistory)
        m_history.removeFirst();
    m_historyIndex = static_cast<int>(m_history.size()) - 1;
}

void MainWindow::resetHistory()
{
    m_history.clear();
    m_historyIndex = -1;
    if (auto *current = qobject_cast<PageBase *>(m_stack->currentWidget()))
        pushHistory(m_currentPageId, current);
    updateNavButtons();
}

void MainWindow::goToHistory(int index)
{
    if (index < 0 || index >= m_history.size() || index == m_historyIndex)
        return;
    captureHistoryState(); // keep the state of the step being left
    const HistoryEntry entry = m_history[index];
    m_historyIndex = index;

    m_restoringHistory = true;
    // The step may belong to the other management mode: switch the sidebar
    // without letting the mode switch navigate on its own.
    if (entry.national != m_context.nationalUiMode()) {
        m_modeGroup->button(entry.national ? 1 : 0)->setChecked(true);
        m_context.setNationalUiMode(entry.national);
        rebuildMenu(/*selectFirst*/ false);
        updateHeader();
    }
    if (PageBase *page = m_pages.value(Navigation::hostPage(entry.pageId)))
        page->restoreHistoryState(entry.state);
    navigateTo(entry.pageId);
    m_restoringHistory = false;
    updateNavButtons();
}

void MainWindow::goBack()
{
    goToHistory(m_historyIndex - 1);
}

void MainWindow::goForward()
{
    goToHistory(m_historyIndex + 1);
}

void MainWindow::goHome()
{
    navigateTo(m_context.nationalUiMode() ? QStringLiteral("national_dashboard")
                                          : QStringLiteral("dashboard"));
}

void MainWindow::updateNavButtons()
{
    if (!m_backButton)
        return;
    m_backButton->setEnabled(m_historyIndex > 0);
    m_forwardButton->setEnabled(m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Mouse side buttons (back/forward) like in a browser — anywhere inside
    // this window, but never while a modal dialog is up.
    if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()
        && !QApplication::activeModalWidget()) {
        auto *widget = static_cast<QWidget *>(watched);
        const auto button = static_cast<QMouseEvent *>(event)->button();
        if (widget->window() == this
            && (button == Qt::BackButton || button == Qt::ForwardButton)) {
            if (button == Qt::BackButton)
                goBack();
            else
                goForward();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::startDwrsRecalc()
{
    // Never stack a full recalc on top of another background job (import,
    // partial recalc) — both would append to the same rating tables.
    if (m_recalcWatcher.isRunning() || m_context.hasRunningBackgroundTask())
        return;

    m_recalcDialog = new BusyProgressDialog(tr("DWRS-Bewertungen werden neu berechnet…"), this);

    // Snapshot everything the worker needs; it opens its own DB connection.
    const QString dbFile = m_context.database().filePath();
    std::vector<Player> players = m_context.store().players();
    const QStringList validRoles = m_context.definitions().validRoles();
    // The engine reads Definitions/AppConfig only during reloadConfig(), which
    // ran on the UI thread; calculateRole afterwards touches cached plans only.
    const DwrsEngine *engine = &m_context.dwrsEngine();

    const QFuture<RatingsUpdater::Result> future = QtConcurrent::run(
        [dbFile, players = std::move(players), engine, validRoles,
         dialogGuard = QPointer<BusyProgressDialog>(m_recalcDialog)] {
            Database db(Database::uniqueConnectionName(QStringLiteral("recalc_worker")));
            if (!db.open(dbFile)) {
                RatingsUpdater::Result result;
                result.error = db.errorString();
                return result;
            }
            return RatingsUpdater::updateDwrsRatings(
                db, players, *engine, validRoles, {},
                [dialogGuard](int current, int total) {
                    const int percent = total > 0 ? current * 100 / total : 0;
                    QMetaObject::invokeMethod(
                        qApp,
                        [dialogGuard, percent] {
                            if (dialogGuard)
                                dialogGuard->setValue(percent);
                        },
                        Qt::QueuedConnection);
                },
                RatingsOrigin::Recalc);
        });
    m_context.registerBackgroundTask(QFuture<void>(future));
    m_recalcWatcher.setFuture(future);
}

void MainWindow::updateHeader()
{
    // Optional header image (user-provided PNG, per database): the national
    // flag while in national mode, otherwise the club logo.
    const QString imagePath =
        m_context.nationalUiMode()
            ? m_context.database().setting(QStringLiteral("national_flag_path"))
            : m_context.database().setting(QStringLiteral("club_logo_path"));
    QPixmap headerImage;
    if (!imagePath.isEmpty() && headerImage.load(imagePath)) {
        m_headerLogo->setPixmap(headerImage.scaledToHeight(34, Qt::SmoothTransformation));
        m_headerLogo->show();
    } else {
        m_headerLogo->clear();
        m_headerLogo->hide();
    }

    QString identity;
    if (m_context.nationalUiMode()) {
        const QString name = m_context.nationalTeamName();
        const QString code = m_context.nationalTeamCode();
        identity = QStringLiteral("<b style='font-size:13pt;'>%1</b>")
                       .arg((name.isEmpty() ? tr("Nationalteam") : name).toHtmlEscaped());
        if (!code.isEmpty())
            identity += QStringLiteral(" &nbsp;<span>(%1)</span>").arg(code.toHtmlEscaped());
    } else {
        const QString fullName =
            m_context.database().setting(QStringLiteral("full_club_name"));
        const QString stadium = m_context.database().setting(QStringLiteral("stadium_name"));
        const QString display = !fullName.isEmpty()
                                    ? fullName
                                    : (!m_context.userClub().isEmpty() ? m_context.userClub()
                                                                       : tr("FM Dashboard"));
        identity = QStringLiteral("<b style='font-size:13pt;'>%1</b>")
                       .arg(display.toHtmlEscaped());
        if (!stadium.isEmpty())
            m_headerIdentity->setToolTip(stadium);
    }
    m_headerIdentity->setText(identity);
}

void MainWindow::updateMenuIcons()
{
    if (!m_menu)
        return;
    QColor color = m_theme.text();
    color.setAlphaF(0.80);
    for (int i = 0; i < m_menu->count(); ++i) {
        QListWidgetItem *item = m_menu->item(i);
        const QString name = item->data(Qt::UserRole + 1).toString();
        if (!name.isEmpty())
            item->setIcon(LineIcons::icon(name, color, 18, devicePixelRatioF()));
    }
}

void MainWindow::updateBreadcrumb(const QString &pageId)
{
    QString text;
    if (const Navigation::Group *group = Navigation::findGroup(m_sidebar, pageId)) {
        text = group->label;
        if (group->tabs.size() > 1) {
            for (const Navigation::Tab &tab : group->tabs) {
                if (tab.pageId == pageId)
                    text += QStringLiteral("  ›  ") + tab.label;
            }
        }
    }
    m_breadcrumb->setText(text.isEmpty() ? QString() : QStringLiteral("›  ") + text);
}

void MainWindow::showNotice(const QString &text, const QString &actionLabel,
                            std::function<void()> action)
{
    m_noticeText->setText(text);
    m_noticeAction->setText(actionLabel);
    m_noticeAction->setVisible(!actionLabel.isEmpty());
    m_noticeCallback = std::move(action);
    m_noticeBar->show();
}

void MainWindow::showToast(const QString &text)
{
    m_toast->setText(text);
    m_toast->adjustSize();
    const QWidget *area = centralWidget();
    m_toast->move(area->width() - m_toast->width() - 24, area->height() - m_toast->height() - 24);
    m_toast->raise();
    m_toast->show();
    m_toastTimer.start(5000);
}

void MainWindow::rebuildSearchModel()
{
    const auto &players = m_context.store().players();
    m_searchKeys.clear();
    m_searchKeys.reserve(players.size());
    for (const Player &player : players)
        m_searchKeys.append(foldForSearch(player.name + QLatin1Char(' ') + player.club));
    m_searchModelDirty = false;
}

void MainWindow::openPalette()
{
    if (QApplication::activeModalWidget())
        return;
    QList<CommandPalette::Entry> entries;
    QColor color = m_theme.text();
    color.setAlphaF(0.75);
    const qreal dpr = devicePixelRatioF();

    // Pages of both modes; switching mode where needed.
    const bool registration = m_context.registrationSettings().active();
    QSet<QString> seen;
    for (const bool national : {m_context.nationalUiMode(), !m_context.nationalUiMode()}) {
        for (const Navigation::Section &section : Navigation::sidebar(national, registration)) {
            for (const Navigation::Group &group : section.groups) {
                for (const Navigation::Tab &tab : group.tabs) {
                    if (seen.contains(tab.pageId))
                        continue;
                    seen.insert(tab.pageId);
                    const QString title =
                        group.tabs.size() > 1 ? tab.label : group.label;
                    QString detail = group.tabs.size() > 1 ? group.label : QString();
                    if (national != m_context.nationalUiMode())
                        detail = national ? tr("Nationalteam") : tr("Verein");
                    entries.append({tr("Seiten"),
                                    LineIcons::icon(LineIcons::forGroup(group.id), color, 18, dpr),
                                    title, detail,
                                    foldForSearch(title + QLatin1Char(' ') + group.label),
                                    [this, national, pageId = tab.pageId] {
                                        if (national != m_context.nationalUiMode()) {
                                            m_modeGroup->button(national ? 1 : 0)->setChecked(true);
                                            m_context.setNationalUiMode(national);
                                            rebuildMenu(/*selectFirst*/ false);
                                            updateHeader();
                                        }
                                        navigateTo(pageId);
                                    }});
                }
            }
        }
    }
    const auto action = [&](const QString &icon, const QString &title, const QString &shortcut,
                            std::function<void()> run) {
        entries.append({tr("Aktionen"), LineIcons::icon(icon, color, 18, dpr), title, shortcut,
                        foldForSearch(title), std::move(run)});
    };
    action(QStringLiteral("upload"), tr("Export importieren …"), tr("Strg+I"),
           [this] { importExport({}); });
    action(QStringLiteral("folder"), tr("Spielstand wechseln …"), tr("Strg+O"),
           [this] { switchSave(); });
    action(QStringLiteral("plus"), tr("Neuer Spielstand …"), tr("Strg+N"), [this] { newSave(); });
    action(QStringLiteral("trend"), tr("DWRS neu berechnen"), {}, [this] { startDwrsRecalc(); });
    action(QStringLiteral("moon"), tr("Tag/Nacht wechseln"), {}, [this] { m_theme.toggle(); });
    m_palette->setEntries(std::move(entries));

    m_palette->setPlayerSearch([this, color, dpr](const QString &query) {
        if (m_searchModelDirty)
            rebuildSearchModel();
        QList<CommandPalette::Entry> found;
        const auto &players = m_context.store().players();
        const QIcon icon = LineIcons::icon(QStringLiteral("person"), color, 18, dpr);
        // Name starts with the query first, then anywhere.
        for (const bool prefixPass : {true, false}) {
            for (qsizetype i = 0; i < m_searchKeys.size() && found.size() < 8; ++i) {
                const bool prefix = m_searchKeys.at(i).startsWith(query);
                if (prefix != prefixPass || (!prefix && !m_searchKeys.at(i).contains(query)))
                    continue;
                const Player &player = players.at(static_cast<size_t>(i));
                found.append({tr("Spieler"), icon, player.name,
                              QStringLiteral("%1 · %2").arg(player.club, player.positionRaw), {},
                              [this, uid = player.uid] {
                                  m_context.setPendingProfileUid(uid);
                                  navigateTo(QStringLiteral("player_profile"));
                              }});
            }
        }
        return found;
    });
    m_palette->popup();
}

void MainWindow::updateDbLabel()
{
    const SaveInfo save = SaveLibrary::current(m_context);
    m_saveSwitcher->setIcon(QIcon(SaveLibrary::crest(save, 32, devicePixelRatioF())));
    m_saveSwitcher->setText(QStringLiteral("%1\n%2 · %3")
                                .arg(save.name, SaveLibrary::shortVersionLabel(save.fmVersionId),
                                     tr("%1 Spieler").arg(QLocale().toString(save.playerCount))));
}

void MainWindow::fillSaveMenu(QMenu *menu)
{
    menu->clear();
    const QList<SaveInfo> saves = SaveLibrary::scan(m_context);
    int shown = 0;
    for (const SaveInfo &save : saves) {
        if (save.name == m_context.currentDbName())
            continue;
        if (shown == 0)
            menu->addSection(tr("Zuletzt verwendet"));
        QAction *action = menu->addAction(
            QIcon(SaveLibrary::crest(save, 20, devicePixelRatioF())),
            QStringLiteral("%1  ·  %2").arg(save.name,
                                            SaveLibrary::shortVersionLabel(save.fmVersionId)));
        connect(action, &QAction::triggered, this, [this, name = save.name] {
            QString error;
            if (!m_context.openDatabase(name, &error))
                QMessageBox::critical(this, tr("Spielstand öffnen"), error);
        });
        if (++shown == 5)
            break;
    }
    if (shown > 0)
        menu->addSeparator();
    menu->addAction(tr("Alle Spielstände …"), this, &MainWindow::switchSave);
    menu->addAction(tr("Neuer Spielstand …"), this, &MainWindow::newSave);
}

void MainWindow::newSave()
{
    NewSaveWizard wizard(m_context, this);
    wizard.exec();
}

void MainWindow::switchSave()
{
    StartScreen screen(m_context, m_theme, StartScreen::Mode::Switch, this);
    screen.exec();
}

void MainWindow::importExport(QString filePath)
{
    if (filePath.isEmpty()) {
        filePath = QFileDialog::getOpenFileName(
            this, tr("FM-Export (HTML oder CSV) auswählen"), m_context.htmlExportDir(),
            tr("FM-Exporte (*.html *.htm *.csv);;Alle Dateien (*)"));
        if (filePath.isEmpty())
            return;
    }
    // The import (with its options) lives on the mode's overview page.
    const bool national = m_context.nationalUiMode();
    navigateTo(national ? QStringLiteral("national_dashboard") : QStringLiteral("dashboard"));
    if (national) {
        if (auto *page = qobject_cast<NationalDashboardPage *>(m_pages.value(
                QStringLiteral("national_dashboard"))))
            page->prepareImport(filePath);
    } else if (auto *page = qobject_cast<DashboardPage *>(m_pages.value(QStringLiteral("dashboard")))) {
        page->prepareImport(filePath);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!DropZone::importableFileFrom(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QString path = DropZone::importableFileFrom(event->mimeData());
    if (path.isEmpty())
        return;
    event->acceptProposedAction();
    importExport(path);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Background jobs (import, recalcs) still hold pointers into AppContext (the
    // engine, the definitions). Let every one of them finish before teardown.
    m_context.waitForBackgroundTasks();
    m_context.paths().setWindowGeometry(saveGeometry());
    QMainWindow::closeEvent(event);
}

} // namespace fm
