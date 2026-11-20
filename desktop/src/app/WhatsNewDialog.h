#pragma once

#include <QDialog>

class QCheckBox;

namespace fm {

class AppPaths;

// The changes of the version that is running, in the UI language: shown
// once at the first start after an update (unless the user ticked "Don't
// show again") and from Help → What's new at any time. The texts come from
// the changelog built into the app: CHANGELOG.md and its translations in
// desktop/changelog/. A version missing from a translation shows in English.
class WhatsNewDialog : public QDialog
{
    Q_OBJECT

public:
    // notes: Markdown from changelogNotes(). withOptOut adds the
    // "Don't show again" box (only for the automatic start-up dialog).
    WhatsNewDialog(const QString &notes, AppPaths &paths, bool withOptOut,
                   QWidget *parent = nullptr);

    // The changes from fromVersion (exclusive, may be empty) up to toVersion
    // in the given UI language, falling back to English. Empty if the
    // changelog has nothing for that range.
    static QString notes(const QString &language, const QString &fromVersion,
                         const QString &toVersion);
    // The changelog file for a UI language: CHANGELOG.md for English,
    // CHANGELOG.<language>.md for a translation (desktop/changelog/).
    static QString changelogFileName(const QString &language);

    // At startup: shows the dialog once if the running version is newer
    // than the one that last started and the user did not opt out. Records
    // the running version either way.
    static void showAfterUpdate(AppPaths &paths, const QString &language, QWidget *parent);

private:
    AppPaths &m_paths;
    QCheckBox *m_optOut = nullptr;
};

} // namespace fm
