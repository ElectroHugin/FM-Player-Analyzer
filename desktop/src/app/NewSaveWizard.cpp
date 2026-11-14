#include "NewSaveWizard.h"

#include "AppContext.h"
#include "ImportRunner.h"
#include "SaveLibrary.h"
#include "core/AppConfig.h"
#include "core/Constants.h"
#include "core/Database.h"
#include "widgets/DropZone.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFileInfo>
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

bool isSaveFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("fm"), Qt::CaseInsensitive) == 0;
}

// The club and the other first settings follow in the welcome assistant
// (WelcomeWizard), which opens once the save holds players.
enum Step { NameStep = 0, ImportStep = 1 };

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
    const QStringList stepNames = {tr("Spielstand"), tr("Erster Import")};
    for (int i = 0; i < stepNames.size(); ++i) {
        if (i > 0) {
            auto *line = new QFrame(this);
            line->setObjectName(QStringLiteral("stepLine"));
            line->setFixedHeight(2);
            line->setMinimumWidth(30);
            steps->addWidget(line, 1);
        }
        auto *number = new QLabel(QString::number(i + 1), this);
        number->setObjectName(QStringLiteral("stepNumber"));
        number->setAlignment(Qt::AlignCenter);
        auto *label = new QLabel(stepNames.at(i), this);
        label->setObjectName(QStringLiteral("stepLabel"));
        m_stepLabels.append(number);
        m_stepLabels.append(label);
        steps->addWidget(number);
        steps->addWidget(label);
    }
    layout->addLayout(steps);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildNamePage());
    m_pages->addWidget(buildImportPage());
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
    m_nameEdit->setPlaceholderText(tr("z. B. Mein Verein 2027"));
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
        auto *card = new QPushButton(page);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);
        cardLayout->setSpacing(2);
        auto *cardTitle = new QLabel(SaveLibrary::shortVersionLabel(version.id), card);
        cardTitle->setObjectName(QStringLiteral("choiceTitle"));
        auto *cardText = new QLabel(
            QStringLiteral("%1\n%2").arg(version.displayName,
                                          tr("Import: %1").arg(formats.join(QStringLiteral(", ")))),
            card);
        cardText->setObjectName(QStringLiteral("kpiCaption"));
        cardLayout->addWidget(cardTitle);
        cardLayout->addWidget(cardText);
        for (QLabel *label : {cardTitle, cardText})
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        card->setMinimumHeight(cardLayout->sizeHint().height());
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

    // Each save has its own profile; a new one starts clean or as a copy.
    layout->addSpacing(14);
    auto *profileLabel = new QLabel(tr("Einstellungen"), page);
    profileLabel->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(profileLabel);
    m_profileSourceCombo = new QComboBox(page);
    m_profileSourceCombo->addItem(tr("Standardeinstellungen"), QString());
    for (const QString &save : m_context.availableDatabases())
        m_profileSourceCombo->addItem(tr("Übernehmen von „%1“").arg(save), save);
    layout->addWidget(m_profileSourceCombo);
    auto *profileHint = new QLabel(
        tr("Übernimmt Gewichte, Farben, Analyse-Optionen, Rollen und Taktiken. Verein, "
           "Logo, Spieler, Favoriten und den live verfolgten FM27-Spielstand bringt der neue "
           "Spielstand selbst mit."),
        page);
    profileHint->setObjectName(QStringLiteral("kpiCaption"));
    profileHint->setWordWrap(true);
    layout->addWidget(profileHint);
    // Only a choice once another save exists to copy from; the very first
    // save always starts with the defaults.
    const bool canCopy = m_profileSourceCombo->count() > 1;
    for (QWidget *widget : {static_cast<QWidget *>(profileLabel),
                            static_cast<QWidget *>(m_profileSourceCombo),
                            static_cast<QWidget *>(profileHint)})
        widget->setVisible(canCopy);
    layout->addStretch(1);
    return page;
}

QWidget *NewSaveWizard::buildImportPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    m_importIntro = new QLabel(page);
    m_importIntro->setWordWrap(true);
    layout->addWidget(m_importIntro);

    m_dropZone = new DropZone(page);
    layout->addWidget(m_dropZone, 1);
    connect(m_dropZone, &DropZone::fileChosen, this, [this](const QString &path) {
        m_importFile = path;
        m_importResult->hide();
        m_nextButton->setText(tr("Importieren"));
        m_followCheck->setVisible(isSaveFile(path));
    });

    // A save game read in here can be followed right away (Settings → Import).
    m_followCheck = new QCheckBox(tr("Spielstand live verfolgen"), page);
    m_followCheck->setChecked(true);
    m_followCheck->setToolTip(tr("Solange der Analyzer offen ist, liest er den Spielstand nach "
                                 "jedem Speichern im Spiel neu ein, auch nach einem Autosave."));
    m_followCheck->hide();
    layout->addWidget(m_followCheck);

    m_autoAssignCheck =
        new QCheckBox(tr("Neuen Spielern automatisch passende Rollen zuweisen"), page);
    m_autoAssignCheck->setChecked(true);
    layout->addWidget(m_autoAssignCheck);

    m_importResult = new QLabel(page);
    m_importResult->setWordWrap(true);
    m_importResult->hide();
    layout->addWidget(m_importResult);

    m_exportHint = new QLabel(page);
    m_exportHint->setObjectName(QStringLiteral("kpiCaption"));
    m_exportHint->setWordWrap(true);
    layout->addWidget(m_exportHint);
    return page;
}

void NewSaveWizard::goTo(int step)
{
    m_pages->setCurrentIndex(step);
    for (int i = 0; i < m_stepLabels.size(); ++i) {
        QLabel *label = m_stepLabels.at(i);
        const int labelStep = i / 2; // number + name per step
        label->setProperty("state", labelStep < step    ? QStringLiteral("done")
                                    : labelStep == step ? QStringLiteral("current")
                                                        : QStringLiteral("todo"));
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
    switch (step) {
    case NameStep:
        m_skipButton->hide();
        m_nextButton->setText(tr("Weiter"));
        break;
    case ImportStep: {
        m_cancelButton->setText(tr("Später einrichten"));
        m_skipButton->hide(); // "Fertig" without a file does the same
        m_nextButton->setText(m_importFile.isEmpty() ? tr("Fertig") : tr("Importieren"));
        // A version that reads save games starts there: the save is the
        // fastest and most complete source; exports stay possible.
        const QString versionId = m_context.fmVersionId();
        const bool readsSaves = fmDataVersion(versionId).importFormats.contains(QStringLiteral("fm"));
        if (m_stepLabels.size() > 3)
            m_stepLabels.at(3)->setText(readsSaves ? tr("Spielstand einlesen") : tr("Erster Import"));
        if (readsSaves) {
            m_importIntro->setText(tr("Wähle deine Spielstand-Datei aus Football Manager. Der "
                                      "Analyzer liest sie direkt, auch große Datenbanken schnell."));
            m_dropZone->setPrompt(tr("Spielstand hierher ziehen"),
                                  tr("%1-Spielstand (.fm) – oder klicken, um ihn auszuwählen")
                                      .arg(SaveLibrary::shortVersionLabel(versionId)));
            m_dropZone->setDialog(tr("Spielstand auswählen"),
                                  tr("Spielstände und Exporte (*.fm *.csv *.html *.htm);;Alle Dateien (*)"));
            m_dropZone->setStartDir(AppConfig::defaultSavesDir(versionId));
            m_exportHint->setText(tr("Optional. Exporte aus dem FM View Exporter (CSV oder HTML) "
                                     "kannst du ebenfalls hier einlesen."));
        } else {
            m_importIntro->setText(tr("Ziehe einen Spieler-Export aus dem Football Manager "
                                      "hierher, zum Beispiel deinen Kader oder eine "
                                      "Scouting-Liste. Weitere Exporte kannst du jederzeit "
                                      "später importieren."));
            m_dropZone->setPrompt({}, {});
            m_dropZone->setDialog({}, {});
            m_dropZone->setStartDir(m_context.htmlExportDir());
            m_exportHint->clear();
        }
        m_exportHint->setVisible(readsSaves);
        m_dropZone->setFile(m_importFile);
        m_followCheck->setVisible(isSaveFile(m_importFile));
        break;
    }
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
    }
}

void NewSaveWizard::skip()
{
    if (m_importRunning)
        return;
    accept();
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
        if (m_context.createDatabase(name, versionId,
                                     m_profileSourceCombo->currentData().toString(), &error))
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
                          if (isSaveFile(m_importFile) && m_followCheck->isChecked())
                              m_context.config().followSave(
                                  m_importFile,
                                  QFileInfo(m_importFile).lastModified().toMSecsSinceEpoch());
                          // Done: the welcome assistant picks up with the club.
                          accept();
                      });
}

} // namespace fm
