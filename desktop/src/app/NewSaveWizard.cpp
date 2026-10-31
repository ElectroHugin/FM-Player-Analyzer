#include "NewSaveWizard.h"

#include "AppContext.h"
#include "ImportRunner.h"
#include "SaveLibrary.h"
#include "core/Constants.h"
#include "core/Database.h"
#include "widgets/DropZone.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fm {

namespace {

enum Step { NameStep = 0, ImportStep = 1, ClubStep = 2 };

// A collapsible explanation: a small arrow button that shows/hides the text.
QWidget *foldableHint(const QString &title, const QString &text, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *toggle = new QToolButton(box);
    toggle->setObjectName(QStringLiteral("foldButton"));
    toggle->setText(title);
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setCursor(Qt::PointingHandCursor);
    auto *body = new QLabel(text, box);
    body->setObjectName(QStringLiteral("kpiCaption"));
    body->setWordWrap(true);
    body->hide();
    layout->addWidget(toggle, 0, Qt::AlignLeft);
    layout->addWidget(body);
    QObject::connect(toggle, &QToolButton::toggled, body, [toggle, body](bool on) {
        toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(on);
    });
    return box;
}

} // namespace

NewSaveWizard::NewSaveWizard(AppContext &context, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
{
    setWindowTitle(tr("Neuer Spielstand"));
    setMinimumSize(720, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 22, 28, 18);
    layout->setSpacing(18);

    // Step indicator.
    auto *steps = new QHBoxLayout;
    steps->setSpacing(10);
    const QStringList stepNames = {tr("Spielstand"), tr("Erster Import"), tr("Verein")};
    for (int i = 0; i < stepNames.size(); ++i) {
        if (i > 0) {
            auto *line = new QFrame(this);
            line->setObjectName(QStringLiteral("stepLine"));
            line->setFixedHeight(2);
            line->setMinimumWidth(30);
            steps->addWidget(line, 1);
        }
        auto *label = new QLabel(QStringLiteral("%1  %2").arg(i + 1).arg(stepNames.at(i)), this);
        label->setObjectName(QStringLiteral("stepLabel"));
        m_stepLabels.append(label);
        steps->addWidget(label);
    }
    layout->addLayout(steps);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildNamePage());
    m_pages->addWidget(buildImportPage());
    m_pages->addWidget(buildClubPage());
    layout->addWidget(m_pages, 1);

    auto *footer = new QHBoxLayout;
    m_cancelButton = new QPushButton(tr("Abbrechen"), this);
    m_skipButton = new QPushButton(tr("Überspringen"), this);
    m_nextButton = new QPushButton(tr("Weiter"), this);
    m_nextButton->setDefault(true);
    footer->addWidget(m_cancelButton);
    footer->addStretch(1);
    footer->addWidget(m_skipButton);
    footer->addWidget(m_nextButton);
    layout->addLayout(footer);

    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        // After step 1 the save exists and is open: closing keeps it.
        if (m_createdName.isEmpty())
            reject();
        else
            accept();
    });
    connect(m_skipButton, &QPushButton::clicked, this, &NewSaveWizard::skip);
    connect(m_nextButton, &QPushButton::clicked, this, &NewSaveWizard::next);

    goTo(NameStep);
    m_nameEdit->setFocus();
}

QWidget *NewSaveWizard::buildNamePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *nameLabel = new QLabel(tr("Name"), page);
    nameLabel->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(nameLabel);
    m_nameEdit = new QLineEdit(page);
    m_nameEdit->setPlaceholderText(tr("z. B. Bayern 2027"));
    m_nameEdit->setMaxLength(80);
    layout->addWidget(m_nameEdit);
    m_nameError = new QLabel(page);
    m_nameError->setObjectName(QStringLiteral("errorText"));
    m_nameError->setWordWrap(true);
    m_nameError->hide();
    layout->addWidget(m_nameError);
    auto *nameHint =
        new QLabel(tr("Nur für dich zum Wiedererkennen, z. B. Verein und Startjahr."), page);
    nameHint->setObjectName(QStringLiteral("kpiCaption"));
    layout->addWidget(nameHint);
    connect(m_nameEdit, &QLineEdit::textEdited, m_nameError, &QLabel::hide);

    layout->addSpacing(14);
    auto *versionLabel = new QLabel(tr("Football-Manager-Version"), page);
    versionLabel->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(versionLabel);

    auto *cards = new QHBoxLayout;
    cards->setSpacing(10);
    m_versionGroup = new QButtonGroup(this);
    m_versionGroup->setExclusive(true);
    int index = 0;
    for (const FmDataVersion &version : fmDataVersions()) {
        if (!version.supported)
            continue;
        QStringList formats;
        for (const QString &format : version.importFormats)
            formats << format.toUpper();
        auto *card = new QPushButton(
            QStringLiteral("%1\n%2")
                .arg(version.displayName,
                     tr("Import: %1").arg(formats.join(QStringLiteral(", ")))),
            page);
        card->setObjectName(QStringLiteral("choiceCard"));
        card->setCheckable(true);
        card->setProperty("versionId", version.id);
        card->setCursor(Qt::PointingHandCursor);
        card->setChecked(version.id == newestFmVersionId());
        m_versionGroup->addButton(card, index++);
        cards->addWidget(card, 1);
    }
    layout->addLayout(cards);

    layout->addSpacing(6);
    layout->addWidget(foldableHint(
        tr("Warum lässt sich die Version später nicht ändern?"),
        tr("Rollen, Bewertungen und Verlauf eines Spielstands beziehen sich auf die Rollen "
           "seiner FM-Version. Für einen anderen Football Manager legst du deshalb einen "
           "eigenen Spielstand an. Deine bisherigen Spielstände bleiben unverändert."),
        page));
    layout->addStretch(1);
    return page;
}

QWidget *NewSaveWizard::buildImportPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *intro = new QLabel(
        tr("Ziehe einen Spieler-Export aus dem Football Manager hierher, zum Beispiel deinen "
           "Kader oder eine Scouting-Liste. Weitere Exporte kannst du jederzeit später "
           "importieren."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_dropZone = new DropZone(page);
    layout->addWidget(m_dropZone, 1);
    connect(m_dropZone, &DropZone::fileChosen, this, [this](const QString &path) {
        m_importFile = path;
        m_importResult->hide();
        m_nextButton->setText(tr("Importieren"));
    });

    m_autoAssignCheck =
        new QCheckBox(tr("Neuen Spielern automatisch passende Rollen zuweisen"), page);
    m_autoAssignCheck->setChecked(true);
    layout->addWidget(m_autoAssignCheck);

    m_importResult = new QLabel(page);
    m_importResult->setWordWrap(true);
    m_importResult->hide();
    layout->addWidget(m_importResult);
    return page;
}

QWidget *NewSaveWizard::buildClubPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("Welchen Verein managst du?"), page);
    label->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(label);
    m_clubCombo = new QComboBox(page);
    m_clubCombo->setEditable(true);
    m_clubCombo->setInsertPolicy(QComboBox::NoInsert);
    m_clubCombo->completer()->setFilterMode(Qt::MatchContains);
    m_clubCombo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    layout->addWidget(m_clubCombo);
    m_clubHint = new QLabel(
        tr("Damit kennt der Analyzer deinen Kader: Übersicht, Best XI, Kaufberatung und "
           "Trainingsplan rechnen mit diesem Verein. Logo, Zweitteam und Nationalteam "
           "stellst du später in den Einstellungen ein."),
        page);
    m_clubHint->setObjectName(QStringLiteral("kpiCaption"));
    m_clubHint->setWordWrap(true);
    layout->addWidget(m_clubHint);
    layout->addSpacing(10);
    m_importSummary = new QLabel(page);
    m_importSummary->setObjectName(QStringLiteral("successNote"));
    m_importSummary->setWordWrap(true);
    m_importSummary->hide();
    layout->addWidget(m_importSummary);
    layout->addStretch(1);
    return page;
}

void NewSaveWizard::goTo(int step)
{
    m_pages->setCurrentIndex(step);
    for (int i = 0; i < m_stepLabels.size(); ++i) {
        QLabel *label = m_stepLabels.at(i);
        label->setProperty("state", i < step    ? QStringLiteral("done")
                                    : i == step ? QStringLiteral("current")
                                                : QStringLiteral("todo"));
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
    switch (step) {
    case NameStep:
        m_skipButton->hide();
        m_nextButton->setText(tr("Weiter"));
        break;
    case ImportStep:
        m_cancelButton->setText(tr("Später einrichten"));
        m_skipButton->show();
        m_nextButton->setText(m_importFile.isEmpty() ? tr("Weiter") : tr("Importieren"));
        m_dropZone->setStartDir(m_context.htmlExportDir());
        break;
    case ClubStep:
        m_skipButton->show();
        m_nextButton->setText(tr("Fertig"));
        fillClubs();
        m_clubCombo->setFocus();
        break;
    }
}

void NewSaveWizard::next()
{
    switch (m_pages->currentIndex()) {
    case NameStep:
        if (createSave())
            goTo(ImportStep);
        break;
    case ImportStep:
        if (m_importFile.isEmpty())
            skip();
        else
            startImport();
        break;
    case ClubStep:
        finish();
        break;
    }
}

void NewSaveWizard::skip()
{
    if (m_importRunning)
        return;
    if (m_pages->currentIndex() == ImportStep && m_context.store().size() > 0)
        goTo(ClubStep);
    else
        accept(); // no players yet: nothing to pick a club from
}

bool NewSaveWizard::createSave()
{
    const QString name = m_nameEdit->text().trimmed();
    QString error = SaveLibrary::validateName(m_context, name);
    if (error.isEmpty()) {
        const QString versionId =
            m_versionGroup->checkedButton()
                ? m_versionGroup->checkedButton()->property("versionId").toString()
                : newestFmVersionId();
        if (m_context.createDatabase(name, versionId, &error))
            m_createdName = name;
    }
    if (m_createdName.isEmpty()) {
        m_nameError->setText(error);
        m_nameError->show();
        m_nameEdit->setFocus();
        return false;
    }
    return true;
}

void NewSaveWizard::startImport()
{
    if (m_importRunning)
        return;
    m_importRunning = true;
    m_nextButton->setEnabled(false);
    m_skipButton->setEnabled(false);
    runImportPipeline(m_context, this, m_importFile, m_autoAssignCheck->isChecked(),
                      [this](const ImportPipelineResult &result) {
                          m_importRunning = false;
                          m_nextButton->setEnabled(true);
                          m_skipButton->setEnabled(true);
                          if (!result.import.success) {
                              m_importResult->setObjectName(QStringLiteral("errorText"));
                              m_importResult->setText(
                                  tr("Die Datei konnte nicht importiert werden: %1")
                                      .arg(importErrorText(result.import)));
                              m_importResult->style()->unpolish(m_importResult);
                              m_importResult->style()->polish(m_importResult);
                              m_importResult->show();
                              return;
                          }
                          m_importFile.clear();
                          m_dropZone->setFile({});
                          goTo(ClubStep);
                          m_importSummary->setText(
                              importSummaryLines(result).join(QLatin1Char('\n')));
                          m_importSummary->show();
                      });
}

void NewSaveWizard::fillClubs()
{
    // Most represented clubs first: a squad export makes the user's club the top entry.
    QHash<QString, int> counts;
    for (const Player &player : m_context.store().players()) {
        if (!player.club.isEmpty())
            ++counts[player.club];
    }
    QStringList clubs = counts.keys();
    std::sort(clubs.begin(), clubs.end(), [&counts](const QString &a, const QString &b) {
        const int ca = counts.value(a);
        const int cb = counts.value(b);
        return ca != cb ? ca > cb : a.localeAwareCompare(b) < 0;
    });
    m_clubCombo->clear();
    m_clubCombo->addItems(clubs);
    m_clubCombo->setCurrentIndex(clubs.isEmpty() ? -1 : 0);
}

void NewSaveWizard::finish()
{
    const QString club = m_clubCombo->currentText().trimmed();
    if (!club.isEmpty()) {
        m_context.database().setSetting(QStringLiteral("user_club"), club);
        m_context.notifySettingsChanged();
    }
    accept();
}

} // namespace fm
