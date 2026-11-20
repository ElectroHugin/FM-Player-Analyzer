#include "WhatsNewDialog.h"

#include "AppPaths.h"
#include "core/UpdateCheck.h"
#include "core/Version.h"

#include <QCheckBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fm {

namespace {

QString readResource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

QString WhatsNewDialog::changelogFileName(const QString &language)
{
    return language.isEmpty() || language == QLatin1String("en")
               ? QStringLiteral("CHANGELOG.md")
               : QStringLiteral("CHANGELOG.%1.md").arg(language);
}

QString WhatsNewDialog::notes(const QString &language, const QString &fromVersion,
                              const QString &toVersion)
{
    if (language != QLatin1String("en")) {
        const QString translated = changelogNotes(
            readResource(QStringLiteral(":/changelog/") + changelogFileName(language)),
            fromVersion, toVersion);
        if (!translated.isEmpty())
            return translated;
    }
    return changelogNotes(readResource(QStringLiteral(":/changelog/CHANGELOG.md")), fromVersion,
                          toVersion);
}

void WhatsNewDialog::showAfterUpdate(AppPaths &paths, const QString &language, QWidget *parent)
{
    const QString running = appVersion();
    const QString last = paths.lastStartedVersion();
    if (last == running)
        return;
    paths.setLastStartedVersion(running);
    const std::optional<AppVersion> now = AppVersion::parse(running);
    const std::optional<AppVersion> before = AppVersion::parse(last);
    if (!now || (before && compareVersions(*now, *before) <= 0))
        return; // an older version started again: nothing new to tell
    if (!paths.showWhatsNew())
        return;
    const QString text = notes(language, last, running);
    if (text.isEmpty())
        return;
    WhatsNewDialog dialog(text, paths, /*withOptOut*/ true, parent);
    dialog.exec();
}

WhatsNewDialog::WhatsNewDialog(const QString &notes, AppPaths &paths, bool withOptOut,
                               QWidget *parent)
    : QDialog(parent)
    , m_paths(paths)
{
    setWindowTitle(tr("What's new"));
    resize(560, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("New in version %1").arg(appVersion()), this);
    title->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(title);

    auto *browser = new QTextBrowser(this);
    // Changelog entries are shown as text only; links in them are not followed.
    browser->setOpenLinks(false);
    if (notes.trimmed().isEmpty())
        browser->setPlainText(tr("No release notes for this version."));
    else
        browser->setMarkdown(notes);
    layout->addWidget(browser, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    if (withOptOut) {
        m_optOut = new QCheckBox(tr("Don't show again after updates"), this);
        m_optOut->setToolTip(tr("Help → What's new still shows the changes. Can be switched "
                                "back on under Settings → Saves → Updates."));
        buttons->addWidget(m_optOut);
    }
    buttons->addStretch(1);
    auto *close = new QPushButton(tr("Close"), this);
    close->setObjectName(QStringLiteral("headerPrimary"));
    close->setDefault(true);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(this, &QDialog::finished, this, [this] {
        if (m_optOut && m_optOut->isChecked())
            m_paths.setShowWhatsNew(false);
    });
}

} // namespace fm
