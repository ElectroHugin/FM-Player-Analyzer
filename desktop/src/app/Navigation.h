#pragma once

#include <QList>
#include <QString>

namespace fm {

// The sidebar's structure: sections of groups, each group one sidebar entry
// whose pages appear as tabs above the content. Every navigation target keeps
// its own page id (context menus, the player search and the Back/Forward
// history address pages by id), so a page id says both which group to
// highlight and which tab to select.
namespace Navigation {

struct Tab {
    QString label;
    QString pageId;
    QString keywords; // extra search words for the command palette
};

struct Group {
    QString id;
    QString label;
    QList<Tab> tabs; // a single tab shows no tab bar
};

struct Section {
    QString heading;
    QList<Group> groups;
};

// The sidebar of a management mode.
QList<Section> sidebar(bool national);

// The group that holds pageId in the given sidebar, or nullptr.
const Group *findGroup(const QList<Section> &sections, const QString &pageId);

// Several page ids can be views of one page object (they share its controls,
// e.g. the Best XI tactic choice): the id of the object that shows pageId.
QString hostPage(const QString &pageId);

} // namespace Navigation

} // namespace fm
