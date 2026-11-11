#pragma once

#include <QWidget>

namespace fm {

class AppContext;

// Base for all navigable pages. Pages are constructed lazily on first
// activation; refresh() is called when the page becomes visible or when
// AppContext::dataChanged() fires while it is visible.
class PageBase : public QWidget
{
    Q_OBJECT

public:
    explicit PageBase(AppContext &context, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_context(context)
    {
    }

    virtual void refresh() {}
    // Called before navigating away; false keeps the page (e.g. unsaved draft).
    virtual bool confirmLeave() { return true; }

    // One page object can serve several navigation targets (tabs), e.g. Best
    // XI's first-team and youth views share the tactic choice. Called with the
    // target's page id each time the page is shown for it, before refresh().
    virtual void showSection(const QString &pageId) { Q_UNUSED(pageId); }

    // Called on the non-visible pages whenever the player data changes, and on
    // every page (the visible one too, refreshed right after) when the store
    // was replaced, so no page keeps dangling Player* into the old store. Pages
    // that cache raw Player* (table models, filter pools) override this to
    // drop them; they rebuild in refresh() when shown again.
    virtual void releaseStoreRows() {}

    // Back/Forward history (MainWindow): page-specific state worth restoring
    // when the user navigates back — e.g. which player the profile page shows.
    // Empty = the page itself is enough.
    virtual QString historyState() const { return {}; }
    // Called right before the page is re-shown from the history, with the
    // state historyState() returned back then.
    virtual void restoreHistoryState(const QString &state) { Q_UNUSED(state); }

protected:
    AppContext &m_context;
};

} // namespace fm
