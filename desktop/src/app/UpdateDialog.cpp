#include "UpdateDialog.h"

#include "AppPaths.h"
#include "core/Version.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace fm {

UpdateDialog::UpdateDialog(const ReleaseInfo &release, AppPaths &paths, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Update available"));
    resize(560, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(release.preRelease
                                 ? tr("Test version %1 is available").arg(release.version)
                                 : tr("Version %1 is available").arg(release.version),
                             this);
    title->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(title);
    auto *current = new QLabel(tr("You are using version %1.").arg(appVersion()), this);
    current->setObjectName(QStringLiteral("kpiCaption"));
    layout->addWidget(current);

    auto *notesHeading = new QLabel(tr("What's new"), this);
    layout->addWidget(notesHeading);
    auto *notes = new QTextBrowser(this);
    // Release notes are shown as text only; links in them are not followed.
    notes->setOpenLinks(false);
    if (release.notes.trimmed().isEmpty())
        notes->setPlainText(tr("No release notes for this version."));
    else
        notes->setMarkdown(release.notes);
    layout->addWidget(notes, 1);

    auto *hint = new QLabel(tr("The download opens the release page in your browser. Install the "
                               "new version as usual; your saves and settings stay as they are."),
                            this);
    hint->setObjectName(QStringLiteral("kpiCaption"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto *skip = new QPushButton(tr("Skip this version"), this);
    skip->setToolTip(tr("No notice at startup for this version. A newer one is shown again."));
    auto *later = new QPushButton(tr("Later"), this);
    auto *download = new QPushButton(tr("Download"), this);
    download->setObjectName(QStringLiteral("headerPrimary"));
    download->setDefault(true);
    download->setEnabled(!release.pageUrl.isEmpty());
    buttons->addWidget(skip);
    buttons->addStretch(1);
    buttons->addWidget(later);
    buttons->addWidget(download);
    layout->addLayout(buttons);

    const QString version = release.version;
    connect(skip, &QPushButton::clicked, this, [this, &paths, version] {
        paths.setSkippedUpdateVersion(version);
        reject();
    });
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    const QString pageUrl = release.pageUrl;
    connect(download, &QPushButton::clicked, this, [this, pageUrl] {
        QDesktopServices::openUrl(QUrl(pageUrl));
        accept();
    });
}

void UpdateDialog::askForConsent(AppPaths &paths, QWidget *parent)
{
    QMessageBox box(parent);
    box.setWindowTitle(tr("Check for updates?"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("Should %1 check for new versions at startup?").arg(appName()));
    box.setInformativeText(
        tr("It only asks GitHub for the latest version number. No data about you, your saves "
           "or your players is sent, and nothing is installed automatically. You can change "
           "this at any time under Settings → Saves."));
    QPushButton *yes = box.addButton(tr("Check for updates"), QMessageBox::AcceptRole);
    QPushButton *no = box.addButton(tr("Don't check"), QMessageBox::RejectRole);
    box.setDefaultButton(yes);
    box.setEscapeButton(no);
    box.exec();
    paths.setUpdateCheckEnabled(box.clickedButton() == yes);
}

} // namespace fm
