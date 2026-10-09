#pragma once

#include "core/Utils.h"

#include <QTableWidgetItem>

namespace fm {

// Table item that displays text but sorts by a numeric key, so columns like
// age/DWRS/value sort by magnitude instead of lexicographically. Shared by the
// table pages (previously duplicated in each one).
class NumericItem : public QTableWidgetItem
{
public:
    NumericItem(const QString &text, double sortKey)
        : QTableWidgetItem(text)
        , m_key(sortKey)
    {
    }
    bool operator<(const QTableWidgetItem &other) const override
    {
        if (const auto *numeric = dynamic_cast<const NumericItem *>(&other))
            return m_key < numeric->m_key;
        return QTableWidgetItem::operator<(other);
    }

private:
    double m_key;
};

// Player-name item that shows the full name ("Frank Müller") but sorts by last
// name, then by the full name — the order of Football Manager's own lists.
class NameItem : public QTableWidgetItem
{
public:
    explicit NameItem(const QString &name)
        : QTableWidgetItem(name)
        , m_lastName(getLastName(name))
    {
    }
    bool operator<(const QTableWidgetItem &other) const override
    {
        if (const auto *name = dynamic_cast<const NameItem *>(&other)) {
            const int cmp = m_lastName.localeAwareCompare(name->m_lastName);
            return cmp != 0 ? cmp < 0 : text().localeAwareCompare(name->text()) < 0;
        }
        return QTableWidgetItem::operator<(other);
    }

private:
    QString m_lastName;
};

} // namespace fm
