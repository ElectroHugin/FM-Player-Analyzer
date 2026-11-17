#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

namespace fm {

// First-launch dialog: choose the data directory (databases, config,
// definitions, backups) and whether the app may check for updates. Preselects %LOCALAPPDATA%\FM24PlayerAnalyzer.
class FirstRunDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FirstRunDialog(const QString &defaultDir, QWidget *parent = nullptr);

    QString chosenDataDir() const;
    // The answer to "check for new versions at startup?" (off by default).
    bool updateCheckChosen() const;

private:
    QLineEdit *m_pathEdit;
    QCheckBox *m_updateCheck;
};

} // namespace fm
