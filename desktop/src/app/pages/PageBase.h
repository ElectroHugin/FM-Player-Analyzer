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

    // Called on every non-visible page when the player store is replaced (a
    // reload), so no page keeps dangling Player* into the old store. Pages that
    // cache raw Player* (table models, filter pools) override this to drop them;
    // they rebuild in refresh() when shown again. The visible page is refreshed
    // instead, so it is not asked to release.
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
