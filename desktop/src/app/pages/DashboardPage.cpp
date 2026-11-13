#include "DashboardPage.h"
#include "core/DwrsDisplay.h"
#include "core/Registration.h"
#include "core/ImportHistory.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../theming/ThemeManager.h"
#include "../widgets/NumericTableItem.h"
#include "../widgets/StrengthGridWidget.h"
#include "PageHelpers.h"
#include "core/Database.h"
#include "core/PlayerStatus.h"
#include "core/Utils.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fm {

namespace {

// Opens a menu on left or right click anywhere on a card (the labels inside
// pass their clicks on to the card).
class CardClickFilter : public QObject
{
public:
    CardClickFilter(std::function<void(const QPoint &)> open, QObject *parent)
        : QObject(parent), m_open(std::move(open)) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_open(mouse->globalPosition().toPoint());
                return true;
            }
        } else if (event->type() == QEvent::ContextMenu) {
            m_open(static_cast<QContextMenuEvent *>(event)->globalPos());
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void(const QPoint &)> m_open;
};

void installClickMenu(QWidget *card, std::function<void(const QPoint &)> open)
{
    card->installEventFilter(new CardClickFilter(std::move(open), card));
}

QString formatMillions(double value)
{
    return QStringLiteral("%1 Mio. €")
        .arg(QLocale().toString(value / 1'000'000.0, 'f', 2));
}

// One "player has left" resolution row. destination = new club, or the status
// strings "FrA" (free agent) / "Retired".
struct DepartureChoice {
    QString uid;
    bool left = false;
    QString destination;
};

// The most common outcome for a departed player: released as a free agent.
inline QString freeAgentTag() { return PlayerStatus::freeAgentClubTag(); }
inline QString retiredTag() { return PlayerStatus::retiredClubTag(); }

// Modal dialog to resolve players missing from a full squad export
// (legacy "Action Required: Player Departures" form). Every detected player
// defaults to "left → FrA" since that fits the yearly wave of departing youth;
// the user only unchecks the ones who stayed and edits the high-profile moves.
class DepartureDialog : public QDialog
{
    // Not a Q_OBJECT (local class); this gives tr() a real translation context
    // ("DepartureDialog") so its strings resolve at runtime instead of silently
    // falling back to the QDialog context.
    Q_DECLARE_TR_FUNCTIONS(DepartureDialog)

public:
    DepartureDialog(const std::vector<const Player *> &missing, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr("Spieler-Abgänge klären"));
        setMinimumWidth(640);

        auto *layout = new QVBoxLayout(this);
        auto *info = new QLabel(
            tr("Diese Spieler stehen in der Datenbank bei deinem Verein, waren aber "
               "nicht in der hochgeladenen Kader-Datei. Standardmäßig gelten alle als "
               "gegangen und werden zu Free Agents (FrA) — hake die aus, die geblieben "
               "sind, und trage bei den anderen bei Bedarf einen neuen Verein oder "
               "'Retired' ein."),
            this);
        info->setWordWrap(true);
        layout->addWidget(info);

        auto *toggleAll = new QCheckBox(tr("Alle als gegangen markieren"), this);
        toggleAll->setChecked(true);
        layout->addWidget(toggleAll);

        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *content = new QWidget;
        auto *grid = new QGridLayout(content);
        grid->setColumnStretch(2, 1);

        auto *hName = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Spieler")), content);
        auto *hGone = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Gegangen")), content);
        auto *hDest = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Neuer Verein / Status")),
                                 content);
        grid->addWidget(hName, 0, 0);
        grid->addWidget(hGone, 0, 1);
        grid->addWidget(hDest, 0, 2);

        int row = 1;
        for (const Player *player : missing) {
            auto *name = new QLabel(QStringLiteral("<b>%1</b> (%2)")
                                        .arg(player->name.toHtmlEscaped(),
                                             player->club.toHtmlEscaped()),
                                    content);
            auto *gone = new QCheckBox(content);
            gone->setChecked(true);
            auto *dest = new QComboBox(content);
            dest->setEditable(true);
            dest->addItem(freeAgentTag());
            dest->addItem(retiredTag());
            dest->setCurrentText(freeAgentTag());
            dest->setToolTip(tr("Neuer Verein eintippen oder Status wählen "
                                "(FrA = Free Agent, Retired = Karriereende)."));
            connect(gone, &QCheckBox::toggled, dest, &QComboBox::setEnabled);

            grid->addWidget(name, row, 0);
            grid->addWidget(gone, row, 1, Qt::AlignHCenter);
            grid->addWidget(dest, row, 2);
            m_rows.push_back({player->uid, gone, dest});
            ++row;
        }
        scroll->setWidget(content);
        layout->addWidget(scroll, 1);

        connect(toggleAll, &QCheckBox::toggled, this, [this](bool checked) {
            for (const Row &r : m_rows)
                r.gone->setChecked(checked);
        });

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             this);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Abgänge bestätigen"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    std::vector<DepartureChoice> choices() const
    {
        std::vector<DepartureChoice> result;
        for (const Row &row : m_rows)
            result.push_back({row.uid, row.gone->isChecked(), row.dest->currentText().trimmed()});
        return result;
    }

private:
    struct Row {
        QString uid;
        QCheckBox *gone;
        QComboBox *dest;
    };
    std::vector<Row> m_rows;
};

} // namespace

DashboardPage::DashboardPage(AppContext &context, ThemeManager &theme, QWidget *parent)
    : PageBase(context, parent)
    , m_theme(theme)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);

    auto *heading = new QLabel(tr("Übersicht"), content);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);

    layout->addWidget(buildImportSection(content));
    layout->addWidget(buildAnalysisHeader(content));

    m_kpiRow = buildKpiRow(content);
    layout->addWidget(m_kpiRow);

    m_squadSection = buildSquadSection(content);
    layout->addWidget(m_squadSection);

    m_suggestionBox = qobject_cast<QGroupBox *>(buildSuggestionSection(content));
    layout->addWidget(m_suggestionBox);

    layout->addStretch(1);
    scroll->setWidget(content);

    m_suggestionDebounce = new QTimer(this);
    m_suggestionDebounce->setSingleShot(true);
    m_suggestionDebounce->setInterval(180);
    connect(m_suggestionDebounce, &QTimer::timeout, this, &DashboardPage::rebuildSuggestions);
}

QWidget *DashboardPage::buildImportSection(QWidget *parent)
{
    auto *box = new QGroupBox(tr("Neue Spielerdaten importieren (FM-Export, HTML oder CSV)"), parent);
    auto *layout = new QVBoxLayout(box);

    auto *fileRow = new QHBoxLayout;
    m_filePathEdit = new QLineEdit(box);
    m_filePathEdit->setReadOnly(true);
    m_filePathEdit->setPlaceholderText(tr("Keine Datei ausgewählt…"));
    auto *browseButton = new QPushButton(tr("Durchsuchen…"), box);
    m_importButton = new QPushButton(tr("Importieren"), box);
    m_importButton->setEnabled(false);
    m_importButton->setDefault(true);
    fileRow->addWidget(m_filePathEdit, 1);
    fileRow->addWidget(browseButton);
    fileRow->addWidget(m_importButton);
    layout->addLayout(fileRow);

    auto *optionsRow = new QHBoxLayout;
    m_squadUpdateCheck = new QCheckBox(
        tr("Datei ist ein kompletter Kader-Export meines Vereins (Abgänge erkennen)"), box);
    m_squadUpdateCheck->setToolTip(
        tr("Aktivieren, wenn die Datei NUR Spieler deines Haupt- und Zweitteams enthält. "
           "Die App erkennt Spieler, die den Verein verlassen haben, und fragt nach."));
    m_autoAssignCheck = new QCheckBox(
        tr("Neuen/unzugeordneten Spielern automatisch Rollen zuweisen"), box);
    m_autoAssignCheck->setChecked(true);
    optionsRow->addWidget(m_squadUpdateCheck);
    optionsRow->addWidget(m_autoAssignCheck);
    optionsRow->addStretch(1);
    layout->addLayout(optionsRow);

    connect(browseButton, &QPushButton::clicked, this, &DashboardPage::chooseImportFile);
    connect(m_importButton, &QPushButton::clicked, this, &DashboardPage::startImport);
    return box;
}

QWidget *DashboardPage::buildAnalysisHeader(QWidget *parent)
{
    auto *widget = new QWidget(parent);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *row = new QHBoxLayout;
    auto *title = new QLabel(tr("Kader-Analyse"), widget);
    title->setObjectName(QStringLiteral("sectionTitle"));
    row->addWidget(title);
    row->addSpacing(24);

    row->addWidget(new QLabel(tr("Mein Verein:"), widget));
    m_clubCombo = new QComboBox(widget);
    m_clubCombo->setMinimumWidth(220);
    m_clubCombo->setMaxVisibleItems(25);
    row->addWidget(m_clubCombo);

    row->addSpacing(16);
    row->addWidget(new QLabel(tr("Analyse-Taktik:"), widget));
    m_tacticCombo = new QComboBox(widget);
    m_tacticCombo->setMinimumWidth(220);
    row->addWidget(m_tacticCombo);
    row->addStretch(1);
    layout->addLayout(row);

    m_analysisHint = new QLabel(widget);
    m_analysisHint->setWordWrap(true);
    m_analysisHint->hide();
    layout->addWidget(m_analysisHint);

    connect(m_clubCombo, &QComboBox::activated, this, [this] {
        if (m_updatingCombos)
            return;
        m_context.database().setSetting(QStringLiteral("user_club"),
                                        m_clubCombo->currentText());
        refreshAnalysis();
        m_context.notifySettingsChanged(); // header and other pages follow the club
    });
    connect(m_tacticCombo, &QComboBox::activated, this, [this] {
        if (!m_updatingCombos)
            refreshAnalysis();
    });
    return widget;
}

QWidget *DashboardPage::buildKpiRow(QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    const auto makeTile = [&](const QString &caption, QLabel **valueOut,
                              QLabel **captionOut = nullptr, QFrame **tileOut = nullptr) {
        auto *tile = new QFrame(row);
        tile->setObjectName(QStringLiteral("kpiTile"));
        auto *tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(16, 12, 16, 12);
        auto *value = new QLabel(QStringLiteral("–"), tile);
        value->setObjectName(QStringLiteral("kpiValue"));
        auto *captionLabel = new QLabel(caption, tile);
        captionLabel->setObjectName(QStringLiteral("kpiCaption"));
        tileLayout->addWidget(value);
        tileLayout->addWidget(captionLabel);
        layout->addWidget(tile, 1);
        *valueOut = value;
        if (captionOut)
            *captionOut = captionLabel;
        if (tileOut)
            *tileOut = tile;
    };

    makeTile(tr("Spieler im Kern-Kader"), &m_kpiPlayers);
    makeTile(tr("Gesamtwert Kader"), &m_kpiTotalValue);
    makeTile(tr("Ø Spielerwert"), &m_kpiAvgValue);
    makeTile(tr("Ø Alter"), &m_kpiAvgAge);
    makeTile(QString(), &m_kpiGameDate, &m_kpiGameDateCaption, &m_kpiGameDateTile);
    m_kpiGameDateTile->setVisible(false);
    return row;
}

QWidget *DashboardPage::buildSquadSection(QWidget *parent)
{
    auto *widget = new QWidget(parent);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *gridColumn = new QVBoxLayout;
    auto *gridTitle = new QLabel(tr("Positionsstärke"), widget);
    gridTitle->setObjectName(QStringLiteral("sectionTitle"));
    gridTitle->setAlignment(Qt::AlignHCenter);
    gridColumn->addWidget(gridTitle);
    // Tactical view of a two-phase tactic (hidden for one line-up).
    auto *phases = new QFrame(widget);
    m_phaseSwitch = phases;
    phases->setObjectName(QStringLiteral("segmented"));
    auto *phaseLayout = new QHBoxLayout(phases);
    phaseLayout->setContentsMargins(3, 3, 3, 3);
    phaseLayout->setSpacing(3);
    auto *phaseGroup = new QButtonGroup(widget);
    const QStringList views = {tr("Kombiniert"), tr("Mit Ball"), tr("Ohne Ball"), tr("Beide")};
    for (int i = 0; i < views.size(); ++i) {
        auto *button = new QPushButton(views.at(i), phases);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("segmentButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setChecked(i == m_phaseView);
        phaseGroup->addButton(button, i);
        phaseLayout->addWidget(button);
    }
    connect(phaseGroup, &QButtonGroup::idClicked, this, [this](int view) {
        m_phaseView = view;
        showStarterView();
    });
    phases->setVisible(false);
    gridColumn->addWidget(phases, 0, Qt::AlignHCenter);
    // Colouring: level (rarity tiers) or squad comparison; stored per save.
    auto *colorRow = new QHBoxLayout;
    colorRow->setSpacing(6);
    auto *colorLabel = new QLabel(tr("Färben nach:"), widget);
    colorLabel->setObjectName(QStringLiteral("kpiCaption"));
    colorRow->addStretch(1);
    colorRow->addWidget(colorLabel);
    auto *colorModes = new QFrame(widget);
    colorModes->setObjectName(QStringLiteral("segmented"));
    auto *colorLayout = new QHBoxLayout(colorModes);
    colorLayout->setContentsMargins(3, 3, 3, 3);
    colorLayout->setSpacing(3);
    m_colorModeGroup = new QButtonGroup(widget);
    const QStringList colorViews = {tr("Niveau"), tr("Kadervergleich")};
    for (int i = 0; i < colorViews.size(); ++i) {
        auto *button = new QPushButton(colorViews.at(i), colorModes);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("segmentButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setChecked(i == 0);
        button->setToolTip(i == 0 ? tr("Farbstufen wie überall in der App: Schwach bis Legendär.")
                                  : tr("Jeder Starter im Vergleich zur Kadermitte (Median der gezeigten "
                                       "Starter): grau bis 1,5 Punkte Abstand, darunter orange, darüber "
                                       "blau, ab 4 Punkten kräftig."));
        m_colorModeGroup->addButton(button, i);
        colorLayout->addWidget(button);
    }
    colorRow->addWidget(colorModes);
    colorRow->addStretch(1);
    connect(m_colorModeGroup, &QButtonGroup::idClicked, this, [this](int mode) {
        m_context.database().setSetting(QStringLiteral("dashboard_strength_colors"),
                                        mode == 1 ? QStringLiteral("squad") : QStringLiteral("level"));
        applyStrengthColorMode();
    });
    gridColumn->addLayout(colorRow);
    auto *gridRow = new QHBoxLayout;
    const auto pitchColumn = [&](QLabel *&title, StrengthGridWidget *&grid) {
        auto *column = new QVBoxLayout;
        title = new QLabel(widget);
        title->setAlignment(Qt::AlignHCenter);
        title->setVisible(false);
        grid = new StrengthGridWidget(m_theme, widget);
        grid->setPitchWidth(300);
        column->addWidget(title);
        column->addWidget(grid, 0, Qt::AlignHCenter | Qt::AlignTop);
        gridRow->addLayout(column);
        connect(grid, &StrengthGridWidget::playerDoubleClicked, this,
                [this](const QString &uid) { PlayerActions::openProfile(m_context, uid); });
        connect(grid, &StrengthGridWidget::playerContextMenuRequested, this,
                [this](const QString &uid, const QPoint &pos) {
                    PlayerActions::showContextMenu(m_context, this, uid, pos);
                });
    };
    pitchColumn(m_gridTitle, m_strengthGrid);
    pitchColumn(m_gridTitleOop, m_strengthGridOop);
    m_strengthGridOop->setVisible(false);
    gridColumn->addLayout(gridRow);
    m_gridLegend = new QLabel(widget);
    m_gridLegend->setObjectName(QStringLiteral("kpiCaption"));
    m_gridLegend->setWordWrap(true);
    m_gridLegend->setFixedWidth(300);
    gridColumn->addWidget(m_gridLegend, 0, Qt::AlignHCenter);
    m_colorLegend = new QLabel(widget);
    m_colorLegend->setObjectName(QStringLiteral("kpiCaption"));
    m_colorLegend->setTextFormat(Qt::RichText);
    m_colorLegend->setAlignment(Qt::AlignHCenter);
    m_colorLegend->setWordWrap(true);
    m_colorLegend->setVisible(false);
    gridColumn->addWidget(m_colorLegend, 0, Qt::AlignHCenter);
    gridColumn->addStretch(1);
    layout->addLayout(gridColumn);

    auto *tableColumn = new QVBoxLayout;
    m_clubTableTitle = new QLabel(widget);
    m_clubTableTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_clubTable = new QTableWidget(widget);
    m_clubTable->setColumnCount(7);
    m_clubTable->setHorizontalHeaderLabels({tr("Name"), tr("Alter"), tr("Position"),
                                            tr("Persönlichkeit"), tr("Spielzeit"),
                                            tr("Ø-Note"), tr("Marktwert")});
    m_clubTable->verticalHeader()->setVisible(false);
    m_clubTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_clubTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_clubTable->setAlternatingRowColors(true);
    m_clubTable->setSortingEnabled(true);
    m_clubTable->horizontalHeader()->setStretchLastSection(true);
    m_clubTable->setMinimumHeight(430);
    PlayerActions::attachToTableWidget(m_context, m_clubTable);
    tableColumn->addWidget(m_clubTableTitle);
    tableColumn->addWidget(m_clubTable, 1);
    layout->addLayout(tableColumn, 1);

    return widget;
}

QWidget *DashboardPage::buildSuggestionSection(QWidget *parent)
{
    auto *box = new QGroupBox(tr("Transferziele"), parent);
    auto *layout = new QVBoxLayout(box);

    auto *info = new QLabel(tr("Mögliche Verstärkungen aus deiner Scouting-Datenbank, "
                               "basierend auf den Rollen der gewählten Taktik."),
                            box);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Max. Alter:"), box));
    m_ageSlider = new QSlider(Qt::Horizontal, box);
    m_ageSlider->setRange(15, 40);
    m_ageSlider->setValue(28);
    m_ageLabel = new QLabel(box);
    m_ageLabel->setMinimumWidth(70);
    filterRow->addWidget(m_ageSlider, 1);
    filterRow->addWidget(m_ageLabel);
    filterRow->addSpacing(20);
    filterRow->addWidget(new QLabel(tr("Max. Marktwert:"), box));
    m_valueSlider = new QSlider(Qt::Horizontal, box);
    m_valueSlider->setRange(0, 400); // 0.5 Mio € steps -> 0..200 Mio €
    m_valueSlider->setValue(20);     // 10 Mio €
    m_valueLabel = new QLabel(box);
    m_valueLabel->setMinimumWidth(110);
    filterRow->addWidget(m_valueSlider, 1);
    filterRow->addWidget(m_valueLabel);
    layout->addLayout(filterRow);

    m_suggestionStatus = new QLabel(box);
    m_suggestionStatus->setWordWrap(true);
    layout->addWidget(m_suggestionStatus);

    auto *cardsHost = new QWidget(box);
    m_cardsLayout = new QGridLayout(cardsHost);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(12);
    layout->addWidget(cardsHost);

    const auto updateLabels = [this] {
        m_ageLabel->setText(tr("%1 Jahre").arg(m_ageSlider->value()));
        m_valueLabel->setText(formatMillions(maxValueFilter()));
    };
    updateLabels();
    connect(m_ageSlider, &QSlider::valueChanged, this, [this, updateLabels] {
        updateLabels();
        m_suggestionDebounce->start();
    });
    connect(m_valueSlider, &QSlider::valueChanged, this, [this, updateLabels] {
        updateLabels();
        m_suggestionDebounce->start();
    });
    return box;
}

QString DashboardPage::selectedTactic() const
{
    return m_tacticCombo ? m_tacticCombo->currentText() : QString();
}

double DashboardPage::maxValueFilter() const
{
    return m_valueSlider->value() * 500'000.0;
}

void DashboardPage::applyStrengthColorMode()
{
    const bool squad = m_context.database().setting(QStringLiteral("dashboard_strength_colors"))
                       == QLatin1String("squad");
    if (auto *button = m_colorModeGroup->button(squad ? 1 : 0))
        button->setChecked(true);
    const auto mode = squad ? StrengthGridWidget::ColorMode::SquadComparison
                            : StrengthGridWidget::ColorMode::Level;
    m_strengthGrid->setColorMode(mode);
    m_strengthGridOop->setColorMode(mode);
    updateColorLegend();
}

void DashboardPage::updateColorLegend()
{
    const bool squad = m_strengthGrid->colorMode() == StrengthGridWidget::ColorMode::SquadComparison;
    const double median = m_strengthGrid->shownMedian();
    m_colorLegend->setVisible(squad && median > 0.0);
    if (!m_colorLegend->isVisibleTo(m_colorLegend->parentWidget()))
        return;
    // Five swatches from far below to far above the squad median.
    const QList<QColor> colors = dwrsComparisonColors();
    const QStringList labels = {tr("weit darunter"), tr("darunter"), tr("Kadermitte"),
                                tr("darüber"), tr("weit darüber")};
    QStringList parts;
    for (int i = 0; i < colors.size() && i < labels.size(); ++i)
        parts << QStringLiteral("<span style=\"color:%1\">&#9632;</span>&nbsp;%2")
                     .arg(colors.at(i).name(), labels.at(i).toHtmlEscaped());
    QString text = parts.join(QStringLiteral(" &nbsp; "));
    const bool both = m_strengthGridOop->isVisibleTo(m_strengthGridOop->parentWidget());
    if (!both)
        text = tr("Kadermitte: %1").arg(formatDwrs(median)).toHtmlEscaped() + QStringLiteral("<br>") + text;
    m_colorLegend->setFixedWidth(m_gridLegend->width());
    m_colorLegend->setText(text);
}

void DashboardPage::refresh()
{
    applyStrengthColorMode();
    refreshCombos();
    refreshAnalysis();
}

void DashboardPage::refreshCombos()
{
    m_updatingCombos = true;

    // Distinct clubs, sorted; keep the persisted selection.
    const QString savedClub = m_context.userClub();
    QSet<QString> clubSet;
    for (const Player &player : m_context.store().players()) {
        if (!player.club.isEmpty())
            clubSet.insert(player.club);
    }
    QStringList clubs(clubSet.cbegin(), clubSet.cend());
    std::sort(clubs.begin(), clubs.end());
    m_clubCombo->clear();
    m_clubCombo->addItem(QString()); // "no club selected"
    m_clubCombo->addItems(clubs);
    if (!savedClub.isEmpty())
        m_clubCombo->setCurrentText(savedClub);

    // Tactics sorted alphabetically (legacy), favorite tactic preselected.
    const QString previous = m_tacticCombo->currentText();
    QStringList tactics = m_context.definitions().tacticNames();
    std::sort(tactics.begin(), tactics.end());
    m_tacticCombo->clear();
    m_tacticCombo->addItems(tactics);
    QString wanted = previous;
    if (wanted.isEmpty())
        wanted = m_context.database().setting(QStringLiteral("favorite_tactic_1"));
    if (!wanted.isEmpty() && tactics.contains(wanted))
        m_tacticCombo->setCurrentText(wanted);

    m_updatingCombos = false;
}

void DashboardPage::refreshAnalysis()
{
    const QString userClub = m_context.userClub();
    const QString tactic = selectedTactic();

    if (userClub.isEmpty() || m_context.store().isEmpty()) {
        m_analysisHint->setText(
            tr("Bitte importiere Spielerdaten und wähle oben deinen Verein, "
               "um die Kader-Analyse zu sehen."));
        m_analysisHint->show();
        m_kpiRow->hide();
        m_squadSection->hide();
        m_suggestionBox->hide();
        return;
    }
    m_analysisHint->hide();
    m_kpiRow->show();
    m_squadSection->show();
    m_suggestionBox->show();

    // Club pool in store order (mirrors the legacy list order).
    std::vector<const Player *> clubPlayers;
    for (const Player &player : m_context.store().players()) {
        if (player.club == userClub)
            clubPlayers.push_back(&player);
    }
    updateClubTable(clubPlayers);

    const auto positions = m_context.definitions().tacticRoles().value(tactic);
    if (clubPlayers.empty() || positions.isEmpty()) {
        m_kpiRow->hide();
        m_starters = StarterView();
        m_strengthGrid->clearData();
        m_strengthGridOop->clearData();
        m_analysisHint->setText(
            tr("Für die Taktik '%1' konnte kein Kader gebildet werden — es gibt "
               "keine passenden Spieler in deinem Verein.")
                .arg(tactic));
        m_analysisHint->show();
        clearSuggestionCards();
        m_suggestionStatus->clear();
        return;
    }

    const QStringList slotOrder = m_context.definitions().tacticSlotOrder(tactic);
    const SquadResult squad = m_context.squadBuilder().calculateSquadForTactic(
        clubPlayers, tactic, m_context.ratings());

    // Core squad players (XI + B-team + depth).
    std::vector<const Player *> coreSquad;
    for (const Player *player : clubPlayers) {
        if (squad.coreSquadUids.contains(player->uid))
            coreSquad.push_back(player);
    }
    if (coreSquad.empty()) {
        m_kpiRow->hide();
        m_starters = StarterView();
        m_strengthGrid->clearData();
        m_strengthGridOop->clearData();
        m_analysisHint->setText(
            tr("Für die Taktik '%1' konnte kein Kader gebildet werden — es gibt "
               "keine passenden Spieler in deinem Verein.")
                .arg(tactic));
        m_analysisHint->show();
    } else {
        updateKpis(coreSquad);
    }

    // Positional strength: the starter per place and what his absence costs.
    updateStarterView(clubPlayers, tactic);

    rebuildSuggestions();
}

void DashboardPage::updateStarterView(const std::vector<const Player *> &clubPlayers,
                                      const QString &tactic)
{
    // The same starting XI as Best XI without a competition: the whole club
    // without the long-term injured, with the manual places of Best XI.
    // Without a squad list, players marked "not registered" stay out too.
    const bool skipUnregistered =
        !Registration::restrictsSquad(m_context.registrationSettings().league);
    std::vector<const Player *> pool;
    for (const Player *player : clubPlayers) {
        if (!player->longTermInjured && !(skipUnregistered && player->registration.leagueBlocked))
            pool.push_back(player);
    }
    const Definitions &definitions = m_context.definitions();
    const RoleRatings &ratings = m_context.ratings();
    const SquadBuilder &builder = m_context.squadBuilder();
    const auto bridges = loadManualBridges(m_context, tactic, &pool);
    // The cover search tries every bench player for every starter; showing
    // the page again with unchanged data reuses the last result.
    QStringList keyParts{QString::number(m_context.dataGeneration()), tactic,
                         m_context.fmVersionId(),
                         QString::number(m_context.config().ipSharePercent())};
    for (auto it = bridges.cbegin(); it != bridges.cend(); ++it)
        keyParts << it.key() + QLatin1Char('=') + it->first + QLatin1Char('/') + it->second;
    for (const Player *player : pool)
        keyParts << player->uid;
    const QString key = keyParts.join(QLatin1Char('|'));
    if (m_starters.valid && m_starters.key == key) {
        showStarterView();
        return;
    }
    StarterView view;
    view.key = key;
    view.valid = true;
    view.tactic = tactic;
    view.twoPhase = definitions.isTwoPhaseTactic(tactic);
    view.ipShare = m_context.config().ipSharePercent();
    if (view.twoPhase) {
        const TwoPhaseSquadResult squad = builder.calculateTwoPhaseSquad(
            pool, tactic, ratings, view.ipShare / 100.0, true, bridges);
        view.ip = squad.startingIp;
        view.oop = squad.startingOop;
        view.slotsOfUid = squad.slotsOfUid;
        view.valueOfUid = squad.valueOfUid;
    } else {
        view.xi = builder.calculateSquadForTactic(pool, tactic, ratings).startingXi;
    }

    const auto nameOf = [this](const QString &uid) {
        const Player *p = m_context.store().findByUid(uid);
        return p ? getLastName(p->name) : uid;
    };
    const bool twoPhase = view.twoPhase;
    const auto places = [twoPhase](const QPair<QString, QString> &pair) {
        return twoPhase ? tr("mit %1 · ohne %2").arg(pair.first, pair.second) : pair.first;
    };
    const auto delta = [](double value) {
        const QString text = QLocale().toString(std::abs(value), 'f', 1);
        return value <= -0.05 ? QStringLiteral("−") + text
               : value >= 0.05 ? QStringLiteral("+") + text
                               : QStringLiteral("±0");
    };
    const QList<OutageCover> covers =
        builder.replacementPlans(pool, tactic, ratings, true, bridges);
    for (const OutageCover &cover : covers) {
        const ReplacementPlan &plan = cover.plans.front();
        QStringList lines;
        lines << tr("Fällt %1 aus:").arg(nameOf(cover.outUid));
        if (!plan.complete) {
            view.impact.insert(cover.outUid, tr("Lücke"));
            lines << tr("Kein Spieler im Kader füllt die Lücke, ein Platz bleibt frei.");
        } else {
            view.impact.insert(cover.outUid, tr("Elf %1").arg(delta(plan.total - cover.current)));
            lines << tr("%1 kommt (%2).").arg(nameOf(plan.inUid), places(plan.inSlots));
            for (const PlaceMove &move : plan.moves)
                lines << tr("%1 rückt um: %2 → %3.")
                             .arg(nameOf(move.uid), places(move.from), places(move.to));
            lines << tr("Startelf %1 DWRS-Punkte (Summe aller elf).")
                         .arg(delta(plan.total - cover.current));
        }
        view.tooltip.insert(cover.outUid, lines.join(QLatin1Char('\n')));
    }
    m_starters = view;
    showStarterView();
}

void DashboardPage::showStarterView()
{
    const StarterView &view = m_starters;
    m_phaseSwitch->setVisible(view.valid && view.twoPhase);
    if (!view.valid) {
        m_strengthGrid->clearData();
        m_strengthGridOop->clearData();
        return;
    }
    const Definitions &definitions = m_context.definitions();
    const QString version = m_context.fmVersionId();
    const auto &slotMap = masterPositionMap(version);
    const auto layoutOf = [&](const QHash<QString, QString> &lineUp) {
        QHash<QString, QStringList> strata;
        for (auto it = lineUp.cbegin(); it != lineUp.cend(); ++it) {
            if (it.key() != QLatin1String("GK"))
                strata[slotMap.value(it.key()).stratum].append(it.key());
        }
        return strata;
    };
    const auto abbr = [](QString role) {
        if (role.endsWith(QLatin1String("-IP")))
            role.chop(3);
        else if (role.endsWith(QLatin1String("-OOP")))
            role.chop(4);
        return role;
    };
    // One line-up: each starter at his slot with that line-up's role DWRS.
    const auto singleMarkers = [&](const QHash<QString, XiCell> &team,
                                   const QHash<QString, QString> &lineUp) {
        QList<StrengthGridWidget::Marker> markers;
        const QHash<QString, QPointF> anchors = slotAnchors(version, layoutOf(lineUp), true);
        for (auto it = lineUp.cbegin(); it != lineUp.cend(); ++it) {
            if (!anchors.contains(it.key()))
                continue;
            StrengthGridWidget::Marker marker;
            marker.anchor = anchors.value(it.key());
            marker.header = abbr(it.value());
            marker.goalkeeper = it.key() == QLatin1String("GK");
            const XiCell cell = team.value(it.key());
            if (cell.isFilled()) {
                marker.uid = cell.playerUid;
                marker.value = cell.rating;
                marker.impact = view.impact.value(cell.playerUid);
                marker.tooltip = cell.name + QLatin1Char('\n')
                                 + view.tooltip.value(cell.playerUid);
            } else {
                marker.impact = tr("frei");
                marker.tooltip = tr("Für %1 hat der Kader keinen Spieler.").arg(it.key());
            }
            markers.append(marker);
        }
        return markers;
    };

    const bool both = view.twoPhase && m_phaseView == 3;
    m_strengthGridOop->setVisible(both);
    m_gridTitle->setVisible(both);
    m_gridTitleOop->setVisible(both);
    const int width = both ? 280 : 300;
    m_strengthGrid->setPitchWidth(width);
    m_strengthGridOop->setPitchWidth(width);
    for (StrengthGridWidget *grid : {m_strengthGrid, m_strengthGridOop})
        grid->setFmVersionId(version);

    if (!view.twoPhase) {
        m_strengthGrid->setMarkers(singleMarkers(view.xi, definitions.tacticRoles().value(view.tactic)));
    } else {
        const auto &lineUps = definitions.twoPhaseTactics().value(view.tactic);
        if (m_phaseView == 1) {
            m_strengthGrid->setMarkers(singleMarkers(view.ip, lineUps.ip));
        } else if (m_phaseView == 2) {
            m_strengthGrid->setMarkers(singleMarkers(view.oop, lineUps.oop));
        } else if (both) {
            m_gridTitle->setText(tr("Mit Ball"));
            m_gridTitleOop->setText(tr("Ohne Ball"));
            m_strengthGrid->setMarkers(singleMarkers(view.ip, lineUps.ip));
            m_strengthGridOop->setMarkers(singleMarkers(view.oop, lineUps.oop));
        } else {
            // Combined: each starter midway between his two places, his
            // weighted value; free places stay at their own slot.
            const QHash<QString, QPointF> ipAnchors =
                slotAnchors(version, layoutOf(lineUps.ip), true);
            const QHash<QString, QPointF> oopAnchors =
                slotAnchors(version, layoutOf(lineUps.oop), true);
            QList<StrengthGridWidget::Marker> markers;
            for (const QString &ipSlot : lineUps.ipOrder) {
                const XiCell cell = view.ip.value(ipSlot);
                if (!cell.isFilled()) {
                    StrengthGridWidget::Marker marker;
                    marker.anchor = ipAnchors.value(ipSlot);
                    marker.header = tr("mit: %1").arg(abbr(lineUps.ip.value(ipSlot)));
                    marker.impact = tr("frei");
                    marker.tooltip = tr("%1 mit Ball ist frei.").arg(ipSlot);
                    markers.append(marker);
                    continue;
                }
                const QString oopSlot = view.slotsOfUid.value(cell.playerUid).second;
                StrengthGridWidget::Marker marker;
                marker.uid = cell.playerUid;
                marker.goalkeeper = ipSlot == QLatin1String("GK");
                marker.anchor = combinedAnchor(ipAnchors.value(ipSlot), oopAnchors.value(oopSlot));
                marker.header = QStringLiteral("%1 · %2").arg(abbr(lineUps.ip.value(ipSlot)),
                                                             abbr(lineUps.oop.value(oopSlot)));
                marker.value = view.valueOfUid.value(cell.playerUid);
                marker.impact = view.impact.value(cell.playerUid);
                marker.tooltip = tr("%1 (mit Ball %2, ohne Ball %3)")
                                     .arg(cell.name, ipSlot, oopSlot)
                                 + QLatin1Char('\n') + view.tooltip.value(cell.playerUid);
                markers.append(marker);
            }
            for (const QString &oopSlot : lineUps.oopOrder) {
                if (view.oop.value(oopSlot).isFilled())
                    continue;
                StrengthGridWidget::Marker marker;
                marker.anchor = oopAnchors.value(oopSlot);
                marker.header = tr("ohne: %1").arg(abbr(lineUps.oop.value(oopSlot)));
                marker.impact = tr("frei");
                marker.tooltip = tr("%1 ohne Ball ist frei.").arg(oopSlot);
                markers.append(marker);
            }
            m_strengthGrid->setMarkers(markers);
        }
    }

    QString what;
    if (!view.twoPhase || m_phaseView == 1 || m_phaseView == 2 || both)
        what = tr("Groß: DWRS des Starters in seiner Rolle.");
    else
        what = tr("Groß: Wert des Starters über beide Aufstellungen (%1 % mit Ball).")
                   .arg(view.ipShare);
    m_gridLegend->setFixedWidth(both ? 2 * width + 16 : width);
    m_gridLegend->setText(what + QLatin1Char(' ')
                          + tr("Klein: so ändert sich die Startelf, wenn er ausfällt. Details "
                               "zeigt der Tooltip."));
    // A wrapped label in this row does not ask for its height by itself.
    m_gridLegend->setFixedHeight(
        m_gridLegend->fontMetrics()
            .boundingRect(QRect(0, 0, m_gridLegend->width(), 10000), Qt::TextWordWrap,
                          m_gridLegend->text())
            .height()
        + 4);
    m_gridLegend->setToolTip(
        tr("„Elf −4,0“: Fällt der Spieler aus, kommt der beste Ersatz (wie in Best XI unter "
           "„Ausfall in der Startelf“), und die Summe der DWRS der ganzen Startelf sinkt um "
           "4,0. „Lücke“: Kein Spieler im Kader füllt den Platz. Grundlage ist der ganze Verein "
           "ohne Langzeitverletzte und ohne Spieler, die als nicht registriert markiert sind, "
           "sonst ohne Wettbewerbsregeln; von Hand gesetzte Plätze aus Best XI gelten."));
    updateColorLegend();
}

void DashboardPage::updateKpis(const std::vector<const Player *> &coreSquad)
{
    double totalValue = 0.0;
    int ageSum = 0, ageCount = 0;
    for (const Player *player : coreSquad) {
        // "Not for Sale" (sentinel) counts as 0 for the sums (legacy).
        if (player->transferValue < kUnbuyableValue)
            totalValue += player->transferValue;
        if (player->age > 0) {
            ageSum += player->age;
            ++ageCount;
        }
    }
    const double avgValue = coreSquad.empty() ? 0.0 : totalValue / coreSquad.size();
    const double avgAge = ageCount > 0 ? static_cast<double>(ageSum) / ageCount : 0.0;

    m_kpiPlayers->setText(QString::number(coreSquad.size()));
    m_kpiTotalValue->setText(formatMillions(totalValue));
    m_kpiAvgValue->setText(formatMillions(avgValue));
    m_kpiAvgAge->setText(QLocale().toString(avgAge, 'f', 1));
    {
        // Where the data stands in the game: the newest save game read in.
        const QDate gameDate = m_context.database().lastGameDate();
        m_kpiGameDateTile->setVisible(gameDate.isValid());
        if (gameDate.isValid()) {
            const ImportHistory::SeasonStart start =
                ImportHistory::seasonStartOf(m_context.database());
            m_kpiGameDate->setText(QLocale().toString(gameDate, QLocale::ShortFormat));
            m_kpiGameDateCaption->setText(
                tr("Ingame-Datum · Saison %1")
                    .arg(ImportHistory::seasonLabel(ImportHistory::seasonOf(gameDate, start),
                                                    start)));
            m_kpiGameDateTile->setToolTip(tr("Stand des zuletzt eingelesenen FM27-Spielstands"));
        }
    }
}

void DashboardPage::updateClubTable(const std::vector<const Player *> &clubPlayers)
{
    m_clubTableTitle->setText(tr("Spieler bei %1 (%2)")
                                  .arg(m_context.userClub())
                                  .arg(clubPlayers.size()));

    m_clubTable->setSortingEnabled(false);
    m_clubTable->setRowCount(static_cast<int>(clubPlayers.size()));
    int row = 0;
    for (const Player *player : clubPlayers) {
        auto *nameItem = new NameItem(player->name);
        nameItem->setData(Qt::UserRole, player->uid);
        m_clubTable->setItem(row, 0, nameItem);
        m_clubTable->setItem(row, 1,
                             new NumericItem(QString::number(player->age), player->age));
        m_clubTable->setItem(row, 2, new QTableWidgetItem(player->positionRaw));

        auto *personality = new QTableWidgetItem(player->personality);
        const CellStyle style = personalityCellStyle(
            m_context.definitions().personalityCategory(player->personality));
        if (style.isValid()) {
            personality->setBackground(style.background);
            personality->setForeground(style.text);
        }
        m_clubTable->setItem(row, 3, personality);

        m_clubTable->setItem(row, 4, new QTableWidgetItem(player->agreedPlayingTime));
        m_clubTable->setItem(
            row, 5,
            new NumericItem(player->averageRating > 0.0
                                ? QLocale().toString(player->averageRating, 'f', 2)
                                : QStringLiteral("–"),
                            player->averageRating));
        m_clubTable->setItem(row, 6,
                             new NumericItem(player->transferValueRaw, player->transferValue));
        ++row;
    }
    m_clubTable->setSortingEnabled(true);
    m_clubTable->resizeColumnsToContents();
}

void DashboardPage::clearSuggestionCards()
{
    while (QLayoutItem *item = m_cardsLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void DashboardPage::rebuildSuggestions()
{
    clearSuggestionCards();

    const QString userClub = m_context.userClub();
    const QString secondClub = m_context.secondTeamClub();
    const QString tactic = selectedTactic();
    if (userClub.isEmpty() || tactic.isEmpty())
        return;

    // Every role of the tactic, for FM27 both line-ups.
    const QStringList roles = m_context.definitions().tacticRoleList(tactic);

    const RoleRatings &ratings = m_context.ratings();
    const QHash<QString, QString> roleNames = m_context.definitions().roleDisplayMap();
    const int maxAge = m_ageSlider->value();
    const double maxValue = maxValueFilter();
    const PlayerStatus::FreshnessContext freshness = m_context.freshnessContext();

    struct Suggestion {
        QString role;
        const Player *player;
        double rating;
        double myBest;
        // Own players rated for the role, best first (the card compares
        // the target with them).
        std::vector<std::pair<const Player *, double>> mine;
    };
    std::vector<Suggestion> suggestions;

    const PlayerStore &store = m_context.store();
    for (const QString &role : roles) {
        const auto roleRatings = ratings.constFind(role);
        if (roleRatings == ratings.constEnd())
            continue;
        // Iterate only the players actually rated for this role (a few hundred),
        // not the whole 35k store twice.
        const QHash<QString, double> &rated = roleRatings.value();

        // Best rating for the role inside my own club.
        double myBest = 0.0;
        std::vector<std::pair<const Player *, double>> mine;
        for (auto it = rated.constBegin(); it != rated.constEnd(); ++it) {
            const Player *player = store.findByUid(it.key());
            if (player && player->club == userClub) {
                myBest = std::max(myBest, it.value());
                mine.emplace_back(player, it.value());
            }
        }
        std::sort(mine.begin(), mine.end(),
                  [](const auto &a, const auto &b) { return a.second > b.second; });
        if (mine.size() > 3)
            mine.resize(3);

        const Player *bestUpgrade = nullptr;
        double bestUpgradeRating = 0.0;
        for (auto it = rated.constBegin(); it != rated.constEnd(); ++it) {
            const double rating = it.value();
            if (rating <= myBest || rating <= bestUpgradeRating)
                continue; // cheap numeric gate before the store lookup
            const Player *player = store.findByUid(it.key());
            if (!player)
                continue;
            if (player->club == userClub || (!secondClub.isEmpty() && player->club == secondClub))
                continue;
            if (player->age <= 0 || player->age > maxAge)
                continue;
            if (player->transferValue > maxValue)
                continue;
            if (PlayerStatus::isInactive(*player, freshness))
                continue; // left the game — never a transfer target
            bestUpgrade = player;
            bestUpgradeRating = rating;
        }
        if (bestUpgrade)
            suggestions.push_back({role, bestUpgrade, bestUpgradeRating, myBest, mine});
    }

    if (suggestions.empty()) {
        m_suggestionStatus->setText(
            tr("Dein Kader ist gut aufgestellt. Keine klaren Verstärkungen "
               "innerhalb der Filterkriterien gefunden."));
        return;
    }
    m_suggestionStatus->clear();

    const int columns = std::min<int>(4, static_cast<int>(suggestions.size()));
    int index = 0;
    for (const Suggestion &suggestion : suggestions) {
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("suggestionCard"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 10, 14, 10);
        cardLayout->setSpacing(4);

        const Player *player = suggestion.player;
        const QString roleName = roleNames.value(suggestion.role, suggestion.role);
        auto *header = new QLabel(player->name, card);
        header->setObjectName(QStringLiteral("cardTitle"));
        header->setWordWrap(true);
        cardLayout->addWidget(header);
        auto *role = new QLabel(tr("Upgrade für %1").arg(roleName), card);
        role->setObjectName(QStringLiteral("kpiCaption"));
        role->setWordWrap(true);
        cardLayout->addWidget(role);
        cardLayout->addSpacing(4);

        // Figures as a small aligned table: label left, value right.
        auto *figures = new QGridLayout;
        figures->setHorizontalSpacing(12);
        figures->setVerticalSpacing(2);
        const QList<QPair<QString, QString>> rows = {
            {tr("Bewertung"), tr("%1 % (dein Bester %2 %)")
                                  .arg(formatDwrs(suggestion.rating))
                                  .arg(formatDwrs(suggestion.myBest))},
            {tr("Wert"), player->transferValueRaw},
            {tr("Alter"), QString::number(player->age)},
        };
        for (int row = 0; row < rows.size(); ++row) {
            auto *label = new QLabel(rows[row].first, card);
            label->setObjectName(QStringLiteral("kpiCaption"));
            auto *value = new QLabel(rows[row].second, card);
            figures->addWidget(label, row, 0);
            figures->addWidget(value, row, 1);
        }
        figures->setColumnStretch(1, 1);
        cardLayout->addLayout(figures);

        auto *footer = new QLabel(tr("Verein: %1<br/>Positionen: %2")
                                      .arg(player->club.toHtmlEscaped(),
                                           player->positionRaw.toHtmlEscaped()),
                                  card);
        footer->setObjectName(QStringLiteral("kpiCaption"));
        footer->setWordWrap(true);
        cardLayout->addWidget(footer);

        // Left or right click: the player menu, with comparisons against the
        // own players this target would replace.
        card->setCursor(Qt::PointingHandCursor);
        card->setToolTip(tr("Klicken für Aktionen: Profil, Shortlist, Vergleich"));
        const QString targetUid = player->uid;
        const QString targetRole = suggestion.role;
        std::vector<std::pair<QString, QString>> ownEntries; // uid, label
        for (const auto &[own, ownRating] : suggestion.mine)
            ownEntries.emplace_back(own->uid,
                                    tr("%1 (%2 %)").arg(own->name).arg(formatDwrs(ownRating)));
        const auto showMenu = [this, targetUid, targetRole, ownEntries](const QPoint &pos) {
            // The window, not the card, owns the menu: a shortlist toggle
            // rebuilds the cards while the menu is still open.
            PlayerActions::showContextMenu(
                m_context, window(), targetUid, pos,
                [this, targetUid, targetRole, ownEntries](QMenu &menu) {
                    const auto compare = [this, targetUid, targetRole](const QStringList &own) {
                        m_context.addPendingComparisonUid(targetUid);
                        for (const QString &uid : own)
                            m_context.addPendingComparisonUid(uid);
                        m_context.setPendingComparisonRole(targetRole);
                        m_context.requestNavigation(QStringLiteral("player_comparison"));
                    };
                    for (const auto &[uid, label] : ownEntries) {
                        menu.addAction(tr("Vergleichen mit %1").arg(label),
                                       [compare, uid] { compare({uid}); });
                    }
                    if (ownEntries.size() > 1) {
                        QStringList all;
                        for (const auto &entry : ownEntries)
                            all << entry.first;
                        menu.addAction(tr("Vergleichen mit allen %n", nullptr,
                                          static_cast<int>(all.size())),
                                       [compare, all] { compare(all); });
                    }
                });
        };
        installClickMenu(card, showMenu);

        m_cardsLayout->addWidget(card, index / columns, index % columns);
        ++index;
    }
    for (int c = 0; c < columns; ++c)
        m_cardsLayout->setColumnStretch(c, 1);
}

void DashboardPage::chooseImportFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("FM-Export oder FM27-Spielstand auswählen"), m_context.htmlExportDir(),
        tr("FM-Exporte und FM27-Spielstände (*.html *.htm *.csv *.fm);;Alle Dateien (*)"));
    if (path.isEmpty())
        return;
    m_filePathEdit->setText(path);
    m_importButton->setEnabled(true);
}

void DashboardPage::prepareImport(const QString &filePath)
{
    if (m_importRunning)
        return;
    m_filePathEdit->setText(filePath);
    m_importButton->setEnabled(true);
    m_importButton->setFocus();
}

void DashboardPage::startImport()
{
    if (m_importRunning)
        return;
    const QString filePath = m_filePathEdit->text();
    if (filePath.isEmpty())
        return;

    m_pendingSquadUpdate = m_squadUpdateCheck->isChecked();
    m_importRunning = true;

    runImportPipeline(m_context, this, filePath, m_autoAssignCheck->isChecked(),
                      [this](const ImportPipelineResult &result) {
                          m_importRunning = false;
                          importFinished(result);
                      });
}

void DashboardPage::importFinished(const ImportPipelineResult &result)
{
    if (!result.import.success) {
        QMessageBox::critical(
            this, tr("Import fehlgeschlagen"),
            tr("Die Datei konnte nicht importiert werden.\n\n%1\n\n"
               "Mögliche Ursachen: keine 'UID'-Spalte oder kein gültiger "
               "Football-Manager-Export (HTML oder CSV).")
                .arg(importErrorText(result.import)));
        return;
    }

    const QStringList summary = importSummaryLines(result);
    const QStringList warnings = importWarningLines(result);

    QMessageBox box(this);
    box.setIcon(warnings.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
    box.setWindowTitle(tr("Import abgeschlossen"));
    box.setText(summary.join(QLatin1Char('\n')));
    if (!warnings.isEmpty())
        box.setDetailedText(warnings.join(QStringLiteral("\n\n")));
    box.exec();

    // --- Departure detection for full squad updates. ---
    if (m_pendingSquadUpdate && !m_context.userClub().isEmpty()) {
        const QSet<QString> affected(result.import.affectedUids.cbegin(),
                                     result.import.affectedUids.cend());
        resolveDepartures(affected);
    }
    m_pendingSquadUpdate = false;
}

void DashboardPage::resolveDepartures(const QSet<QString> &affectedUids)
{
    const QString userClub = m_context.userClub();
    const QString secondClub = m_context.secondTeamClub();

    std::vector<const Player *> missing;
    for (const Player &player : m_context.store().players()) {
        const bool atMyClub = player.club == userClub
                              || (!secondClub.isEmpty() && player.club == secondClub);
        if (atMyClub && !affectedUids.contains(player.uid))
            missing.push_back(&player);
    }
    if (missing.empty())
        return;

    DepartureDialog dialog(missing, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    std::vector<Player> updates;
    for (const DepartureChoice &choice : dialog.choices()) {
        if (!choice.left)
            continue; // stayed at the club -> no change
        const Player *player = m_context.store().findByUid(choice.uid);
        if (!player)
            continue;
        // Empty destination falls back to the free-agent default.
        const QString destination = choice.destination.isEmpty() ? freeAgentTag()
                                                                  : choice.destination;
        if (destination == player->club)
            continue;
        Player updated = *player;
        PlayerStatus::applyDeparture(updated, destination);
        updates.push_back(std::move(updated));
    }
    if (updates.empty())
        return;

    if (!m_context.database().upsertPlayers(updates)) {
        QMessageBox::critical(this, tr("Abgänge"), m_context.database().errorString());
        return;
    }
    QList<int> ids;
    for (const Player &player : updates)
        ids << player.id;
    m_context.refreshPlayers(ids);
    QMessageBox::information(this, tr("Abgänge"),
                             tr("%1 Spieler-Datensätze aktualisiert.").arg(updates.size()));
}

} // namespace fm
