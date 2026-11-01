#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QTimer>

#include <functional>

#include "Navigation.h"
#include "core/RatingsUpdater.h"

class QButtonGroup;
class QFrame;
class QPushButton;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;
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
class CommandPalette;
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
    // An FM export dropped anywhere on the window opens the import with it.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

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
    void showAbout();
    // selectFirst: also navigate to the first entry (a user mode switch);
    // false when the history restores a page itself.
    void rebuildMenu(bool selectFirst = true);
    // Rebuilds the sidebar (e.g. a page appeared) and keeps the current page selected.
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
    void fillSaveMenu(QMenu *menu);
    void newSave();
    void switchSave();
    // Opens the import of the current mode's overview with filePath (asks
    // for a file when empty).
    void importExport(QString filePath);
    void updateHeader();
    void rebuildSearchModel();
    void openPalette();
    void updateMenuIcons();
    void updateBreadcrumb(const QString &pageId);
    // Hint bar above the page (instead of a blocking pop-up).
    void showNotice(const QString &text, const QString &actionLabel,
                    std::function<void()> action);
    // Short confirmation at the bottom right that fades out by itself.
    void showToast(const QString &text);

    AppContext &m_context;
    ThemeManager &m_theme;

    QListWidget *m_menu = nullptr;
    QButtonGroup *m_modeGroup = nullptr; // 0 = club, 1 = national
    QLabel *m_breadcrumb = nullptr;
    QPushButton *m_paletteButton = nullptr;
    QPushButton *m_importButton = nullptr;
    CommandPalette *m_palette = nullptr;
    QFrame *m_noticeBar = nullptr;
    QLabel *m_noticeText = nullptr;
    QPushButton *m_noticeLater = nullptr;
    QPushButton *m_noticeAction = nullptr;
    std::function<void()> m_noticeCallback;
    QLabel *m_toast = nullptr;
    QTimer m_toastTimer;
    QToolButton *m_saveSwitcher = nullptr;
    QLabel *m_headerLogo = nullptr;     // optional club logo (user PNG)
    QLabel *m_headerIdentity = nullptr; // club/national name + stadium
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

    QStringList m_searchKeys; // folded "name club" per store row, for the palette
    bool m_searchModelDirty = true;
};

} // namespace fm
