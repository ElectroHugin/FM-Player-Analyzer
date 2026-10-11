#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>

#include "Navigation.h"
#include "core/RatingsUpdater.h"

class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace fm {

class AppContext;
class BusyProgressDialog;
class PageBase;
class PlayerSearchModel;
class ThemeManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(AppContext &context, ThemeManager &theme, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    // Mouse back/forward side buttons anywhere in this window drive the history.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // One Back/Forward history step: the page, the management mode it was
    // shown in, and page-specific state (PageBase::historyState()).
    struct HistoryEntry {
        QString pageId;
        bool national = false;
        QString state;
    };

    void buildSidebar();
    void buildMenuBar();
    void changeLanguage(const QString &language);
    // selectFirst: also navigate to the first entry (a user mode switch);
    // false when the history restores a page itself.
    void rebuildMenu(bool selectFirst = true);
    // Rebuilds the sidebar (e.g. a page appeared) and keeps the current page selected.
    void refreshMenuKeepingPage();
    // Highlights the sidebar group of pageId without triggering navigation.
    void selectMenuEntry(const QString &pageId);
    // Tabs of pageId's sidebar group above the page; hidden for a single page.
    void updateTabBar(const QString &pageId);
    // A sidebar group: its last shown tab, else its first.
    void navigateToGroup(const QString &groupId);
    void navigateTo(const QString &pageId);
    void goBack();
    void goForward();
    void goHome();
    void goToHistory(int index);
    void captureHistoryState();
    void pushHistory(const QString &pageId, PageBase *page);
    void resetHistory();
    void updateNavButtons();
    PageBase *createPage(const QString &pageId);
    void startDwrsRecalc();
    // Asks for a full recalculation when the active database's ratings were
    // computed with an older DWRS formula (kDwrsSchemeVersion).
    void offerRecalcIfOutdated();
    void updateDbLabel();
    void updateHeader();
    void rebuildSearchModel();

    AppContext &m_context;
    ThemeManager &m_theme;

    QListWidget *m_menu = nullptr;
    QComboBox *m_modeCombo = nullptr; // Club / National
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_dbLabel = nullptr;
    QLabel *m_headerLogo = nullptr;     // optional club logo (user PNG)
    QLabel *m_headerIdentity = nullptr; // club/national name + stadium
    QLabel *m_headerSave = nullptr;     // active save file
    QStackedWidget *m_stack = nullptr;
    QHash<QString, PageBase *> m_pages; // by host page id (Navigation::hostPage), lazily created
    QString m_currentPageId;            // page id shown (not its host)
    QList<Navigation::Section> m_sidebar;
    QWidget *m_tabStrip = nullptr;
    QTabBar *m_tabBar = nullptr;
    bool m_updatingTabs = false;
    QHash<QString, QString> m_groupTabs; // group id -> last page id shown in it

    QFutureWatcher<RatingsUpdater::Result> m_recalcWatcher;
    BusyProgressDialog *m_recalcDialog = nullptr;

    QToolButton *m_backButton = nullptr;
    QToolButton *m_forwardButton = nullptr;
    QToolButton *m_homeButton = nullptr;
    QList<HistoryEntry> m_history;
    int m_historyIndex = -1;
    bool m_restoringHistory = false; // navigateTo() must not record a new step

    QCompleter *m_searchCompleter = nullptr;
    PlayerSearchModel *m_searchModel = nullptr;
    bool m_searchModelDirty = true;
    bool m_registrationInMenu = false; // sidebar shows the registration page
};

} // namespace fm
