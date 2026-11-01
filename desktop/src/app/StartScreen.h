#pragma once

#include "SaveLibrary.h"

#include <QDialog>

class QCheckBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace fm {

class AppContext;
class ThemeManager;

// Start screen: pick a recent save, create a new one or bring one in. Shown
// at launch (unless "always open the last save" is set) and from the main
// window via "Spielstand wechseln". accept() means the chosen save is open.
class StartScreen : public QDialog
{
    Q_OBJECT

public:
    enum class Mode {
        Startup, // before the main window exists; closing quits the app
        Switch,  // from the main window; a save is open, closing keeps it
    };

    StartScreen(AppContext &context, ThemeManager &theme, Mode mode,
                QWidget *parent = nullptr);

    // Relaunches the executable (after a language change) and quits.
    static void relaunchApplication();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QWidget *buildHero();
    QWidget *buildList();
    QWidget *buildSaveCard(const SaveInfo &save);

    void reload();
    void applyFilter();
    void updateOpenButton();
    QString selectedName() const;
    bool openSave(const QString &name);
    void showSaveMenu(const QString &name, const QPoint &globalPos);
    void newSave();
    void openExternalFile();
    void migrateLegacy();
    void renameSave(const QString &name);
    void duplicateSave(const QString &name);
    void deleteSave(const QString &name);
    void changeLanguage(const QString &language);
    void updateThemedIcons();

    AppContext &m_context;
    ThemeManager &m_theme;
    Mode m_mode;
    QList<SaveInfo> m_saves;

    QLineEdit *m_search;
    QStackedWidget *m_listStack;
    QListWidget *m_list;
    QLabel *m_emptyLabel;
    QCheckBox *m_autoOpenCheck;
    QPushButton *m_openButton;
    QToolButton *m_themeButton = nullptr;
};

} // namespace fm
