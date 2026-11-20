#include "MigrationWizard.h"

#include "AppContext.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <atomic>

namespace fm {

MigrationWizard::MigrationWizard(AppContext &context, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
{
    setWindowTitle(tr("Import Legacy Database"));
    setMinimumSize(640, 480);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Imports a database from the old Streamlit version into the new format.\n"
           "The original file is not modified."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    auto *sourceRow = new QHBoxLayout;
    m_sourceEdit = new QLineEdit(this);
    m_sourceEdit->setPlaceholderText(tr("Path to the old .db file…"));
    auto *browse = new QPushButton(tr("Browse…"), this);
    sourceRow->addWidget(m_sourceEdit, 1);
    sourceRow->addWidget(browse);
    form->addRow(tr("Legacy Database:"), sourceRow);

    m_targetNameEdit = new QLineEdit(this);
    m_targetNameEdit->setPlaceholderText(tr("Name of the new database (without .db)"));
    form->addRow(tr("Target Name:"), m_targetNameEdit);

    m_importExtras = new QCheckBox(
        tr("Import config.ini, definitions.json, logo and flag from the legacy folder"),
        this);
    m_importExtras->setChecked(true);
    form->addRow(QString(), m_importExtras);
    layout->addLayout(form);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    layout->addWidget(m_log, 1);

    auto *buttons = new QHBoxLayout;
    m_startButton = new QPushButton(tr("Start Import"), this);
    m_startButton->setDefault(true);
    m_closeButton = new QPushButton(tr("Close"), this);
    buttons->addStretch(1);
    buttons->addWidget(m_startButton);
    buttons->addWidget(m_closeButton);
    layout->addLayout(buttons);

    connect(browse, &QPushButton::clicked, this, &MigrationWizard::browseSource);
    connect(m_startButton, &QPushButton::clicked, this, &MigrationWizard::startMigration);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(&m_watcher, &QFutureWatcher<MigrationStats>::finished, this,
            &MigrationWizard::migrationFinished);
}

void MigrationWizard::browseSource()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Choose legacy database"), QString(),
        tr("SQLite databases (*.db);;All files (*)"));
    if (file.isEmpty())
        return;
    m_sourceEdit->setText(QDir::toNativeSeparators(file));
    if (m_targetNameEdit->text().isEmpty())
        m_targetNameEdit->setText(QFileInfo(file).completeBaseName());
}

void MigrationWizard::startMigration()
{
    const QString source = QDir::fromNativeSeparators(m_sourceEdit->text().trimmed());
    const QString targetName = m_targetNameEdit->text().trimmed();
    if (source.isEmpty() || !QFile::exists(source)) {
        QMessageBox::warning(this, windowTitle(), tr("Please choose an existing .db file."));
        return;
    }
    if (targetName.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Please provide a target name."));
        return;
    }
    const QString targetPath = m_context.paths().databaseFile(targetName);
    if (QFile::exists(targetPath)
        && QMessageBox::question(this, windowTitle(),
                                 tr("The database '%1' already exists. Overwrite?")
                                     .arg(targetName))
               != QMessageBox::Yes) {
        return;
    }

    m_startButton->setEnabled(false);
    m_closeButton->setEnabled(false);
    m_log->appendPlainText(tr("Importing %1 …").arg(source));

    // Progress updates arrive from the worker thread; forward via queued call.
    auto progressHandler = [this](qint64 current, qint64 total) {
        const int percent = total > 0 ? static_cast<int>(current * 100 / total) : 0;
        QMetaObject::invokeMethod(m_progress, [this, percent] { m_progress->setValue(percent); },
                                  Qt::QueuedConnection);
    };

    m_migratedDbName = targetName;
    m_watcher.setFuture(QtConcurrent::run([source, targetPath, progressHandler] {
        LegacyMigrator migrator;
        return migrator.migrate(source, targetPath, progressHandler);
    }));
}

void MigrationWizard::reject()
{
    if (!m_watcher.isRunning())
        QDialog::reject();
}

void MigrationWizard::closeEvent(QCloseEvent *event)
{
    if (m_watcher.isRunning()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void MigrationWizard::migrationFinished()
{
    const MigrationStats stats = m_watcher.result();
    m_startButton->setEnabled(true);
    m_closeButton->setEnabled(true);

    if (!stats.success) {
        m_progress->setValue(0);
        m_log->appendPlainText(tr("ERROR: %1").arg(stats.error));
        m_migratedDbName.clear();
        return;
    }

    m_progress->setValue(100);
    m_log->appendPlainText(tr("Done: %1 players, %2 rating entries imported (%3 "
                              "orphaned skipped).")
                               .arg(stats.playersMigrated)
                               .arg(stats.ratingsMigrated)
                               .arg(stats.orphanedRatings));
    if (stats.nationalSquadMigrated || stats.shortlistMigrated)
        m_log->appendPlainText(tr("National squad: %1, shortlist: %2 players.")
                                   .arg(stats.nationalSquadMigrated)
                                   .arg(stats.shortlistMigrated));
    for (const QString &note : stats.coercions)
        m_log->appendPlainText(QStringLiteral("  • ") + note);

    // Optional extras: legacy config.ini + definitions.json + assets, taken
    // from the legacy folder layout relative to the .db file
    // (…/databases/x.db -> …/config/).
    if (m_importExtras->isChecked()) {
        QDir legacyDir =
            QFileInfo(QDir::fromNativeSeparators(m_sourceEdit->text().trimmed())).dir();
        legacyDir.cdUp(); // databases/ -> legacy root
        const QString legacyConfigDir = legacyDir.filePath(QStringLiteral("config"));

        const auto copyIfExists = [&](const QString &from, const QString &to) {
            if (QFile::exists(from)) {
                QFile::remove(to);
                if (QFile::copy(from, to)) {
                    QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner);
                    m_log->appendPlainText(tr("Imported: %1").arg(QFileInfo(from).fileName()));
                }
            }
        };
        copyIfExists(QDir(legacyConfigDir).filePath(QStringLiteral("config.ini")),
                     m_context.paths().configFile());
        // The legacy app only knew FM24 (= the version of unmarked databases).
        copyIfExists(QDir(legacyConfigDir).filePath(QStringLiteral("definitions.json")),
                     m_context.paths().definitionsFile(defaultFmVersionId()));
        const QString assetsDir = QDir(legacyConfigDir).filePath(QStringLiteral("assets"));
        copyIfExists(QDir(assetsDir).filePath(QStringLiteral("logo.png")),
                     QDir(m_context.paths().assetsDir()).filePath(QStringLiteral("logo.png")));
        copyIfExists(QDir(assetsDir).filePath(QStringLiteral("flag.png")),
                     QDir(m_context.paths().assetsDir()).filePath(QStringLiteral("flag.png")));

        // The files on disk changed under the running app — reload them.
        if (!m_context.reloadConfigAndDefinitions()) {
            m_log->appendPlainText(
                tr("WARNING: The definitions could not be loaded after the import "
                   "(%1). The previous definitions remain active.")
                    .arg(m_context.definitions().errorString()));
        }
    }

    m_log->appendPlainText(tr("Import complete. The database '%1' can now be "
                              "selected.")
                               .arg(m_migratedDbName));
}

} // namespace fm
