#pragma once

#include "core/TrainingAdvice.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace fm {

// Settings -> Training: the save's training profile. Bands (name, age window,
// weight, mentoring only) and which band each attribute belongs to; bands can
// be added and removed, the profile exported to and imported from JSON.
class TrainingProfileEditor : public QWidget
{
    Q_OBJECT

public:
    explicit TrainingProfileEditor(QWidget *parent = nullptr);

    // standard: what "Restore standard" brings back.
    void setProfile(const TrainingAdvice::TrainingProfile &profile,
                    const TrainingAdvice::TrainingProfile &standard);
    TrainingAdvice::TrainingProfile profile() const;

    // Display name of a band (the standard bands are stored language-neutral).
    static QString bandLabel(const QString &storedName);

private:
    void fillBands(const TrainingAdvice::TrainingProfile &profile);
    void fillAttributes(const TrainingAdvice::TrainingProfile &profile);
    void refreshAttributeChoices();
    void addBand();
    void removeBand();
    void importProfile();
    void exportProfile();
    QString storedName(const QString &label) const;

    TrainingAdvice::TrainingProfile m_standard;
    QTableWidget *m_bands = nullptr;
    QTableWidget *m_attributes = nullptr;
    QPushButton *m_removeBand = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace fm
