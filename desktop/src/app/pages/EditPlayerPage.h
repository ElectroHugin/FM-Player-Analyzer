#pragma once

#include "PageBase.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace fm {

struct Player;

// Edit app-managed player data: club, agreed playing time, natural positions,
// primary role, preferred side and (with registration rules active) the
// home-grown / club-trained / U21 status. Port of legacy edit_player.py.
class EditPlayerPage : public PageBase
{
    Q_OBJECT

public:
    explicit EditPlayerPage(AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

private:
    void rebuildClubCombo();
    void runSearch();
    void showEditor();
    void save();
    const Player *currentPlayer() const;

    QComboBox *m_clubPlayerCombo = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_searchResultCombo = nullptr;

    QGroupBox *m_editorBox = nullptr;
    QLineEdit *m_clubEdit = nullptr;
    QComboBox *m_aptCombo = nullptr;
    QLabel *m_aptLabel = nullptr;
    QListWidget *m_naturalPositionsList = nullptr;
    QComboBox *m_primaryRoleCombo = nullptr;
    QComboBox *m_preferredSideCombo = nullptr;
    QWidget *m_tacticalColumn = nullptr;
    QWidget *m_registrationBox = nullptr;
    QCheckBox *m_homeGrownCheck = nullptr;
    QCheckBox *m_clubTrainedCheck = nullptr;
    QComboBox *m_u21Combo = nullptr;
    QPushButton *m_saveButton = nullptr;

    QString m_currentUid;
    bool m_updating = false;
};

} // namespace fm
