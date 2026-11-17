#pragma once

#include "core/UpdateCheck.h"

#include <QDialog>

namespace fm {

class AppPaths;

// Shows a newer release: its version, its release notes and a button that
// opens the release page in the browser. Nothing is downloaded or installed
// by the app itself.
class UpdateDialog : public QDialog
{
    Q_OBJECT

public:
    UpdateDialog(const ReleaseInfo &release, AppPaths &paths, QWidget *parent = nullptr);

    // Asks once (at the first start after installing or updating) whether the
    // app may check for new versions. Stores the answer either way.
    static void askForConsent(AppPaths &paths, QWidget *parent);
};

} // namespace fm
