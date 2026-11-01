#pragma once

#include <QDialog>
#include <QIcon>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;

namespace fm {

// Ctrl+K: one field to jump to players, pages and actions. The owner feeds
// the fixed entries (pages, actions) and a player search callback.
class CommandPalette : public QDialog
{
    Q_OBJECT

public:
    struct Entry {
        QString section; // group heading ("Spieler", "Seiten", "Aktionen")
        QIcon icon;
        QString title;
        QString detail;   // right-hand hint (club, shortcut, area)
        QString foldedKey; // foldForSearch()-ed text the query is matched against
        std::function<void()> run;
    };
    // Returns the best player matches for a folded query (may be empty).
    using PlayerSearch = std::function<QList<Entry>(const QString &foldedQuery)>;

    explicit CommandPalette(QWidget *parent);

    void setEntries(QList<Entry> entries) { m_entries = std::move(entries); }
    void setPlayerSearch(PlayerSearch search) { m_playerSearch = std::move(search); }

    // Shows the palette centered over the parent window with an empty query.
    void popup();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void runCurrent();
    void moveSelection(int delta);

    QList<Entry> m_entries;
    PlayerSearch m_playerSearch;
    QList<Entry> m_shown; // row -> entry (section rows hold an empty entry)
    QLineEdit *m_input;
    QListWidget *m_list;
    QLabel *m_hint;
};

} // namespace fm
