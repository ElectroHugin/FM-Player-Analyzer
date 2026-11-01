#pragma once

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace fm {

class AppContext;
class DropZone;

// "Neuer Spielstand" in three steps: name + FM version (creates and opens the
// save), first import (optional), managed club (optional, from the imported
// players). Once step 1 is done the new save stays open even if the rest is
// skipped; accepted() then means "a new save is open".
class NewSaveWizard : public QDialog
{
    Q_OBJECT

public:
    explicit NewSaveWizard(AppContext &context, QWidget *parent = nullptr);

    QString createdName() const { return m_createdName; }

private:
    QWidget *buildNamePage();
    QWidget *buildImportPage();
    QWidget *buildClubPage();

    void goTo(int step);
    void next();
    void skip();
    bool createSave();
    void startImport();
    void fillClubs();
    void finish();

    AppContext &m_context;
    QString m_createdName;
    QString m_importFile;
    bool m_importRunning = false;

    QList<QLabel *> m_stepLabels;
    QStackedWidget *m_pages;
    QLineEdit *m_nameEdit;
    QLabel *m_nameError;
    QButtonGroup *m_versionGroup;
    DropZone *m_dropZone;
    QCheckBox *m_autoAssignCheck;
    QLabel *m_importResult;
    QComboBox *m_clubCombo;
    QLabel *m_clubHint;
    QLabel *m_importSummary;
    QPushButton *m_cancelButton;
    QPushButton *m_skipButton;
    QPushButton *m_nextButton;
};

} // namespace fm
