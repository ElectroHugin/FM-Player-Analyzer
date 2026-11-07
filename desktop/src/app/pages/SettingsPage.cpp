#include "SettingsPage.h"
#include "core/ImportHistory.h"

#include "../AppContext.h"
#include "../MigrationWizard.h"
#include "../NewSaveWizard.h"
#include "../theming/ThemeManager.h"
#include "../theming/ThemePresets.h"
#include "core/Constants.h"
#include "core/Utils.h"

#include <QBuffer>
#include <QCheckBox>
#include <QDateEdit>
#include <QColorDialog>
#include <QComboBox>
#include <QStandardItemModel>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QFileInfo>
#include <QLocale>
#include <QPixmap>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSet>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace fm {

namespace {

// An image as PNG bytes for the save's profile (large pictures are scaled
// down: the header shows them at 34 px, the start screen at 64 px).
QByteArray pngBytes(QPixmap pixmap)
{
    if (pixmap.width() > 512 || pixmap.height() > 512)
        pixmap = pixmap.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return bytes;
}

const QStringList &weightCategoryOrder()
{
    static const QStringList order = {
        QStringLiteral("Extremely Important"), QStringLiteral("Important"),
        QStringLiteral("Good"), QStringLiteral("Decent"), QStringLiteral("Almost Irrelevant")};
    return order;
}

const QStringList &gkWeightCategoryOrder()
{
    static const QStringList order = {
        QStringLiteral("Top Importance"), QStringLiteral("High Importance"),
        QStringLiteral("Medium Importance"), QStringLiteral("Key"),
        QStringLiteral("Preferable"), QStringLiteral("Other")};
    return order;
}

// Color swatch button style with a readable label on top of the color.
QString colorButtonStyle(const QColor &color)
{
    const QString textColor =
        contrastRatio(color, QColorConstants::White) >= 3.0 ? QStringLiteral("#ffffff")
                                                            : QStringLiteral("#000000");
    return QStringLiteral("background-color:%1; color:%2; border:1px solid rgba(128,128,128,0.5);"
                          " border-radius:6px; padding:5px;")
        .arg(color.name(), textColor);
}

// Fixed control widths, so the fields of a card form one clean column.
constexpr int kFieldWidth = 320;   // combos, names
constexpr int kPathWidth = 280;    // folder and file paths (plus buttons)
constexpr int kShortWidth = 96;    // three-letter codes, ids
constexpr int kNumberWidth = 112;  // spin boxes
constexpr int kLabelWidth = 200;   // label column of a card
constexpr int kSwatchWidth = 136;  // color swatches

template <typename W>
W *sized(W *widget, int width)
{
    widget->setFixedWidth(width);
    return widget;
}

// A card: quiet raised surface with a hairline, title, optional one-line
// subtitle, then the body (usually a form).
QFrame *makeCard(const QString &title, const QString &subtitle, QVBoxLayout **body)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("settingsCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 20);
    layout->setSpacing(12);
    if (!title.isEmpty()) {
        auto *head = new QVBoxLayout;
        head->setSpacing(2);
        auto *titleLabel = new QLabel(title, card);
        titleLabel->setObjectName(QStringLiteral("cardTitle"));
        head->addWidget(titleLabel);
        if (!subtitle.isEmpty()) {
            auto *sub = new QLabel(subtitle, card);
            sub->setObjectName(QStringLiteral("kpiCaption"));
            sub->setWordWrap(true);
            head->addWidget(sub);
        }
        layout->addLayout(head);
    }
    *body = layout;
    return card;
}

// Two-column form inside a card: fixed label column, controls at their size.
QFormLayout *makeForm(QVBoxLayout *body, int labelWidth = kLabelWidth)
{
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setProperty("labelWidth", labelWidth);
    body->addLayout(form);
    return form;
}

QLabel *rowLabel(const QString &text, QFormLayout *form)
{
    auto *label = new QLabel(text);
    label->setFixedWidth(form->property("labelWidth").toInt());
    label->setWordWrap(true);
    return label;
}

void addRow(QFormLayout *form, const QString &label, QWidget *field)
{
    form->addRow(rowLabel(label, form), field);
}

void addRow(QFormLayout *form, const QString &label, QLayout *field)
{
    auto *holder = new QWidget;
    field->setContentsMargins(0, 0, 0, 0);
    holder->setLayout(field);
    form->addRow(rowLabel(label, form), holder);
}

QHBoxLayout *hbox(std::initializer_list<QWidget *> widgets, bool stretch = true)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    for (QWidget *w : widgets)
        row->addWidget(w);
    if (stretch)
        row->addStretch(1);
    return row;
}

// A folded explanation: a quiet "Explanation" toggle, the text below it
// only when opened.
QWidget *foldableHint(const QString &text)
{
    auto *box = new QWidget;
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *toggle = new QToolButton(box);
    toggle->setObjectName(QStringLiteral("foldButton"));
    toggle->setText(QCoreApplication::translate("fm::SettingsPage", "Erklärung"));
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setCursor(Qt::PointingHandCursor);
    auto *body = new QLabel(text, box);
    body->setObjectName(QStringLiteral("kpiCaption"));
    body->setWordWrap(true);
    body->setMaximumWidth(620);
    body->hide();
    layout->addWidget(toggle, 0, Qt::AlignLeft);
    layout->addWidget(body);
    QObject::connect(toggle, &QToolButton::toggled, body, [toggle, body](bool on) {
        toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(on);
    });
    return box;
}

// Explanation below the controls of a form (in the field column).
void addHint(QFormLayout *form, const QString &text)
{
    form->addRow(QString(), foldableHint(text));
}

QLabel *caption(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("kpiCaption"));
    label->setWordWrap(true);
    return label;
}

// A page: title, one-line description, then cards; content width capped so
// forms do not drift apart on wide windows.
QWidget *makePage(const QString &title, const QString &subtitle, QVBoxLayout **cards)
{
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("settingsContent"));
    auto *outer = new QHBoxLayout(content);
    outer->setContentsMargins(32, 24, 32, 32);
    auto *column = new QWidget(content);
    column->setMaximumWidth(860);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *titleLabel = new QLabel(title, column);
    titleLabel->setObjectName(QStringLiteral("settingsPageTitle"));
    layout->addWidget(titleLabel);
    if (!subtitle.isEmpty()) {
        auto *sub = new QLabel(subtitle, column);
        sub->setObjectName(QStringLiteral("kpiCaption"));
        sub->setWordWrap(true);
        layout->addWidget(sub);
    }
    layout->addSpacing(4);
    outer->addWidget(column, 1);
    outer->addStretch(0);
    *cards = layout;

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

void finishPage(QVBoxLayout *cards)
{
    cards->addStretch(1);
}

} // namespace

SettingsPage::SettingsPage(AppContext &context, ThemeManager &theme, QWidget *parent)
    : PageBase(context, parent)
    , m_theme(theme)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    auto *navPanel = new QFrame(this);
    navPanel->setObjectName(QStringLiteral("settingsNavPanel"));
    navPanel->setFixedWidth(232);
    auto *navLayout = new QVBoxLayout(navPanel);
    navLayout->setContentsMargins(12, 20, 12, 12);
    navLayout->setSpacing(8);
    auto *heading = new QLabel(tr("Einstellungen"), navPanel);
    heading->setObjectName(QStringLiteral("settingsNavTitle"));
    navLayout->addWidget(heading);
    m_nav = new QListWidget(navPanel);
    m_nav->setObjectName(QStringLiteral("settingsNav"));
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navLayout->addWidget(m_nav, 1);
    body->addWidget(navPanel);

    m_stack = new QStackedWidget(this);
    body->addWidget(m_stack, 1);
    layout->addLayout(body, 1);

    addSection(tr("Spielstand"), tr("Verein"), "Verein", buildClubPage());
    addSection(tr("Spielstand"), tr("Nationalteam"), "Nationalteam", buildNationalPage());
    addSection(tr("Spielstand"), tr("Meldeliste"), "Meldeliste", buildRegistrationPage());
    addSection(tr("Daten"), tr("Import"), "Import", buildImportPage());
    addSection(tr("Daten"), tr("Verlauf und Datenfrische"), "Verlauf und Datenfrische",
               buildHistoryPage());
    addSection(tr("Daten"), tr("Porträts und Wappen"), "Porträts und Wappen", buildGraphicsPage());
    addSection(tr("Berechnung"), tr("DWRS-Gewichte"), "DWRS-Gewichte", buildWeightsPage());
    addSection(tr("Berechnung"), tr("Kader und Analyse"), "Kader und Analyse", buildSquadPage());
    addSection(tr("Berechnung"), tr("Training"), "Training", buildTrainingPage());
    addSection(tr("Programm"), tr("Design"), "Design", buildThemePage());
    addSection(tr("Programm"), tr("Spielstände"), "Spielstände", buildDatabasePage());

    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        const QVariant page = m_nav->item(row) ? m_nav->item(row)->data(Qt::UserRole) : QVariant();
        if (page.isValid())
            m_stack->setCurrentIndex(page.toInt());
    });
    m_nav->setCurrentRow(m_sectionRows.value(QStringLiteral("Verein")));

    // Footer: what the save button affects, discard and save.
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("settingsFooter"));
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(24, 12, 24, 12);
    footerLayout->setSpacing(8);
    m_footerScope = new QLabel(footer);
    m_footerScope->setObjectName(QStringLiteral("kpiCaption"));
    footerLayout->addWidget(m_footerScope, 1);
    auto *discardButton = new QPushButton(tr("Verwerfen"), footer);
    discardButton->setToolTip(tr("Setzt alle nicht gespeicherten Änderungen auf dieser Seite "
                                 "zurück."));
    auto *saveButton = new QPushButton(tr("Speichern"), footer);
    saveButton->setDefault(true);
    saveButton->setMinimumWidth(132);
    footerLayout->addWidget(discardButton);
    footerLayout->addWidget(saveButton);
    layout->addWidget(footer);
    connect(saveButton, &QPushButton::clicked, this, &SettingsPage::saveAll);
    connect(discardButton, &QPushButton::clicked, this, &SettingsPage::refresh);

    refresh();
}

void SettingsPage::addSection(const QString &groupTitle, const QString &title,
                              const char *germanTitle, QWidget *page)
{
    if (groupTitle != m_currentNavGroup) {
        m_currentNavGroup = groupTitle;
        auto *caption = new QListWidgetItem(groupTitle, m_nav);
        caption->setFlags(Qt::NoItemFlags);
        caption->setData(Qt::UserRole + 1, true);
    }
    const int pageIndex = m_stack->addWidget(page);
    auto *item = new QListWidgetItem(title, m_nav);
    item->setData(Qt::UserRole, pageIndex);
    m_sectionRows.insert(QString::fromUtf8(germanTitle), m_nav->row(item));
}

bool SettingsPage::openSettingsSection(const QString &germanTitle)
{
    if (!m_sectionRows.contains(germanTitle))
        return false;
    m_nav->setCurrentRow(m_sectionRows.value(germanTitle));
    return true;
}

QWidget *SettingsPage::buildClubPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Verein"),
                             tr("Dein Verein in diesem Spielstand: Name und Logo für den Kopf "
                                "des Fensters, das Zweitteam und die Lieblings-Taktiken."),
                             &cards);

    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Verein"), QString(), &body));
    QFormLayout *form = makeForm(body);
    m_userClubCombo = sized(new QComboBox, kFieldWidth);
    m_secondClubCombo = sized(new QComboBox, kFieldWidth);
    m_fullClubNameEdit = sized(new QLineEdit, kFieldWidth);
    m_fullClubNameEdit->setPlaceholderText(tr("Wie er im Kopf erscheinen soll"));
    m_stadiumEdit = sized(new QLineEdit, kFieldWidth);
    m_clubCountryEdit = sized(new QLineEdit, kShortWidth);
    m_clubCountryEdit->setMaxLength(3);
    m_clubCountryEdit->setPlaceholderText(QStringLiteral("GER"));
    addRow(form, tr("Mein Verein"), m_userClubCombo);
    addRow(form, tr("Zweitteam"), m_secondClubCombo);
    addRow(form, tr("Voller Vereinsname"), m_fullClubNameEdit);
    addRow(form, tr("Stadion"), m_stadiumEdit);
    addRow(form, tr("Vereins-Land"), m_clubCountryEdit);
    addHint(form, tr("Mein Verein und Zweitteam stammen aus den importierten Spielern. Das "
                     "Zweitteam (zum Beispiel die zweite Mannschaft oder U23) zählt zum eigenen "
                     "Kader, etwa für Leihen und die Jugendelf. Vereinsname und Stadion "
                     "erscheinen im Kopf des Fensters. Das Vereins-Land ist der dreistellige "
                     "Nationen-Code aus dem Spiel; es steuert den Inland-Filter beim Scouting."));

    // Logo (PNG) shown in the header — applied immediately, per save.
    cards->addWidget(makeCard(tr("Vereinslogo"),
                              tr("Erscheint im Kopf des Fensters und auf dem Startbildschirm. "
                                 "Wird sofort übernommen."),
                              &body));
    m_logoPreview = new QLabel;
    m_logoPreview->setObjectName(QStringLiteral("imageWell"));
    m_logoPreview->setFixedSize(96, 64);
    m_logoPreview->setAlignment(Qt::AlignCenter);
    auto *chooseLogoButton = new QPushButton(tr("Bild wählen …"));
    m_logoRemoveButton = new QPushButton(tr("Entfernen"));
    body->addLayout(hbox({m_logoPreview, chooseLogoButton, m_logoRemoveButton}));
    connect(chooseLogoButton, &QPushButton::clicked, this, &SettingsPage::chooseLogo);
    connect(m_logoRemoveButton, &QPushButton::clicked, this, &SettingsPage::removeLogo);

    cards->addWidget(makeCard(tr("Lieblings-Taktiken"), QString(), &body));
    form = makeForm(body);
    m_favTactic1Combo = sized(new QComboBox, kFieldWidth);
    m_favTactic2Combo = sized(new QComboBox, kFieldWidth);
    addRow(form, tr("Primäre Taktik"), m_favTactic1Combo);
    addRow(form, tr("Sekundäre Taktik"), m_favTactic2Combo);
    addHint(form, tr("Die Lieblings-Taktiken werden auf Übersicht, Aufstellung, Kader und "
                     "Transfers vorausgewählt. Die primäre bestimmt die Rollen auf der Seite "
                     "Entwicklung."));

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildNationalPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Nationalteam"),
                             tr("Mit einem Team-Namen wird das National-Management aktiv."),
                             &cards);

    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Nationalteam"), QString(), &body));
    QFormLayout *form = makeForm(body);
    m_natNameEdit = sized(new QLineEdit, kFieldWidth);
    m_natNameEdit->setPlaceholderText(tr("z. B. Nationalteam U21"));
    m_natCodeEdit = sized(new QLineEdit, kShortWidth);
    m_natCodeEdit->setMaxLength(3);
    m_natCodeEdit->setPlaceholderText(QStringLiteral("GER"));
    m_natAgeSpin = sized(new QSpinBox, kNumberWidth);
    m_natAgeSpin->setRange(15, 99);
    m_natAgeSpin->setValue(99);
    m_natAgeSpin->setToolTip(tr("99 = keine Altersgrenze (A-Nationalteam); z. B. 21 für U21."));
    m_natFav1Combo = sized(new QComboBox, kFieldWidth);
    m_natFav2Combo = sized(new QComboBox, kFieldWidth);
    addRow(form, tr("Team-Name"), m_natNameEdit);
    addRow(form, tr("Länder-Code"), m_natCodeEdit);
    addRow(form, tr("Altersgrenze"), hbox({m_natAgeSpin, caption(tr("99 = keine"))}));
    addRow(form, tr("Primäre Taktik"), m_natFav1Combo);
    addRow(form, tr("Sekundäre Taktik"), m_natFav2Combo);

    // National flag (PNG) shown in the header while in national mode, mirroring
    // the club logo. Applied immediately, per save.
    cards->addWidget(makeCard(tr("Flagge"),
                              tr("Erscheint im Kopf des Fensters, solange du das Nationalteam "
                                 "betreust. Wird sofort übernommen."),
                              &body));
    m_flagPreview = new QLabel;
    m_flagPreview->setObjectName(QStringLiteral("imageWell"));
    m_flagPreview->setFixedSize(96, 64);
    m_flagPreview->setAlignment(Qt::AlignCenter);
    auto *chooseFlagButton = new QPushButton(tr("Bild wählen …"));
    m_flagRemoveButton = new QPushButton(tr("Entfernen"));
    body->addLayout(hbox({m_flagPreview, chooseFlagButton, m_flagRemoveButton}));
    connect(chooseFlagButton, &QPushButton::clicked, this, &SettingsPage::chooseFlag);
    connect(m_flagRemoveButton, &QPushButton::clicked, this, &SettingsPage::removeFlag);

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildRegistrationPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Meldeliste"),
                             tr("Regeln, nach denen der Registrierungs-Assistent die "
                                "Meldeliste prüft. Gehaltsbudget und Mindestgehalt stellst du "
                                "direkt auf der Seite Meldeliste ein."),
                             &cards);

    m_registrationUnavailable =
        caption(tr("Für diese Football-Manager-Version sind keine Melderegeln hinterlegt."));
    cards->addWidget(m_registrationUnavailable);

    QVBoxLayout *body = nullptr;
    auto *card = makeCard(tr("Registrierungsregeln"), QString(), &body);
    m_registrationCard = card;
    QFormLayout *form = makeForm(body);
    m_registrationLeagueCombo = sized(new QComboBox, kFieldWidth);
    m_registrationUefaCheck =
        new QCheckBox(tr("Champions League / Europa League / Conference League"));
    m_registrationMinGkSpin = sized(new QSpinBox, kNumberWidth);
    m_registrationMinGkSpin->setRange(0, 5);
    m_registrationMinGkSpin->setToolTip(
        tr("Kein Regelwerk schreibt das vor, aber ohne zwei Torhüter zu melden ist riskant."));
    addRow(form, tr("Liga"), m_registrationLeagueCombo);
    addRow(form, tr("Europapokal"), m_registrationUefaCheck);
    addRow(form, tr("Mindestens Torhüter"), m_registrationMinGkSpin);
    addHint(form,
            tr("Aktiviert die Home-Grown-/Club-Grown-/U21-Markierungen (Spieler bearbeiten, "
               "Rechtsklick → Registrierung) und den Registrierungs-Assistenten, sobald ein "
               "Wettbewerb eine Meldeliste verlangt. 'Standard' und Ligen mit 99 Plätzen "
               "(Bundesliga, Ligue 1, Österreich) kennen keine Beschränkung: dort sind alle "
               "Spieler spielberechtigt. Geprüft werden Kadergröße sowie im Land und im Verein "
               "ausgebildete Spieler; Grenzen für Ausländer oder Nicht-EU-Spieler prüft der "
               "Analyzer nicht. 'Standard' gilt für alle Ligen, deren Regeln noch nicht "
               "hinterlegt sind. Wie du weitere Ligen anfragst, steht in der README."));
    cards->addWidget(card);

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildImportPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Import"),
                             tr("Woher die Spielerdaten kommen und wie sie gelesen werden."),
                             &cards);

    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Football-Manager-Version"), QString(), &body));
    QFormLayout *form = makeForm(body);
    m_fmVersionCombo = sized(new QComboBox, kFieldWidth);
    for (const FmDataVersion &version : fmDataVersions()) {
        m_fmVersionCombo->addItem(version.supported
                                      ? version.displayName
                                      : tr("%1 (noch nicht unterstützt)")
                                            .arg(version.displayName),
                                  version.id);
        if (!version.supported)
            m_fmVersionCombo->setItemData(m_fmVersionCombo->count() - 1, false,
                                          Qt::UserRole - 1); // disable row
    }
    addRow(form, tr("Version dieses Spielstands"), m_fmVersionCombo);
    addHint(form, tr("Bestimmt, wie die Spielerattribute aus dem Export gelesen werden, welche "
                     "Rollen und Taktiken gelten und wie die Attribute gewichtet werden. Die "
                     "Version wird beim Anlegen eines Spielstands gewählt und steht fest, sobald "
                     "er Spieler enthält. Unterstützt werden Football Manager 2024 und 2027."));

    cards->addWidget(makeCard(tr("HTML-/CSV-Export"), QString(), &body));
    form = makeForm(body);
    m_importDirEdit = sized(new QLineEdit, kPathWidth);
    auto *importDirBrowse = new QPushButton(tr("Durchsuchen …"));
    auto *importDirOpen = new QPushButton(tr("Öffnen"));
    importDirOpen->setToolTip(tr("Ordner im Dateimanager öffnen"));
    addRow(form, tr("Standardordner"), hbox({m_importDirEdit, importDirBrowse, importDirOpen}));
    addHint(form, tr("Startordner der Dateiauswahl beim Hochladen von HTML- und CSV-Exporten. Leer = "
                     "automatisch: der Spielordner der Football-Manager-Version unter "
                     "'Sports Interactive' im Dokumentenordner des aktuellen Nutzers (falls "
                     "vorhanden)."));

    // The effective folder right now: the (possibly unsaved) edit content when
    // it points to an existing folder, otherwise the automatic default.
    const auto currentImportDir = [this] {
        const QString text = m_importDirEdit->text().trimmed();
        return (!text.isEmpty() && QDir(text).exists())
                   ? text
                   : AppConfig::defaultHtmlExportDir(m_context.fmVersionId());
    };
    connect(importDirBrowse, &QPushButton::clicked, this, [this, currentImportDir] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Standardordner für HTML-/CSV-Import wählen"), currentImportDir());
        if (!dir.isEmpty())
            m_importDirEdit->setText(QDir::toNativeSeparators(dir));
    });
    connect(importDirOpen, &QPushButton::clicked, this, [currentImportDir] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(currentImportDir()));
    });

    // FM27 has no export of its own: the shipped view filters (.fmf) give the
    // game's list screens the columns the analyzer reads. The standard tactic
    // ships as a game file too, so it can be tried in FM27 itself.
    const auto installIntoFm27 = [this](const QString &resourceDir, const QString &gameFolder,
                                        const QString &title) {
        const QString target =
            QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                .filePath(QStringLiteral("Sports Interactive/Football Manager 27/") + gameFolder);
        if (!QDir().mkpath(target)) {
            QMessageBox::warning(this, title,
                                 tr("Der Ordner %1 konnte nicht angelegt werden.")
                                     .arg(QDir::toNativeSeparators(target)));
            return;
        }
        QStringList copied;
        const QDir source(resourceDir);
        for (const QString &name : source.entryList({QStringLiteral("*.fmf")}, QDir::Files)) {
            const QString dest = QDir(target).filePath(name);
            QFile::remove(dest);
            if (QFile::copy(source.filePath(name), dest)) {
                QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner
                                                | QFile::ReadUser | QFile::WriteUser);
                copied << name;
            }
        }
        if (copied.isEmpty()) {
            QMessageBox::warning(this, title,
                                 tr("Es konnte keine Datei nach %1 kopiert werden.")
                                     .arg(QDir::toNativeSeparators(target)));
            return;
        }
        QMessageBox::information(this, title,
                                 tr("%n Datei(en) nach %1 kopiert:\n%2", nullptr, copied.size())
                                     .arg(QDir::toNativeSeparators(target),
                                          copied.join(QLatin1Char('\n'))));
    };

    cards->addWidget(makeCard(tr("Football Manager 27"),
                              tr("FM27 hat keinen eigenen Export. Der Analyzer liest Listen über "
                                 "mitgelieferte Ansichten oder direkt den Spielstand."),
                              &body));
    form = makeForm(body);
    auto *viewsButton = new QPushButton(tr("Ansichten installieren"));
    auto *tacticButton = new QPushButton(tr("Beispieltaktiken installieren"));
    addRow(form, tr("Spieldateien"), hbox({viewsButton, tacticButton}));
    addHint(form,
            tr("Ansichten: kopiert die mitgelieferten Ansichten (Kader, Spielerdatenbank, "
               "Shortlist) in den Ordner 'views' von Football Manager 27. Im Spiel lädst du sie "
               "dann über das Ansichten-Menü der Liste. Vorhandene Dateien mit demselben Namen "
               "werden ersetzt, andere Ansichten bleiben unberührt.\n\nBeispieltaktiken: kopiert "
               "die beiden mitgelieferten Beispieltaktiken (Control Press · 4-2-3-1 und Counter "
               "Surge · 3-4-2-1) als Spieldateien in den Ordner 'tactics' von Football Manager "
               "27. Im Spiel lädst du sie dann in der Taktik-Ansicht über das Laden einer "
               "Taktik. Im Analyzer sind sie schon eingebaut; eigene Taktiken legst du im "
               "Taktik-Editor an oder importierst sie."));
    connect(viewsButton, &QPushButton::clicked, this, [this, installIntoFm27] {
        installIntoFm27(QStringLiteral(":/views"), QStringLiteral("views"),
                        tr("FM27-Ansichten"));
    });
    connect(tacticButton, &QPushButton::clicked, this, [this, installIntoFm27] {
        installIntoFm27(QStringLiteral(":/tactics"), QStringLiteral("tactics"),
                        tr("FM27-Taktiken"));
    });

    m_attributeMaskingCheck = new QCheckBox(tr("Attribut-Maskierung wie im Spiel"));
    addRow(form, tr("Spielstand einlesen"), m_attributeMaskingCheck);
    addHint(form, tr("Gilt, wenn du einen FM27-Spielstand (.fm) einliest. Eingeschaltet übernimmt "
                     "der Analyzer nur Spieler, die du kennst: deinen Kader, Spieler mit sehr "
                     "hoher Reputation und Spieler aus deinem Scouting. Teilweise bekannte Spieler "
                     "bekommen Spannen, die der Analyzer selbst bildet; sie können von den "
                     "Spannen im Spiel abweichen. Ausgeschaltet übernimmt er alle Spieler mit "
                     "ihren echten Werten. Verdeckte Werte wie das Potenzial liest er nie aus."));

    m_liveSaveCheck = new QCheckBox(tr("Spielstand live verfolgen"));
    addRow(form, tr("Live verfolgen"), m_liveSaveCheck);
    m_liveSaveEdit = sized(new QLineEdit, kPathWidth);
    m_liveSaveEdit->setPlaceholderText(tr("FM27-Spielstand (.fm) wählen"));
    auto *liveSaveBrowse = new QPushButton(tr("Durchsuchen …"));
    addRow(form, tr("Spielstand-Datei"), hbox({m_liveSaveEdit, liveSaveBrowse}));
    addHint(form, tr("Solange der Analyzer offen ist, liest er diesen Spielstand automatisch neu "
                     "ein, sobald das Spiel ihn gespeichert hat, auch nach einem Autosave. Er "
                     "nimmt immer die neueste Datei der Karriere, auch die Versionen „(v02)“, "
                     "„(v03)“. Wenn du im Spiel unter einem neuen Namen speicherst, wähle die "
                     "neue Datei hier aus."));
    connect(liveSaveBrowse, &QPushButton::clicked, this, [this] {
        QString start = m_liveSaveEdit->text().trimmed();
        if (start.isEmpty())
            start = QFileInfo(AppConfig::defaultGraphicsDir(m_context.fmVersionId()))
                        .absolutePath()
                    + QStringLiteral("/games");
        const QString path = QFileDialog::getOpenFileName(
            this, tr("FM27-Spielstand auswählen"), start,
            tr("FM27-Spielstände (*.fm);;Alle Dateien (*)"));
        if (path.isEmpty())
            return;
        m_liveSaveEdit->setText(QDir::toNativeSeparators(path));
        m_liveSaveCheck->setChecked(true);
    });

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildHistoryPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Verlauf und Datenfrische"),
                             tr("Was der Analyzer über die Zeit speichert und wann Spieler als "
                                "veraltet gelten."),
                             &cards);

    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Spielerverlauf"), QString(), &body));
    QFormLayout *form = makeForm(body);
    m_historyAllCheck = new QCheckBox(tr("Verlauf für alle Spieler speichern"));
    addRow(form, tr("Umfang"), m_historyAllCheck);
    addHint(form, tr("Beim Einlesen eines FM27-Spielstands speichert der Analyzer pro "
                     "Ingame-Monat einen Stand der Attribute, der Durchschnittsnote, des "
                     "Marktwerts und des Gehalts, aber nur, wenn sich etwas geändert hat. Ohne "
                     "Haken gilt das für deinen Kader, das Zweitteam, die Shortlist, den "
                     "Nationalkader und Spieler aus deinem Scouting. Mit Haken für alle "
                     "eingelesenen Spieler; die Datenbank wird dadurch deutlich größer. Die "
                     "aktuelle und die zwei Saisons davor bleiben monatlich, ältere Saisons "
                     "werden auf ihren Stand am Saisonende verdichtet."));
    m_seasonStartEdit = sized(new QDateEdit, 160);
    m_seasonStartEdit->setDisplayFormat(QStringLiteral("d. MMMM"));
    m_seasonStartEdit->setDateRange(QDate(2001, 1, 1), QDate(2001, 12, 31));
    m_seasonStartEdit->setCalendarPopup(false);
    addRow(form, tr("Saisonbeginn"), m_seasonStartEdit);
    addHint(form, tr("Ab diesem Tag zählt der Verlauf eine neue Saison, zum Beispiel für die "
                     "Anzeige der Saison in der Übersicht und beim Verdichten alter Saisons. Der "
                     "1. Juli passt für die meisten europäischen Ligen; für Ligen, die im "
                     "Kalenderjahr spielen, nimm den 1. Januar."));

    cards->addWidget(makeCard(tr("Datenfrische"), QString(), &body));
    form = makeForm(body);
    m_staleUploadsSpin = sized(new QSpinBox, kNumberWidth);
    m_staleUploadsSpin->setRange(1, 50);
    m_retirementAgeSpin = sized(new QSpinBox, kNumberWidth);
    m_retirementAgeSpin->setRange(25, 60);
    addRow(form, tr("Veraltet nach verpassten Komplett-Exporten (Y)"), m_staleUploadsSpin);
    addRow(form, tr("Retired ab Alter (X)"), m_retirementAgeSpin);
    addHint(form, tr("Ein Import mit mindestens einem Viertel so vielen Spielern wie der letzte "
                     "Komplett-Export zählt selbst als Komplett-Export. Kleinere Exporte (Kader, "
                     "Shortlist, Scouting) aktualisieren nur ihre Spieler. Wer in den letzten Y "
                     "Komplett-Exporten fehlte, gilt als veraltet und wird in Matrizen, "
                     "Vorschlägen und Pools ausgeblendet – außer Spielern deines Vereins und der "
                     "Shortlist. Ist er zusätzlich mindestens X Jahre alt, heißt er Retired."));

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildGraphicsPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Porträts und Wappen"),
                             tr("Optional: Spielergesichter und Vereinslogos aus installierten "
                                "Grafikpaketen. Ohne diese Einstellung ändert sich nichts."),
                             &cards);

    // Optional: portraits and crests from the game's graphics folder (face
    // and logo packs). Off by default; without it nothing changes anywhere.
    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Graphics-Ordner"), QString(), &body));
    m_graphicsCheck = new QCheckBox(tr("Porträts und Wappen aus dem Graphics-Ordner verwenden"));
    body->addWidget(m_graphicsCheck);
    m_graphicsBody = new QWidget;
    auto *graphicsLayout = new QVBoxLayout(m_graphicsBody);
    graphicsLayout->setContentsMargins(0, 4, 0, 0);
    graphicsLayout->setSpacing(12);
    body->addWidget(m_graphicsBody);
    QFormLayout *form = makeForm(graphicsLayout);
    addHint(form, tr("Nur sinnvoll, wenn du Grafikpakete (Spielergesichter, Vereinslogos) für "
                     "Football Manager installiert hast. Der Analyzer liest die Zuordnungen aus "
                     "den config.xml-Dateien der Pakete und zeigt Porträts über die Spieler-ID "
                     "und Wappen über die Vereins-ID. Am Graphics-Ordner wird nichts verändert."));

    m_graphicsDirEdit = sized(new QLineEdit, kPathWidth);
    auto *graphicsBrowse = new QPushButton(tr("Durchsuchen …"));
    auto *graphicsRescan = new QPushButton(tr("Neu einlesen"));
    graphicsRescan->setToolTip(tr("Liest den Ordner erneut ein, z. B. nach dem Installieren "
                                  "eines neuen Grafikpakets."));
    addRow(form, tr("Ordner"), hbox({m_graphicsDirEdit, graphicsBrowse, graphicsRescan}));
    m_graphicsStatus = caption(QString());
    form->addRow(QString(), m_graphicsStatus);

    // The own club's FM id: exports carry no club ids, so it is set once
    // here (typed, or read from the file name of the chosen crest).
    m_clubFmIdEdit = sized(new QLineEdit, kNumberWidth + 28);
    m_clubFmIdEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d{0,10}")), m_clubFmIdEdit));
    m_graphicsCrestPreview = new QLabel;
    m_graphicsCrestPreview->setObjectName(QStringLiteral("imageWell"));
    m_graphicsCrestPreview->setFixedSize(40, 40);
    m_graphicsCrestPreview->setAlignment(Qt::AlignCenter);
    auto *pickCrest = new QPushButton(tr("Wappen wählen …"));
    addRow(form, tr("Vereins-ID meines Vereins"),
           hbox({m_clubFmIdEdit, m_graphicsCrestPreview, pickCrest}));
    addHint(form, tr("Die Exporte enthalten keine Vereins-IDs. Trage die ID deines Vereins ein "
                     "oder wähle sein Wappen im Logo-Paket aus (die Zahl im Dateinamen, z. B. "
                     "1234_club.png, ist die ID). Ein selbst gewähltes Vereinslogo (Seite "
                     "Verein) hat Vorrang."));
    m_graphicsBody->setVisible(false);
    connect(m_graphicsCheck, &QCheckBox::toggled, m_graphicsBody, &QWidget::setVisible);
    connect(m_graphicsCheck, &QCheckBox::toggled, this, &SettingsPage::updateGraphicsStatus);

    const auto currentGraphicsDir = [this] {
        const QString text = m_graphicsDirEdit->text().trimmed();
        return text.isEmpty() ? AppConfig::defaultGraphicsDir(m_context.fmVersionId()) : text;
    };
    connect(graphicsBrowse, &QPushButton::clicked, this, [this, currentGraphicsDir] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Graphics-Ordner wählen"), currentGraphicsDir());
        if (!dir.isEmpty())
            m_graphicsDirEdit->setText(QDir::toNativeSeparators(dir));
    });
    connect(graphicsRescan, &QPushButton::clicked, this,
            [this] { m_context.applyGraphicsSettings(true); });
    connect(pickCrest, &QPushButton::clicked, this, [this, currentGraphicsDir] {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Wappen des eigenen Vereins wählen"), currentGraphicsDir(),
            tr("Bilder (*.png *.jpg *.jpeg);;Alle Dateien (*)"));
        if (file.isEmpty())
            return;
        const QRegularExpressionMatch match =
            QRegularExpression(QStringLiteral("^(\\d{1,10})")).match(QFileInfo(file).baseName());
        if (!match.hasMatch()) {
            QMessageBox::information(
                this, tr("Vereins-ID"),
                tr("Im Dateinamen steht keine Vereins-ID. Bitte die ID von Hand eintragen "
                   "oder das Bild auf der Seite Verein als Vereinslogo wählen."));
            return;
        }
        m_clubFmIdEdit->setText(match.captured(1));
    });
    connect(m_clubFmIdEdit, &QLineEdit::textChanged, this, &SettingsPage::updateGraphicsStatus);
    connect(&m_context.graphics(), &GraphicsLibrary::stateChanged, this,
            &SettingsPage::updateGraphicsStatus);

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildWeightsPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("DWRS-Gewichte"),
                             tr("Wie stark die Attribut-Kategorien in die DWRS-Bewertung "
                                "eingehen. Geänderte Gewichte berechnen beim Speichern alle "
                                "Bewertungen neu."),
                             &cards);

    const auto makeSpin = [](double max = 20.0) {
        auto *spin = sized(new QDoubleSpinBox, kNumberWidth);
        spin->setRange(0.0, max);
        spin->setDecimals(2);
        spin->setSingleStep(0.1);
        return spin;
    };

    m_weightsVersionLabel = caption(QString());
    cards->addWidget(m_weightsVersionLabel);

    // Field players and goalkeepers side by side: same kind, same size.
    auto *pair = new QHBoxLayout;
    pair->setSpacing(16);
    QVBoxLayout *body = nullptr;
    auto *fieldCard = makeCard(tr("Feldspieler"), QString(), &body);
    QFormLayout *form = makeForm(body, 150);
    for (const QString &category : weightCategoryOrder()) {
        auto *spin = makeSpin();
        m_weightSpins.insert(category, spin);
        addRow(form, category, spin);
    }
    body->addStretch(1);
    pair->addWidget(fieldCard, 1);
    auto *gkCard = makeCard(tr("Torhüter"), QString(), &body);
    form = makeForm(body, 150);
    for (const QString &category : gkWeightCategoryOrder()) {
        auto *spin = makeSpin();
        m_gkWeightSpins.insert(category, spin);
        addRow(form, category, spin);
    }
    pair->addWidget(gkCard, 1);
    cards->addLayout(pair);

    cards->addWidget(makeCard(tr("Rollen-Multiplikatoren"), QString(), &body));
    form = makeForm(body);
    m_keyMultSpin = makeSpin();
    m_prefMultSpin = makeSpin();
    addRow(form, tr("Schlüssel-Attribute"), m_keyMultSpin);
    addRow(form, tr("Bevorzugte Attribute"), m_prefMultSpin);

    cards->addWidget(makeCard(tr("Spielzeit-Gewichte"),
                              tr("Gewicht je zugesagter Spielzeit (Agreed Playing Time)."),
                              &body));
    auto *aptGrid = new QGridLayout;
    aptGrid->setContentsMargins(0, 0, 0, 0);
    aptGrid->setHorizontalSpacing(24);
    aptGrid->setVerticalSpacing(12);
    QStringList allApt = fieldPlayerAptOptions() + gkAptOptions();
    allApt.removeDuplicates();
    allApt.removeAll(QStringLiteral("None"));
    const int rows = (int(allApt.size()) + 1) / 2;
    for (int i = 0; i < allApt.size(); ++i) {
        auto *spin = makeSpin(5.0);
        m_aptSpins.insert(allApt.at(i), spin);
        auto *label = new QLabel(allApt.at(i));
        const int column = (i / rows) * 2;
        aptGrid->addWidget(label, i % rows, column);
        aptGrid->addWidget(spin, i % rows, column + 1);
    }
    aptGrid->setColumnMinimumWidth(0, 180);
    aptGrid->setColumnMinimumWidth(2, 180);
    aptGrid->setColumnMinimumWidth(1, kNumberWidth + 24); // gap before the second pair
    aptGrid->setColumnStretch(4, 1);
    body->addLayout(aptGrid);

    cards->addWidget(makeCard(tr("Neuberechnung"), QString(), &body));
    auto *recalcButton = new QPushButton(tr("Alle Bewertungen neu berechnen"));
    body->addLayout(hbox({recalcButton}));
    body->addWidget(foldableHint(
        tr("Berechnet die Bewertungen aller Spieler im aktiven Spielstand mit den "
           "gespeicherten Gewichten neu. Nicht gespeicherte Änderungen auf dieser Seite "
           "werden dabei nicht berücksichtigt.")));
    connect(recalcButton, &QPushButton::clicked, this, &SettingsPage::recalcRequested);

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildSquadPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Kader und Analyse"),
                             tr("Schwellenwerte für Aufstellung, Jugend, Leihen und die "
                                "Gap-Analyse."),
                             &cards);

    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Aufstellung"), QString(), &body));
    QFormLayout *form = makeForm(body);
    m_naturalPosSpin = sized(new QDoubleSpinBox, kNumberWidth);
    m_naturalPosSpin->setRange(1.0, 2.0);
    m_naturalPosSpin->setDecimals(2);
    m_naturalPosSpin->setSingleStep(0.01);
    m_ipShareSpin = sized(new QSpinBox, kNumberWidth);
    m_ipShareSpin->setRange(0, 100);
    m_ipShareSpin->setSuffix(QStringLiteral(" %"));
    m_maxDepthRolesSpin = sized(new QSpinBox, kNumberWidth);
    m_maxDepthRolesSpin->setRange(1, 10);
    addRow(form, tr("Bonus natürliche Position"), m_naturalPosSpin);
    addRow(form, tr("Anteil mit Ball (ab FM27)"), m_ipShareSpin);
    addHint(form, tr("Ab FM27 hat jede Taktik eine Aufstellung mit Ball und eine ohne Ball. Der "
                     "Wert eines Spielers ist dieser Anteil seiner Rolle mit Ball plus der Rest "
                     "seiner Rolle ohne Ball. Standard 50 %."));
    addRow(form, tr("Max. Rollen pro Depth-Spieler"), m_maxDepthRolesSpin);

    cards->addWidget(makeCard(tr("Jugend und Leihen"), QString(), &body));
    form = makeForm(body);
    m_outfielderAgeSpin = sized(new QSpinBox, kNumberWidth);
    m_outfielderAgeSpin->setRange(15, 30);
    m_goalkeeperAgeSpin = sized(new QSpinBox, kNumberWidth);
    m_goalkeeperAgeSpin->setRange(15, 35);
    m_minLoanTalentSpin = sized(new QSpinBox, kNumberWidth);
    m_minLoanTalentSpin->setRange(0, 200);
    m_minLoanAgeSpin = sized(new QSpinBox, kNumberWidth);
    m_minLoanAgeSpin->setRange(14, 25);
    m_youthLoanOverAgeSpin = sized(new QSpinBox, kNumberWidth);
    m_youthLoanOverAgeSpin->setRange(14, 25);
    addRow(form, tr("Jugend: Feldspieler bis Alter"), m_outfielderAgeSpin);
    addRow(form, tr("Jugend: Torhüter bis Alter"), m_goalkeeperAgeSpin);
    addRow(form, tr("Min. Talent-Score für Leihe"), m_minLoanTalentSpin);
    addRow(form, tr("Mindestalter für Leihe"), m_minLoanAgeSpin);
    addRow(form, tr("Jugendelf-Leihe erst über Alter"), m_youthLoanOverAgeSpin);

    cards->addWidget(makeCard(tr("Gap-Analyse"), QString(), &body));
    form = makeForm(body);
    const auto makeGapSpin = [] {
        auto *spin = sized(new QDoubleSpinBox, kNumberWidth);
        spin->setRange(0.0, 50.0);
        spin->setDecimals(1);
        return spin;
    };
    m_displacementSpin = makeGapSpin();
    m_dropoffSpin = makeGapSpin();
    m_wrongSideSpin = makeGapSpin();
    addRow(form, tr("Displacement-Schwelle"), m_displacementSpin);
    addRow(form, tr("Dropoff-Schwelle"), m_dropoffSpin);
    addRow(form, tr("Falsche-Seite-Malus"), m_wrongSideSpin);

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildTrainingPage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Training"),
                             tr("Bis zu welchem Alter sich Attributgruppen noch trainieren "
                                "lassen. Steuert die Trainingsempfehlung."),
                             &cards);

    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Trainings-Altersfenster"), QString(), &body));
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(12);
    auto *peakHead = caption(tr("Voll trainierbar bis"));
    auto *lockedHead = caption(tr("Gesperrt ab"));
    grid->addWidget(peakHead, 0, 1);
    grid->addWidget(lockedHead, 0, 2);
    const auto makeAgeSpin = [] {
        auto *spin = sized(new QSpinBox, kNumberWidth);
        spin->setRange(15, 40);
        return spin;
    };
    int row = 1;
    const auto addWindow = [&](const QString &group, const QString &detail, QSpinBox *&peak,
                               QSpinBox *&locked) {
        auto *labels = new QVBoxLayout;
        labels->setSpacing(0);
        auto *name = new QLabel(group);
        labels->addWidget(name);
        if (!detail.isEmpty())
            labels->addWidget(caption(detail));
        auto *holder = new QWidget;
        labels->setContentsMargins(0, 0, 0, 0);
        holder->setLayout(labels);
        holder->setMinimumWidth(kLabelWidth);
        peak = makeAgeSpin();
        locked = makeAgeSpin();
        grid->addWidget(holder, row, 0);
        grid->addWidget(peak, row, 1);
        grid->addWidget(locked, row, 2);
        ++row;
    };
    addWindow(tr("Explosiv"), tr("Tempo, Antritt, Beweglichkeit"), m_explosivePeakSpin,
              m_explosiveLockedSpin);
    addWindow(tr("Kraft"), tr("Kraft, Ausdauer, Sprungkraft, Balance"), m_strengthPeakSpin,
              m_strengthLockedSpin);
    addWindow(tr("Technik"), QString(), m_technicalPeakSpin, m_technicalLockedSpin);
    grid->setColumnStretch(3, 1);
    body->addLayout(grid);
    body->addWidget(foldableHint(
        tr("Bis 'voll trainierbar bis' gilt eine Gruppe als voll trainierbar, danach nimmt die "
           "Wirkung linear ab bis 'gesperrt ab'. Mentale Attribute gelten immer als "
           "trainierbar, Charakter-Attribute (Entschlossenheit, Arbeitsrate) nie (nur über "
           "Mentoring).")));

    finishPage(cards);
    return page;
}

QWidget *SettingsPage::buildThemePage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Design"),
                             tr("Farben und Vereinskopf dieses Spielstands."), &cards);

    // --- Preset + mode ---
    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Farbschema"), QString(), &body));
    QFormLayout *form = makeForm(body);
    m_presetCombo = sized(new QComboBox, kFieldWidth);
    m_presetCombo->setMaxVisibleItems(20);
    m_templates = std::make_unique<ColorTemplateStore>(m_context.paths().colorTemplatesFile());
    m_templates->load();
    fillPresetCombo();
    m_modeCombo = sized(new QComboBox, kFieldWidth);
    m_modeCombo->addItem(tr("Nacht (dunkel)"), QStringLiteral("night"));
    m_modeCombo->addItem(tr("Tag (hell)"), QStringLiteral("day"));
    addRow(form, tr("Modus"), m_modeCombo);
    addRow(form, tr("Vorlage"), m_presetCombo);
    auto *saveTemplateButton = new QPushButton(tr("Als eigene Vorlage speichern …"));
    saveTemplateButton->setToolTip(
        tr("Speichert die Farben unten (Tag und Nacht) als eigene Vorlage. Eigene Vorlagen "
           "stehen in allen Spielständen zur Wahl und bleiben bei Updates erhalten."));
    m_deleteTemplateButton = new QPushButton(tr("Vorlage löschen"));
    m_deleteTemplateButton->setEnabled(false);
    form->addRow(QString(), [&] {
        auto *w = new QWidget;
        w->setLayout(hbox({saveTemplateButton, m_deleteTemplateButton}));
        return w;
    }());
    connect(saveTemplateButton, &QPushButton::clicked, this, &SettingsPage::saveColorTemplate);
    connect(m_deleteTemplateButton, &QPushButton::clicked, this,
            &SettingsPage::deleteColorTemplate);
    addHint(form, tr("Eine Vorlage setzt eine ruhige Anthrazit-Basis und nur die Vereinsfarben "
                     "als Akzent (Kopfzeile, Buttons, Interaktion). Farben unten lassen sich "
                     "danach frei anpassen — das schaltet auf 'Benutzerdefiniert'."));

    connect(m_presetCombo, &QComboBox::activated, this, [this] {
        const QString id = m_presetCombo->currentData().toString();
        m_deleteTemplateButton->setEnabled(ColorTemplateStore::isTemplateId(id));
        if (id == QLatin1String("custom"))
            return;
        applyPresetToPending(id);
    });

    // --- Club header ---
    cards->addWidget(makeCard(tr("Vereinskopf"), QString(), &body));
    form = makeForm(body);
    m_headerStyleCombo = sized(new QComboBox, kFieldWidth);
    m_headerStyleCombo->addItem(tr("Vereinsfarben mit Wappen"), QStringLiteral("gradient"));
    m_headerStyleCombo->addItem(tr("Einfarbig (Farbe „Kopfzeile“)"), QStringLiteral("solid"));
    addRow(form, tr("Darstellung"), m_headerStyleCombo);
    m_watermarkCheck = new QCheckBox(tr("Wappen zusätzlich als dezente Silhouette zeigen"));
    form->addRow(QString(), m_watermarkCheck);
    addHint(form, tr("Der Vereinskopf nutzt die Farbe „Primär“ als Fläche und „Interaktion“ als "
                     "schmalen Streifen. Sind die Farben zu hell oder zu grell, dunkelt der "
                     "Analyzer die Fläche so weit ab, dass Name und Stadion lesbar bleiben."));
    connect(m_headerStyleCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_watermarkCheck->setEnabled(m_headerStyleCombo->currentData().toString()
                                     != QLatin1String("solid"));
    });

    // --- Color roles: one row per role, night and day side by side ---
    cards->addWidget(makeCard(tr("Farben"),
                              tr("Ein Klick auf eine Farbe öffnet die Farbauswahl."), &body));
    const QList<QPair<QString, QString>> colorKeys = {
        {QStringLiteral("background_color"), tr("Hintergrund")},
        {QStringLiteral("surface_color"), tr("Panel / Karten")},
        {QStringLiteral("sidebar_color"), tr("Sidebar")},
        {QStringLiteral("header_color"), tr("Kopfzeile")},
        {QStringLiteral("primary_color"), tr("Primär / Buttons")},
        {QStringLiteral("interactive_color"), tr("Interaktion (Tabs, Slider)")},
        {QStringLiteral("text_color"), tr("Text")},
    };
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(8);
    grid->addWidget(caption(tr("Nacht")), 0, 1);
    grid->addWidget(caption(tr("Tag")), 0, 2);
    int row = 1;
    for (const auto &[key, label] : colorKeys) {
        auto *name = new QLabel(label);
        name->setMinimumWidth(kLabelWidth);
        grid->addWidget(name, row, 0);
        int column = 1;
        for (const QString &mode : {QStringLiteral("night"), QStringLiteral("day")}) {
            const QString fullKey = mode + QLatin1Char('_') + key;
            auto *button = new QPushButton;
            button->setObjectName(QStringLiteral("colorSwatch"));
            button->setFixedSize(kSwatchWidth, 32);
            button->setCursor(Qt::PointingHandCursor);
            m_colorButtons.insert(fullKey, button);
            connect(button, &QPushButton::clicked, this, [this, fullKey, button] {
                const QColor initial(m_pendingColors.value(fullKey));
                const QColor picked =
                    QColorDialog::getColor(initial, this, tr("Farbe wählen"));
                if (!picked.isValid())
                    return;
                m_pendingColors.insert(fullKey, picked.name());
                button->setText(picked.name());
                button->setStyleSheet(colorButtonStyle(picked));
                markCustomPreset();
                updateContrastWarning();
            });
            grid->addWidget(button, row, column++);
        }
        ++row;
    }
    grid->setColumnStretch(3, 1);
    body->addLayout(grid);

    m_contrastLabel = caption(QString());
    body->addWidget(m_contrastLabel);

    finishPage(cards);
    return page;
}


void SettingsPage::updateGraphicsStatus()
{
    if (!m_graphicsStatus)
        return;
    const GraphicsLibrary &graphics = m_context.graphics();
    const QLocale locale;
    QString text;
    switch (graphics.state()) {
    case GraphicsLibrary::State::Off:
        text = m_graphicsCheck->isChecked()
                   ? tr("Wird beim Speichern eingelesen.")
                   : QString();
        break;
    case GraphicsLibrary::State::Missing:
        text = tr("Ordner nicht gefunden oder ohne Grafikpakete (config.xml): %1")
                   .arg(QDir::toNativeSeparators(graphics.folder()));
        break;
    case GraphicsLibrary::State::Indexing:
        text = tr("Grafikpakete werden eingelesen…");
        break;
    case GraphicsLibrary::State::Ready: {
        const GraphicsIndex::Stats stats = graphics.stats();
        text = tr("%1 Porträts und %2 Vereinswappen gefunden.")
                   .arg(locale.toString(stats.portraits), locale.toString(stats.clubLogos));
        if (stats.portraits == 0 && stats.clubLogos == 0)
            text = tr("Keine Porträts oder Wappen gefunden. Liegen die Pakete mit ihrer "
                      "config.xml in diesem Ordner?");
        break;
    }
    }
    m_graphicsStatus->setText(text);
    m_graphicsStatus->setVisible(!text.isEmpty());

    const quint32 id = GraphicsIndex::parseId(m_clubFmIdEdit->text());
    const QPixmap crest = graphics.clubLogo(id, m_graphicsCrestPreview->width());
    m_graphicsCrestPreview->setPixmap(crest);
    m_graphicsCrestPreview->setToolTip(
        crest.isNull() && id != 0 && graphics.isReady()
            ? tr("Zu dieser Vereins-ID gibt es im Ordner kein Wappen.")
            : QString());
}

void SettingsPage::chooseLogo()
{
    const QString source = QFileDialog::getOpenFileName(
        this, tr("Vereinslogo wählen"), QString(),
        tr("Bilder (*.png *.jpg *.jpeg *.bmp);;Alle Dateien (*)"));
    if (source.isEmpty())
        return;

    QPixmap pixmap;
    if (!pixmap.load(source)) {
        QMessageBox::warning(this, tr("Vereinslogo"),
                             tr("Die Datei konnte nicht als Bild geladen werden."));
        return;
    }

    // Stored inside the save's profile: duplicates and backups carry it
    // along, and the original file can move freely.
    QString error;
    if (!m_context.setProfileImage(AppContext::ProfileImage::ClubLogo, pngBytes(pixmap), &error)) {
        QMessageBox::warning(this, tr("Vereinslogo"),
                             tr("Das Logo konnte nicht gespeichert werden.\n%1").arg(error));
        return;
    }
    updateLogoPreview();
    m_context.notifySettingsChanged(); // refreshes the header
}

void SettingsPage::removeLogo()
{
    QString error;
    if (!m_context.setProfileImage(AppContext::ProfileImage::ClubLogo, {}, &error))
        QMessageBox::warning(this, tr("Vereinslogo"),
                             tr("Das Logo konnte nicht entfernt werden.\n%1").arg(error));
    updateLogoPreview();
    m_context.notifySettingsChanged();
}

void SettingsPage::updateLogoPreview()
{
    QPixmap pixmap;
    if (pixmap.loadFromData(m_context.profileImage(AppContext::ProfileImage::ClubLogo))) {
        m_logoPreview->setPixmap(
            pixmap.scaled(m_logoPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_logoRemoveButton->setEnabled(true);
    } else {
        m_logoPreview->setPixmap(QPixmap());
        m_logoPreview->setText(tr("(kein Logo)"));
        m_logoPreview->setStyleSheet(QStringLiteral("color: rgba(128,128,128,0.8);"));
        m_logoRemoveButton->setEnabled(false);
    }
}

void SettingsPage::chooseFlag()
{
    const QString source = QFileDialog::getOpenFileName(
        this, tr("National-Flagge wählen"), QString(),
        tr("Bilder (*.png *.jpg *.jpeg *.bmp);;Alle Dateien (*)"));
    if (source.isEmpty())
        return;

    QPixmap pixmap;
    if (!pixmap.load(source)) {
        QMessageBox::warning(this, tr("National-Flagge"),
                             tr("Die Datei konnte nicht als Bild geladen werden."));
        return;
    }

    QString error;
    if (!m_context.setProfileImage(AppContext::ProfileImage::NationalFlag, pngBytes(pixmap),
                                   &error)) {
        QMessageBox::warning(this, tr("National-Flagge"),
                             tr("Die Flagge konnte nicht gespeichert werden.\n%1").arg(error));
        return;
    }
    updateFlagPreview();
    m_context.notifySettingsChanged(); // refreshes the header
}

void SettingsPage::removeFlag()
{
    QString error;
    if (!m_context.setProfileImage(AppContext::ProfileImage::NationalFlag, {}, &error))
        QMessageBox::warning(this, tr("National-Flagge"),
                             tr("Die Flagge konnte nicht entfernt werden.\n%1").arg(error));
    updateFlagPreview();
    m_context.notifySettingsChanged();
}

void SettingsPage::updateFlagPreview()
{
    QPixmap pixmap;
    if (pixmap.loadFromData(m_context.profileImage(AppContext::ProfileImage::NationalFlag))) {
        m_flagPreview->setPixmap(
            pixmap.scaled(m_flagPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_flagRemoveButton->setEnabled(true);
    } else {
        m_flagPreview->setPixmap(QPixmap());
        m_flagPreview->setText(tr("(keine Flagge)"));
        m_flagPreview->setStyleSheet(QStringLiteral("color: rgba(128,128,128,0.8);"));
        m_flagRemoveButton->setEnabled(false);
    }
}
void SettingsPage::fillPresetCombo()
{
    QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();

    struct Entry {
        QString league;
        QString name;
        QString id;
    };
    QList<Entry> entries;
    for (const ClubTheme &preset : clubThemes())
        entries.append({preset.league, preset.displayName, preset.id});
    const QString customLeague = tr("Custom");
    for (const ColorTemplate &own : m_templates->templates()) {
        entries.append({own.league.isEmpty() ? customLeague : own.league,
                        tr("%1 (eigene)").arg(own.name), own.id});
    }
    // Neutral first (empty league), leagues alphabetically, "Custom" last.
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) {
        const auto rank = [&](const Entry &e) {
            return e.league.isEmpty() ? 0 : (e.league == customLeague ? 2 : 1);
        };
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        const int league = QString::localeAwareCompare(a.league, b.league);
        if (league != 0)
            return league < 0;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });

    // Grouped by league: a disabled league caption above its entries.
    QString league;
    for (const Entry &entry : std::as_const(entries)) {
        if (entry.league != league) {
            league = entry.league;
            m_presetCombo->insertSeparator(m_presetCombo->count());
            m_presetCombo->addItem(QStringLiteral("— %1 —").arg(league));
            if (auto *model = qobject_cast<QStandardItemModel *>(m_presetCombo->model()))
                model->item(m_presetCombo->count() - 1)->setEnabled(false);
        }
        m_presetCombo->addItem(entry.name, entry.id);
    }
    m_presetCombo->insertSeparator(m_presetCombo->count());
    m_presetCombo->addItem(tr("Benutzerdefiniert (nicht gespeichert)"), QStringLiteral("custom"));
}

void SettingsPage::selectPresetInCombo(const QString &presetId)
{
    QSignalBlocker blocker(m_presetCombo);
    const int index = m_presetCombo->findData(presetId);
    m_presetCombo->setCurrentIndex(index >= 0 ? index
                                              : m_presetCombo->findData(QStringLiteral("custom")));
    if (m_deleteTemplateButton)
        m_deleteTemplateButton->setEnabled(ColorTemplateStore::isTemplateId(
            m_presetCombo->currentData().toString()));
}

void SettingsPage::saveColorTemplate()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Eigene Farbvorlage speichern"));
    auto *form = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(&dialog);
    if (const ColorTemplate *current = m_templates->find(m_pendingPreset))
        nameEdit->setText(current->name);
    auto *leagueCombo = new QComboBox(&dialog);
    leagueCombo->setEditable(true);
    leagueCombo->addItem(tr("Custom"), QString());
    QStringList leagues;
    for (const ClubTheme &preset : clubThemes()) {
        if (!preset.league.isEmpty() && !leagues.contains(preset.league))
            leagues << preset.league;
    }
    for (const QString &own : m_templates->leagues()) {
        if (!leagues.contains(own))
            leagues << own;
    }
    std::sort(leagues.begin(), leagues.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    for (const QString &league : std::as_const(leagues))
        leagueCombo->addItem(league, league);
    if (const ColorTemplate *current = m_templates->find(m_pendingPreset)) {
        const int index = leagueCombo->findData(current->league);
        if (index >= 0)
            leagueCombo->setCurrentIndex(index);
    }
    form->addRow(tr("Name:"), nameEdit);
    form->addRow(tr("Liga:"), leagueCombo);
    auto *hint = new QLabel(tr("Eine Liga sortiert die Vorlage zu den Vereinen dieser Liga. "
                               "„Custom“ sammelt Vorlagen ohne Liga. Gleicher Name in "
                               "derselben Liga überschreibt die Vorlage."),
                            &dialog);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("kpiCaption"));
    form->addRow(hint);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(nameEdit, &QLineEdit::textChanged, &dialog, [buttons](const QString &text) {
        buttons->button(QDialogButtonBox::Save)->setEnabled(!text.trimmed().isEmpty());
    });
    buttons->button(QDialogButtonBox::Save)->setEnabled(!nameEdit->text().trimmed().isEmpty());
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Typed league text that matches no entry: use it as a new league.
    QString league = leagueCombo->currentText().trimmed();
    if (league == tr("Custom"))
        league.clear();
    const QString id = m_templates->save(nameEdit->text(), league, m_pendingColors);
    if (id.isEmpty()) {
        QMessageBox::critical(this, tr("Farbvorlage"),
                              tr("Die Vorlage konnte nicht gespeichert werden.\n\n%1")
                                  .arg(m_templates->lastError()));
        return;
    }
    m_pendingPreset = id;
    fillPresetCombo();
    selectPresetInCombo(id);
}

void SettingsPage::deleteColorTemplate()
{
    const QString id = m_presetCombo->currentData().toString();
    const ColorTemplate *entry = m_templates->find(id);
    if (!entry)
        return;
    if (QMessageBox::question(this, tr("Farbvorlage löschen"),
                              tr("Die eigene Vorlage „%1“ löschen? Spielstände, die sie "
                                 "nutzen, behalten ihre Farben.")
                                  .arg(entry->name))
        != QMessageBox::Yes)
        return;
    if (!m_templates->remove(id)) {
        QMessageBox::critical(this, tr("Farbvorlage"),
                              tr("Die Vorlage konnte nicht gelöscht werden.\n\n%1")
                                  .arg(m_templates->lastError()));
        return;
    }
    // The colors stay; they are just no longer a named template.
    m_pendingPreset = QStringLiteral("custom");
    fillPresetCombo();
    selectPresetInCombo(m_pendingPreset);
}

void SettingsPage::applyPresetToPending(const QString &presetId)
{
    QHash<QString, QString> preset = presetThemeSettings(presetId);
    if (const ColorTemplate *own = m_templates->find(presetId))
        preset = own->colors;
    for (auto it = m_colorButtons.begin(); it != m_colorButtons.end(); ++it) {
        const QString value = preset.value(it.key());
        if (value.isEmpty())
            continue;
        m_pendingColors.insert(it.key(), value);
        it.value()->setText(value);
        it.value()->setStyleSheet(colorButtonStyle(QColor(value)));
    }
    m_pendingPreset = presetId;
    updateContrastWarning();
}

void SettingsPage::markCustomPreset()
{
    m_pendingPreset = QStringLiteral("custom");
    selectPresetInCombo(m_pendingPreset);
}

QWidget *SettingsPage::buildDatabasePage()
{
    QVBoxLayout *cards = nullptr;
    QWidget *page = makePage(tr("Spielstände"),
                             tr("Spielstand wechseln, neu anlegen oder übernehmen. Diese "
                                "Aktionen wirken sofort."),
                             &cards);

    QVBoxLayout *body = nullptr;
    cards->addWidget(makeCard(tr("Aktiver Spielstand"), QString(), &body));
    m_dbCombo = sized(new QComboBox, kFieldWidth);
    auto *switchButton = new QPushButton(tr("Wechseln"));
    body->addLayout(hbox({m_dbCombo, switchButton}));

    connect(switchButton, &QPushButton::clicked, this, [this] {
        const QString name = m_dbCombo->currentText();
        if (name.isEmpty() || name == m_context.currentDbName())
            return;
        QString error;
        if (!m_context.openDatabase(name, &error))
            QMessageBox::critical(this, tr("Datenbank"), error);
    });

    auto *createButton = new QPushButton(tr("Neuer Spielstand …"));
    auto *migrateButton = new QPushButton(tr("Legacy-Datenbank importieren …"));
    body->addLayout(hbox({createButton, migrateButton}));

    connect(createButton, &QPushButton::clicked, this, [this] {
        NewSaveWizard wizard(m_context, this);
        wizard.exec();
        refresh();
    });
    connect(migrateButton, &QPushButton::clicked, this, [this] {
        MigrationWizard wizard(m_context, this);
        wizard.exec();
        refresh();
        if (!wizard.migratedDbName().isEmpty()
            && QMessageBox::question(this, tr("Import"),
                                     tr("Importierte Datenbank '%1' jetzt aktivieren?")
                                         .arg(wizard.migratedDbName()))
                   == QMessageBox::Yes) {
            QString error;
            if (!m_context.openDatabase(wizard.migratedDbName(), &error))
                QMessageBox::critical(this, tr("Datenbank"), error);
            refresh();
        }
    });

    cards->addWidget(makeCard(tr("Programmstart"), QString(), &body));
    auto *autoOpen =
        new QCheckBox(tr("Beim Start immer den zuletzt verwendeten Spielstand öffnen"));
    autoOpen->setChecked(m_context.config().autoOpenLastDatabase());
    connect(autoOpen, &QCheckBox::toggled, this,
            [this](bool on) { m_context.config().setAutoOpenLastDatabase(on); });
    body->addWidget(autoOpen);
    body->addWidget(foldableHint(
        tr("Ohne Haken erscheint beim Start der Startbildschirm mit deinen Spielständen. "
           "Mit Haken lässt er sich durch Gedrückthalten der Umschalttaste beim Start "
           "trotzdem aufrufen.")));
    m_autoOpenCheck = autoOpen;

    cards->addWidget(makeCard(tr("Datenordner"), QString(), &body));
    auto *pathInfo = new QLabel(QDir::toNativeSeparators(m_context.paths().dataDir()));
    pathInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathInfo->setWordWrap(true);
    body->addWidget(pathInfo);
    body->addWidget(caption(tr("Änderbar über bootstrap.ini.")));

    finishPage(cards);
    return page;
}


void SettingsPage::refresh()
{
    AppConfig &config = m_context.config();

    // --- Club tab ---
    const int versionIndex = m_fmVersionCombo->findData(m_context.fmVersionId());
    m_fmVersionCombo->setCurrentIndex(versionIndex >= 0 ? versionIndex : 0);
    // Roles, ratings and history refer to the version's roles once players exist.
    const bool versionLocked = m_context.store().size() > 0;
    m_fmVersionCombo->setEnabled(!versionLocked);
    m_fmVersionCombo->setToolTip(
        versionLocked ? tr("Der Spielstand enthält Spieler. Für eine andere Version bitte "
                           "einen neuen Spielstand anlegen.")
                      : QString());
    m_importDirEdit->setText(QDir::toNativeSeparators(config.htmlExportDir()));
    m_importDirEdit->setPlaceholderText(
        QDir::toNativeSeparators(AppConfig::defaultHtmlExportDir(m_context.fmVersionId())));
    m_graphicsCheck->setChecked(config.graphicsEnabled());
    m_graphicsBody->setVisible(config.graphicsEnabled());
    m_attributeMaskingCheck->setChecked(config.attributeMasking());
    {
        const ImportHistory::SeasonStart start = ImportHistory::seasonStartOf(m_context.database());
        m_seasonStartEdit->setDate(QDate(2001, start.month, start.day));
    }
    m_historyAllCheck->setChecked(
        m_context.database().setting(QString::fromLatin1(ImportHistory::kAllPlayersSetting))
        == QLatin1String("1"));
    m_liveSaveCheck->setChecked(config.liveSaveEnabled());
    m_liveSaveEdit->setText(QDir::toNativeSeparators(config.liveSavePath()));
    m_graphicsDirEdit->setText(QDir::toNativeSeparators(config.graphicsDir()));
    m_graphicsDirEdit->setPlaceholderText(
        QDir::toNativeSeparators(AppConfig::defaultGraphicsDir(m_context.fmVersionId())));
    m_clubFmIdEdit->setText(m_context.database().setting(QStringLiteral("club_fm_id")));
    updateGraphicsStatus();

    QSet<QString> clubSet;
    for (const Player &player : m_context.store().players()) {
        if (!player.club.isEmpty())
            clubSet.insert(player.club);
    }
    QStringList clubs(clubSet.cbegin(), clubSet.cend());
    std::sort(clubs.begin(), clubs.end());
    const auto fillClubCombo = [&clubs](QComboBox *combo, const QString &current) {
        combo->clear();
        combo->addItem(QString());
        combo->addItems(clubs);
        if (!current.isEmpty())
            combo->setCurrentText(current);
    };
    fillClubCombo(m_userClubCombo, m_context.userClub());
    fillClubCombo(m_secondClubCombo, m_context.secondTeamClub());
    m_clubCountryEdit->setText(
        m_context.database().setting(QStringLiteral("club_country_code")));
    m_fullClubNameEdit->setText(
        m_context.database().setting(QStringLiteral("full_club_name")));
    m_stadiumEdit->setText(m_context.database().setting(QStringLiteral("stadium_name")));
    updateLogoPreview();
    updateFlagPreview();

    QStringList tactics = m_context.definitions().tacticNames();
    std::sort(tactics.begin(), tactics.end());
    const auto fillTacticCombo = [&tactics](QComboBox *combo, const QString &current) {
        combo->clear();
        combo->addItem(QString());
        combo->addItems(tactics);
        if (!current.isEmpty())
            combo->setCurrentText(current);
    };
    fillTacticCombo(m_favTactic1Combo,
                    m_context.database().setting(QStringLiteral("favorite_tactic_1")));
    fillTacticCombo(m_favTactic2Combo,
                    m_context.database().setting(QStringLiteral("favorite_tactic_2")));

    // Rule sets exist per FM version; hide the group when there are none.
    const QString fmVersion = m_context.fmVersionId();
    const auto leagues = Registration::leagueRulesFor(fmVersion);
    const bool uefaAvailable = Registration::uefaRulesFor(fmVersion);
    const Registration::Settings registration = m_context.registrationSettings();
    m_registrationCard->setVisible(!leagues.isEmpty() || uefaAvailable);
    m_registrationUnavailable->setVisible(leagues.isEmpty() && !uefaAvailable);
    m_registrationLeagueCombo->clear();
    m_registrationLeagueCombo->addItem(tr("Standard (keine Beschränkung)"),
                                       Registration::leagueRulesKey(Registration::LeagueRules::None));
    for (const Registration::LeagueRules league : leagues) {
        m_registrationLeagueCombo->addItem(Registration::leagueDisplayName(league),
                                           Registration::leagueRulesKey(league));
    }
    m_registrationLeagueCombo->setCurrentIndex(std::max(
        0, m_registrationLeagueCombo->findData(Registration::leagueRulesKey(registration.league))));
    m_registrationUefaCheck->setEnabled(uefaAvailable);
    m_registrationUefaCheck->setChecked(registration.uefa);
    m_registrationMinGkSpin->setValue(registration.minGoalkeepers);

    m_natNameEdit->setText(m_context.nationalTeamName());
    m_natCodeEdit->setText(m_context.nationalTeamCode());
    const int natAge = m_context.nationalTeamAgeLimit();
    m_natAgeSpin->setValue(natAge > 0 ? natAge : 99);
    fillTacticCombo(m_natFav1Combo,
                    m_context.database().setting(QStringLiteral("national_fav_tactic_1")));
    fillTacticCombo(m_natFav2Combo,
                    m_context.database().setting(QStringLiteral("national_fav_tactic_2")));

    const QString weightsVersion = m_context.fmVersionId();
    m_weightsVersionLabel->setText(tr("Gewichte für Spielstände von %1.")
                                       .arg(fmDataVersion(weightsVersion).displayName));
    for (auto it = m_weightSpins.begin(); it != m_weightSpins.end(); ++it)
        it.value()->setValue(config.weight(weightsVersion, it.key()));
    for (auto it = m_gkWeightSpins.begin(); it != m_gkWeightSpins.end(); ++it)
        it.value()->setValue(config.gkWeight(weightsVersion, it.key()));
    m_keyMultSpin->setValue(config.roleMultiplier(QStringLiteral("key")));
    m_prefMultSpin->setValue(config.roleMultiplier(QStringLiteral("preferable")));
    for (auto it = m_aptSpins.begin(); it != m_aptSpins.end(); ++it)
        it.value()->setValue(config.aptWeight(it.key()));

    m_outfielderAgeSpin->setValue(config.ageThreshold(QStringLiteral("outfielder")));
    m_goalkeeperAgeSpin->setValue(config.ageThreshold(QStringLiteral("goalkeeper")));
    m_naturalPosSpin->setValue(config.selectionBonus(QStringLiteral("natural_position")));
    m_ipShareSpin->setValue(config.ipSharePercent());
    m_maxDepthRolesSpin->setValue(
        config.squadManagementSetting(QStringLiteral("max_roles_per_depth_player")));
    m_minLoanTalentSpin->setValue(
        config.squadManagementSetting(QStringLiteral("min_loan_talent_score")));
    m_minLoanAgeSpin->setValue(config.squadManagementSetting(QStringLiteral("min_loan_age")));
    m_youthLoanOverAgeSpin->setValue(
        config.squadManagementSetting(QStringLiteral("youth_loan_over_age")));
    m_displacementSpin->setValue(
        config.gapAnalysisSetting(QStringLiteral("displacement_threshold")));
    m_dropoffSpin->setValue(config.gapAnalysisSetting(QStringLiteral("dropoff_threshold")));
    m_wrongSideSpin->setValue(config.gapAnalysisSetting(QStringLiteral("wrong_side_penalty")));
    m_retirementAgeSpin->setValue(config.freshnessSetting(QStringLiteral("retirement_age")));
    m_staleUploadsSpin->setValue(config.freshnessSetting(QStringLiteral("stale_after_full_uploads")));
    m_explosivePeakSpin->setValue(config.trainingSetting(QStringLiteral("explosive_peak_age")));
    m_explosiveLockedSpin->setValue(config.trainingSetting(QStringLiteral("explosive_locked_age")));
    m_strengthPeakSpin->setValue(config.trainingSetting(QStringLiteral("strength_peak_age")));
    m_strengthLockedSpin->setValue(config.trainingSetting(QStringLiteral("strength_locked_age")));
    m_technicalPeakSpin->setValue(config.trainingSetting(QStringLiteral("technical_peak_age")));
    m_technicalLockedSpin->setValue(config.trainingSetting(QStringLiteral("technical_locked_age")));

    const auto themeSettings = config.themeSettings();
    m_modeCombo->setCurrentIndex(
        themeSettings.value(QStringLiteral("current_mode")) == QLatin1String("day") ? 1 : 0);
    m_pendingPreset = themeSettings.value(QStringLiteral("theme_preset"),
                                          QStringLiteral("custom"));
    m_templates->load(); // another save's settings page may have added one
    fillPresetCombo();
    selectPresetInCombo(m_pendingPreset);
    {
        QSignalBlocker blocker(m_headerStyleCombo);
        const int style = m_headerStyleCombo->findData(
            themeSettings.value(QStringLiteral("header_style"), QStringLiteral("gradient")));
        m_headerStyleCombo->setCurrentIndex(std::max(0, style));
    }
    m_watermarkCheck->setChecked(
        themeSettings.value(QStringLiteral("header_crest_watermark"), QStringLiteral("true"))
        != QLatin1String("false"));
    m_watermarkCheck->setEnabled(m_headerStyleCombo->currentData().toString()
                                 != QLatin1String("solid"));
    m_pendingColors.clear();
    for (auto it = m_colorButtons.begin(); it != m_colorButtons.end(); ++it) {
        const QString value = themeSettings.value(it.key());
        m_pendingColors.insert(it.key(), value);
        it.value()->setText(value);
        it.value()->setStyleSheet(colorButtonStyle(QColor(value)));
    }
    updateContrastWarning();

    {
        QSignalBlocker blocker(m_autoOpenCheck);
        m_autoOpenCheck->setChecked(m_context.config().autoOpenLastDatabase());
    }
    m_dbCombo->clear();
    m_dbCombo->addItems(m_context.availableDatabases());
    m_dbCombo->setCurrentText(m_context.currentDbName());
    m_footerScope->setText(tr("Gilt für den Spielstand „%1“").arg(m_context.currentDbName()));
}

void SettingsPage::updateContrastWarning()
{
    QStringList warnings;
    for (const QString &mode : {QStringLiteral("night"), QStringLiteral("day")}) {
        const QColor text(m_pendingColors.value(mode + QStringLiteral("_text_color")));
        const QColor bg(m_pendingColors.value(mode + QStringLiteral("_background_color")));
        if (text.isValid() && bg.isValid()) {
            const double ratio = contrastRatio(text, bg);
            if (ratio < 4.5) {
                warnings << tr("%1: Kontrast Text/Hintergrund nur %2:1 (WCAG empfiehlt mindestens 4,5:1)")
                                .arg(mode == QLatin1String("night") ? tr("Nacht") : tr("Tag"))
                                .arg(ratio, 0, 'f', 1);
            }
        }
    }
    m_contrastLabel->setText(warnings.isEmpty() ? tr("Kontrast Text/Hintergrund in Ordnung")
                                                : warnings.join(QLatin1Char('\n')));
}

void SettingsPage::saveAll()
{
    AppConfig &config = m_context.config();

    // The weight spins show the version the page was refreshed for; a version
    // switch below must not redirect them to the new one.
    const QString weightsVersion = m_context.fmVersionId();
    QString versionError;
    if (!m_context.setFmVersion(m_fmVersionCombo->currentData().toString(), &versionError))
        QMessageBox::warning(this, tr("Football-Manager-Version"), versionError);

    // Everything below belongs to the open save's profile and is written as
    // one transaction: either all of it reaches the save, or nothing does.
    AppConfig::Transaction transaction(config);
    Database &db = m_context.database();
    bool clubSettingsOk = transaction.isActive();
    const auto saveSetting = [&db, &clubSettingsOk](const QString &key, const QString &value) {
        if (!clubSettingsOk)
            return;
        clubSettingsOk = value.isEmpty() ? db.removeSetting(key) : db.setSetting(key, value);
    };

    // --- Club tab (per-database settings) ---
    config.setHtmlExportDir(m_importDirEdit->text().trimmed());
    config.setGraphicsEnabled(m_graphicsCheck->isChecked());
    config.setAttributeMasking(m_attributeMaskingCheck->isChecked());
    {
        // A different save (or masking switched) is read in afresh.
        const QString livePath = QDir::fromNativeSeparators(m_liveSaveEdit->text().trimmed());
        if (livePath != config.liveSavePath() || !config.liveSaveEnabled())
            config.setLiveSaveLastModified(0);
        config.setLiveSavePath(livePath);
        config.setLiveSaveEnabled(m_liveSaveCheck->isChecked() && !livePath.isEmpty());
    }
    config.setGraphicsDir(QDir::fromNativeSeparators(m_graphicsDirEdit->text().trimmed()));
    saveSetting(QStringLiteral("club_fm_id"), m_clubFmIdEdit->text().trimmed());
    {
        const QDate d = m_seasonStartEdit->date();
        const ImportHistory::SeasonStart start{d.month(), d.day()};
        // The default is no row, so a save keeps following the default.
        saveSetting(QString::fromLatin1(ImportHistory::kSeasonStartSetting),
                    start == ImportHistory::SeasonStart{} ? QString()
                                                          : ImportHistory::seasonStartText(start));
    }
    saveSetting(QString::fromLatin1(ImportHistory::kAllPlayersSetting),
                m_historyAllCheck->isChecked() ? QStringLiteral("1") : QString());
    saveSetting(QStringLiteral("user_club"), m_userClubCombo->currentText());
    saveSetting(QStringLiteral("second_team_club"), m_secondClubCombo->currentText());
    saveSetting(QStringLiteral("club_country_code"),
                m_clubCountryEdit->text().trimmed().toUpper());
    saveSetting(QStringLiteral("full_club_name"), m_fullClubNameEdit->text().trimmed());
    saveSetting(QStringLiteral("stadium_name"), m_stadiumEdit->text().trimmed());
    saveSetting(QStringLiteral("favorite_tactic_1"), m_favTactic1Combo->currentText());
    saveSetting(QStringLiteral("favorite_tactic_2"), m_favTactic2Combo->currentText());
    const QString leagueKey = m_registrationLeagueCombo->currentData().toString();
    saveSetting(QStringLiteral("registration_league"),
                leagueKey == Registration::leagueRulesKey(Registration::LeagueRules::None)
                    ? QString()
                    : leagueKey);
    saveSetting(QStringLiteral("registration_uefa"),
                m_registrationUefaCheck->isChecked() ? QStringLiteral("true") : QString());
    saveSetting(QStringLiteral("registration_min_goalkeepers"),
                QString::number(m_registrationMinGkSpin->value()));
    saveSetting(QStringLiteral("national_team_name"), m_natNameEdit->text().trimmed());
    saveSetting(QStringLiteral("national_team_country_code"),
                m_natCodeEdit->text().trimmed().toUpper());
    saveSetting(QStringLiteral("national_team_age_limit"),
                QString::number(m_natAgeSpin->value()));
    saveSetting(QStringLiteral("national_fav_tactic_1"), m_natFav1Combo->currentText());
    saveSetting(QStringLiteral("national_fav_tactic_2"), m_natFav2Combo->currentText());

    // Detect whether any DWRS-relevant weight actually changed.
    bool weightsChanged = false;
    const auto noteChange = [&](double oldValue, double newValue) {
        if (!qFuzzyCompare(oldValue + 1.0, newValue + 1.0))
            weightsChanged = true;
    };

    for (auto it = m_weightSpins.begin(); it != m_weightSpins.end(); ++it) {
        noteChange(config.weight(weightsVersion, it.key()), it.value()->value());
        config.setWeight(weightsVersion, it.key(), it.value()->value());
    }
    for (auto it = m_gkWeightSpins.begin(); it != m_gkWeightSpins.end(); ++it) {
        noteChange(config.gkWeight(weightsVersion, it.key()), it.value()->value());
        config.setGkWeight(weightsVersion, it.key(), it.value()->value());
    }
    noteChange(config.roleMultiplier(QStringLiteral("key")), m_keyMultSpin->value());
    config.setRoleMultiplier(QStringLiteral("key"), m_keyMultSpin->value());
    noteChange(config.roleMultiplier(QStringLiteral("preferable")), m_prefMultSpin->value());
    config.setRoleMultiplier(QStringLiteral("preferable"), m_prefMultSpin->value());
    for (auto it = m_aptSpins.begin(); it != m_aptSpins.end(); ++it)
        config.setAptWeight(it.key(), it.value()->value());

    config.setAgeThreshold(QStringLiteral("outfielder"), m_outfielderAgeSpin->value());
    config.setAgeThreshold(QStringLiteral("goalkeeper"), m_goalkeeperAgeSpin->value());
    config.setSelectionBonus(QStringLiteral("natural_position"), m_naturalPosSpin->value());
    config.setIpSharePercent(m_ipShareSpin->value());
    config.setSquadManagementSetting(QStringLiteral("max_roles_per_depth_player"),
                                     m_maxDepthRolesSpin->value());
    config.setSquadManagementSetting(QStringLiteral("min_loan_talent_score"),
                                     m_minLoanTalentSpin->value());
    config.setSquadManagementSetting(QStringLiteral("min_loan_age"), m_minLoanAgeSpin->value());
    config.setSquadManagementSetting(QStringLiteral("youth_loan_over_age"),
                                     m_youthLoanOverAgeSpin->value());
    config.setGapAnalysisSetting(QStringLiteral("displacement_threshold"),
                                 m_displacementSpin->value());
    config.setGapAnalysisSetting(QStringLiteral("dropoff_threshold"), m_dropoffSpin->value());
    config.setGapAnalysisSetting(QStringLiteral("wrong_side_penalty"), m_wrongSideSpin->value());
    config.setFreshnessSetting(QStringLiteral("retirement_age"), m_retirementAgeSpin->value());
    config.setFreshnessSetting(QStringLiteral("stale_after_full_uploads"), m_staleUploadsSpin->value());
    config.setTrainingSetting(QStringLiteral("explosive_peak_age"), m_explosivePeakSpin->value());
    config.setTrainingSetting(QStringLiteral("explosive_locked_age"),
                              m_explosiveLockedSpin->value());
    config.setTrainingSetting(QStringLiteral("strength_peak_age"), m_strengthPeakSpin->value());
    config.setTrainingSetting(QStringLiteral("strength_locked_age"),
                              m_strengthLockedSpin->value());
    config.setTrainingSetting(QStringLiteral("technical_peak_age"), m_technicalPeakSpin->value());
    config.setTrainingSetting(QStringLiteral("technical_locked_age"),
                              m_technicalLockedSpin->value());

    auto themeSettings = config.themeSettings();
    themeSettings.insert(QStringLiteral("current_mode"),
                         m_modeCombo->currentData().toString());
    themeSettings.insert(QStringLiteral("theme_preset"),
                         m_pendingPreset.isEmpty() ? QStringLiteral("custom")
                                                   : m_pendingPreset);
    for (auto it = m_pendingColors.constBegin(); it != m_pendingColors.constEnd(); ++it)
        themeSettings.insert(it.key(), it.value());
    themeSettings.insert(QStringLiteral("header_style"),
                         m_headerStyleCombo->currentData().toString());
    themeSettings.insert(QStringLiteral("header_crest_watermark"),
                         m_watermarkCheck->isChecked() ? QStringLiteral("true")
                                                       : QStringLiteral("false"));
    config.saveThemeSettings(themeSettings);

    QString error = clubSettingsOk ? QString() : db.errorString();
    if (!clubSettingsOk || !transaction.commit(&error)) {
        // Nothing was written; the page shows the stored state again.
        QMessageBox::critical(
            this, tr("Einstellungen"),
            tr("Die Einstellungen konnten nicht gespeichert werden. Der Spielstand behält "
               "seinen bisherigen Stand.\n\n%1")
                .arg(error.isEmpty() ? config.lastError() : error));
        refresh();
        return;
    }

    m_context.reloadEngines();
    m_context.applyGraphicsSettings();
    m_theme.setMode(m_modeCombo->currentData().toString());
    m_theme.apply();
    m_context.notifySettingsChanged();

    if (weightsChanged) {
        emit recalcRequested();
    } else {
        QMessageBox::information(this, tr("Einstellungen"), tr("Einstellungen gespeichert."));
    }
}

} // namespace fm
