#pragma once
#include "core/RatingProfileLibrary.h"
#include <QWidget>
class QComboBox;
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
    void setGame(const QString &game) { m_game = game; }
    bool hasDraftChanges() const { return m_dirty; }
private:
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
};
} // namespace fm
