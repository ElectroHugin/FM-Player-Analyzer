#pragma once

#include "PageBase.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace fm {

struct Player;

// National call-up assistant: computes the N-player squad that best strengthens
// the national side (starting XI + B-team, exactly G goalkeepers) from the
// whole eligible pool, then diffs it against the current squad to recommend who
// to invite and who to drop. Injured players can be blocked for the run.
class NationalCallupPage : public PageBase
{
    Q_OBJECT

public:
    explicit NationalCallupPage(AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

private:
    // Eligible = nationality/second nationality matches the national code and
    // (age limit disabled or within it). excludeInjured drops the marked ones.
    std::vector<const Player *> eligiblePool(bool excludeInjured) const;
    // The baseline the invite/drop diff is measured against (saved squad or the
    // uploaded set), resolved to players in the store.
    std::vector<const Player *> currentSquad() const;

    void rebuildInjuredList();
    void uploadSquad();
    void compute();
    void applyRecommendation();
    void showResults(bool visible);

    QLabel *m_hint = nullptr;

    // Step 1: current-squad baseline.
    QRadioButton *m_savedRadio = nullptr;
    QRadioButton *m_uploadRadio = nullptr;
    QPushButton *m_uploadButton = nullptr;
    QLabel *m_baselineLabel = nullptr;
    QSet<QString> m_uploadedUids;
    bool m_haveUpload = false;

    // Step 2: parameters.
    QSpinBox *m_totalSpin = nullptr;
    QSpinBox *m_gkSpin = nullptr;
    QComboBox *m_tacticCombo = nullptr;

    // Step 3: injured / unavailable.
    QLineEdit *m_injuredSearch = nullptr;
    QListWidget *m_injuredList = nullptr;
    QLabel *m_injuredCountLabel = nullptr;
    QSet<QString> m_injuredUids;
    bool m_updatingInjured = false;

    QPushButton *m_computeButton = nullptr;

    // Results.
    QWidget *m_resultsBox = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QListWidget *m_inviteList = nullptr;
    QListWidget *m_dropList = nullptr;
    QPushButton *m_applyButton = nullptr;
    QStringList m_recommendedUids; // final squad to persist on "apply"
};

} // namespace fm
