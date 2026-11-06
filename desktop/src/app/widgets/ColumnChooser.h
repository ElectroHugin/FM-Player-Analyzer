#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>

class QPoint;
class QPushButton;
class QTableView;
class QWidget;

namespace fm {

class AppContext;
class PlayerTableModel;

// Lets the user pick which columns of a player table are shown: right-click
// on the header or the "Spalten" button. Role columns can be toggled per
// phase (with / without ball) at once. Pinned columns (name, search focus)
// always stay. The choice is stored per save under "table_columns/<key>".
class ColumnChooser : public QObject
{
    Q_OBJECT

public:
    ColumnChooser(AppContext &context, QTableView *view, PlayerTableModel *model, QString key,
                  QStringList defaultHidden, QObject *parent = nullptr);

    // A "Spalten" button that opens the same menu as the header.
    QPushButton *createButton(QWidget *parent);
    // Re-applies the visibility (call after the model's columns changed).
    void apply();

private:
    QSet<QString> hiddenHeaders() const;
    void storeHidden(const QSet<QString> &hidden);
    void showMenu(const QPoint &globalPos);

    AppContext &m_context;
    QTableView *m_view;
    PlayerTableModel *m_model;
    QString m_key;
    QStringList m_defaultHidden;
};

} // namespace fm
