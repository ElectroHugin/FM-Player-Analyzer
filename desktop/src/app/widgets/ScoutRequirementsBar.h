#pragma once

#include "core/ScoutSearch.h"

#include <QGroupBox>
#include <QList>
#include <QString>
#include <QStringList>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace fm {

class Definitions;

// Requirements of the scouting search (2.1.0): a list of criteria — roles,
// positions, places (position + role) and bridges (with ball + without
// ball) — that a candidate must ALL meet. One row per criterion with a
// remove button, below them a position and a role choice to add one.
class ScoutRequirementsBar : public QGroupBox
{
    Q_OBJECT

public:
    explicit ScoutRequirementsBar(const Definitions &definitions, QWidget *parent = nullptr);

    // Role ids and game positions ("AM (R)") offered for new criteria.
    void setRoleChoices(const QStringList &roles);
    void setPositionChoices(const QStringList &positions);

    // Replaces the list (no criteriaChanged); source is shown above it,
    // e.g. where a bridge search came from.
    void setCriteria(const QList<ScoutCriterion> &criteria, const QString &source = QString());
    const QList<ScoutCriterion> &criteria() const { return m_criteria; }

    // Readable text of a criterion ("Bridge: mit Ball AMR · IF (IP) + …").
    QString describe(const ScoutCriterion &criterion) const;

signals:
    // The user added or removed a criterion.
    void criteriaChanged();

private:
    void rebuildRows();
    QString describePlace(const ScoutPlace &place) const;

    const Definitions &m_definitions;
    QList<ScoutCriterion> m_criteria;
    QString m_source;
    QLabel *m_sourceLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QComboBox *m_positionCombo = nullptr;
    QComboBox *m_roleCombo = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_clearButton = nullptr;
};

} // namespace fm
