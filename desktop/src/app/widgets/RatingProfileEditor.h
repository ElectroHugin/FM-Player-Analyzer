#pragma once
#include "core/RatingProfileLibrary.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QLineEdit;
class QListWidget;
class QLabel;
namespace fm {
class RatingCurveEditor;
class RatingProfileEditor : public QWidget {
    Q_OBJECT
public:
    RatingProfileEditor(const QString &libraryPath, const QString &game, QWidget *parent = nullptr);
    void setChartPalette(const QPalette &palette);
    bool installBuiltins(const QList<RatingProfile> &profiles);
    void setGame(const QString &game) { m_game = game; }
    bool hasDraftChanges() const { return m_dirty; }
    // Asks before unsaved changes are lost; false keeps the draft.
    bool confirmDiscard() { return discardDraft(); }
    // Marks the open save's profile and enables "use for this save" for
    // profiles compatible with its game.
    void setActiveProfile(const QString &id);
    // Text for a profile's source line: version, provisional note, sources.
    // Translated name of an attribute by its index (Attributes.h order).
    static QString attributeLabel(int index);
signals:
    void activateRequested(const QString &id);
    void librarySaved();
private:
    void updateFactors();
    bool discardDraft();
    void fillProfiles(const QString &select = {});
    void selectProfile(const QString &id);
    void fillAttributes();
    void selectAttribute();
    void filterAttributes();
    void save(bool asNew);
    bool persist(RatingProfileLibrary candidate, const QString &select);
    void reportError(const QString &error);
    void importProfile();
    void exportProfile();
    void newProfile();
    void removeProfile();
    void resetProfile();
    QString m_path, m_game, m_selectedId, m_attribute;
    RatingProfileLibrary m_library;
    RatingProfile m_draft;
    QString m_expectedRevision;
    bool m_dirty = false, m_loading = false, m_storageValid = true;
    QComboBox *m_profiles;
    QComboBox *m_category;
    QLineEdit *m_name;
    QLineEdit *m_search;
    QListWidget *m_attributes;
    RatingCurveEditor *m_curve;
    QLabel *m_status;
    QString m_activeId;
    QPushButton *m_useForSave = nullptr;
    QDoubleSpinBox *m_keyFactor = nullptr;
    QDoubleSpinBox *m_preferableFactor = nullptr;
    QCheckBox *m_roleShareOn = nullptr;
    QSpinBox *m_roleShare = nullptr;
    QCheckBox *m_goalkeeperShareOn = nullptr;
    QSpinBox *m_goalkeeperShare = nullptr;
};
} // namespace fm
