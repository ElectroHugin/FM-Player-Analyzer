#include "ColumnChooser.h"

#include "../AppContext.h"
#include "PlayerTableModel.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QPushButton>
#include <QTableView>

namespace fm {

namespace {

enum class Phase { None, Ip, Oop };

Phase phaseOf(const QString &header)
{
    if (header.endsWith(QLatin1String("-IP")))
        return Phase::Ip;
    if (header.endsWith(QLatin1String("-OOP")))
        return Phase::Oop;
    return Phase::None;
}

} // namespace

ColumnChooser::ColumnChooser(AppContext &context, QTableView *view, PlayerTableModel *model,
                             QString key, QStringList defaultHidden, QObject *parent)
    : QObject(parent), m_context(context), m_view(view), m_model(model), m_key(std::move(key)),
      m_defaultHidden(std::move(defaultHidden))
{
    QHeaderView *header = m_view->horizontalHeader();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested, this,
            [this, header](const QPoint &pos) { showMenu(header->mapToGlobal(pos)); });
    // The view sits on a proxy; apply once the proxy (and header) reset.
    connect(m_view->model(), &QAbstractItemModel::modelReset, this, &ColumnChooser::apply);
}

QPushButton *ColumnChooser::createButton(QWidget *parent)
{
    auto *button = new QPushButton(tr("Spalten…"), parent);
    button->setToolTip(tr("Spalten ein- oder ausblenden. Auch per Rechtsklick auf die "
                          "Kopfzeile der Tabelle."));
    connect(button, &QPushButton::clicked, this, [this, button] {
        showMenu(button->mapToGlobal(QPoint(0, button->height())));
    });
    return button;
}

QSet<QString> ColumnChooser::hiddenHeaders() const
{
    const QString stored =
        m_context.database().setting(QStringLiteral("table_columns/") + m_key);
    if (stored.isEmpty())
        return QSet<QString>(m_defaultHidden.cbegin(), m_defaultHidden.cend());
    QSet<QString> hidden;
    for (const QJsonValue &value : QJsonDocument::fromJson(stored.toUtf8()).array())
        hidden.insert(value.toString());
    return hidden;
}

void ColumnChooser::storeHidden(const QSet<QString> &hidden)
{
    QJsonArray array;
    for (const QString &header : hidden)
        array.append(header);
    m_context.database().setSetting(QStringLiteral("table_columns/") + m_key,
                                    QString::fromUtf8(QJsonDocument(array).toJson(
                                        QJsonDocument::Compact)));
    apply();
}

void ColumnChooser::apply()
{
    const QSet<QString> hidden = hiddenHeaders();
    for (int c = 0; c < m_model->columnCount(); ++c) {
        const bool hide = !m_model->isPinned(c) && hidden.contains(m_model->headerText(c));
        m_view->setColumnHidden(c, hide);
    }
}

void ColumnChooser::showMenu(const QPoint &globalPos)
{
    QSet<QString> hidden = hiddenHeaders();
    QMenu menu(m_view);

    QStringList ipRoles;
    QStringList oopRoles;
    for (int c = 0; c < m_model->columnCount(); ++c) {
        if (m_model->isPinned(c))
            continue;
        const QString header = m_model->headerText(c);
        if (phaseOf(header) == Phase::Ip)
            ipRoles << header;
        else if (phaseOf(header) == Phase::Oop)
            oopRoles << header;
    }

    QAction *showAll = menu.addAction(tr("Alle Spalten zeigen"));
    connect(showAll, &QAction::triggered, this, [this] { storeHidden({}); });
    QAction *reset = menu.addAction(tr("Standard wiederherstellen"));
    connect(reset, &QAction::triggered, this, [this] {
        m_context.database().removeSetting(QStringLiteral("table_columns/") + m_key);
        apply();
    });

    const auto addPhaseToggle = [&](const QString &label, const QStringList &headers) {
        if (headers.isEmpty())
            return;
        bool anyVisible = false;
        for (const QString &header : headers)
            anyVisible = anyVisible || !hidden.contains(header);
        QAction *action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(anyVisible);
        connect(action, &QAction::toggled, this, [this, hidden, headers](bool on) mutable {
            for (const QString &header : headers) {
                if (on)
                    hidden.remove(header);
                else
                    hidden.insert(header);
            }
            storeHidden(hidden);
        });
    };
    if (!ipRoles.isEmpty() || !oopRoles.isEmpty()) {
        menu.addSeparator();
        addPhaseToggle(tr("Alle Rollen mit Ball (IP)"), ipRoles);
        addPhaseToggle(tr("Alle Rollen ohne Ball (OOP)"), oopRoles);
    }

    menu.addSeparator();
    for (int c = 0; c < m_model->columnCount(); ++c) {
        if (m_model->isPinned(c))
            continue;
        const QString header = m_model->headerText(c);
        QAction *action = menu.addAction(header);
        action->setCheckable(true);
        action->setChecked(!hidden.contains(header));
        connect(action, &QAction::toggled, this, [this, hidden, header](bool on) mutable {
            if (on)
                hidden.remove(header);
            else
                hidden.insert(header);
            storeHidden(hidden);
        });
    }
    menu.exec(globalPos);
}

} // namespace fm
