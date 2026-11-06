#include "ThemeManager.h"

#include "core/AppConfig.h"
#include "core/Utils.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>

#include <functional>

namespace fm {

namespace {

// QSS cannot draw combo arrows / check marks without image assets, so the
// theme renders small PNGs matching the current color into the temp dir.
QString renderAsset(const QString &name, const QColor &color,
                    const std::function<void(QPainter &)> &draw)
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/fmplayeranalyzer");
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/%2_%3.png").arg(dir, name, color.name().mid(1));

    if (!QFile::exists(path)) {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        draw(painter);
        painter.end();
        pixmap.save(path, "PNG");
    }
    return QString(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString chevronDown(const QColor &color)
{
    return renderAsset(QStringLiteral("chevron"), color, [](QPainter &p) {
        p.drawPolyline(QPolygonF{{9.0, 13.0}, {16.0, 20.0}, {23.0, 13.0}});
    });
}

QString checkMark(const QColor &color)
{
    return renderAsset(QStringLiteral("check"), color, [](QPainter &p) {
        p.drawPolyline(QPolygonF{{8.0, 16.5}, {13.5, 22.0}, {24.0, 10.0}});
    });
}

// Readable black/white text on a filled color.
QColor contrastText(const QColor &background)
{
    return readableTextOn(background);
}

QString rgba(const QColor &c, double alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(alpha);
}

} // namespace

const QStringList &ThemeManager::roleNames()
{
    static const QStringList roles = {
        QStringLiteral("background"), QStringLiteral("surface"),
        QStringLiteral("sidebar"),    QStringLiteral("header"),
        QStringLiteral("primary"),    QStringLiteral("interactive"),
        QStringLiteral("text"),
    };
    return roles;
}

QString ThemeManager::settingsKey(const QString &mode, const QString &role)
{
    return mode + QLatin1Char('_') + role + QStringLiteral("_color");
}

ThemeManager::ThemeManager(AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    m_mode = m_config.themeSettings().value(QStringLiteral("current_mode"),
                                            QStringLiteral("night"));
}

void ThemeManager::setMode(const QString &mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    // Only the mode changes: one row in the save's profile.
    m_config.saveThemeSettings({{QStringLiteral("current_mode"), mode}});
    apply();
}

void ThemeManager::reload()
{
    m_mode = m_config.themeSettings().value(QStringLiteral("current_mode"),
                                            QStringLiteral("night"));
    apply();
}

void ThemeManager::toggle()
{
    setMode(m_mode == QLatin1String("night") ? QStringLiteral("day") : QStringLiteral("night"));
}

QColor ThemeManager::roleColor(const QString &role) const
{
    // themeDefaults() supplies a value for every role/mode, so this normally
    // resolves without the built-in fallback.
    const auto settings = m_config.themeSettings();
    const QString value = settings.value(settingsKey(m_mode, role));
    if (!value.isEmpty() && QColor::isValidColorName(value))
        return QColor(value);

    const bool night = m_mode != QLatin1String("day");
    static const QHash<QString, QColor> nightFallback = {
        {QStringLiteral("background"), QColor(QStringLiteral("#262626"))},
        {QStringLiteral("surface"), QColor(QStringLiteral("#343434"))},
        {QStringLiteral("sidebar"), QColor(QStringLiteral("#1f1f1f"))},
        {QStringLiteral("header"), QColor(QStringLiteral("#2d2d2d"))},
        {QStringLiteral("primary"), QColor(QStringLiteral("#3d7dff"))},
        {QStringLiteral("interactive"), QColor(QStringLiteral("#3d7dff"))},
        {QStringLiteral("text"), QColor(QStringLiteral("#F5F5F5"))},
    };
    static const QHash<QString, QColor> dayFallback = {
        {QStringLiteral("background"), QColor(QStringLiteral("#F0F2F6"))},
        {QStringLiteral("surface"), QColor(QStringLiteral("#FFFFFF"))},
        {QStringLiteral("sidebar"), QColor(QStringLiteral("#E7E9ED"))},
        {QStringLiteral("header"), QColor(QStringLiteral("#FFFFFF"))},
        {QStringLiteral("primary"), QColor(QStringLiteral("#0069b3"))},
        {QStringLiteral("interactive"), QColor(QStringLiteral("#0069b3"))},
        {QStringLiteral("text"), QColor(QStringLiteral("#31333F"))},
    };
    return (night ? nightFallback : dayFallback).value(role, QColor(QStringLiteral("#808080")));
}

void ThemeManager::apply()
{
    m_background = roleColor(QStringLiteral("background"));
    m_surface = roleColor(QStringLiteral("surface"));
    m_sidebar = roleColor(QStringLiteral("sidebar"));
    m_header = roleColor(QStringLiteral("header"));
    m_primary = roleColor(QStringLiteral("primary"));
    m_interactive = roleColor(QStringLiteral("interactive"));
    m_text = roleColor(QStringLiteral("text"));

    // A club's accent can be navy on the dark base or neon yellow on the light
    // one: as text it is lifted to 4.5:1 against both page and cards, as an
    // outline to 3:1. Fills (buttons, check boxes) keep the real club color.
    m_accentText = withContrast(withContrast(m_interactive, m_background, 4.5), m_surface, 4.5);
    m_accentUi = withContrast(m_interactive, m_background, 3.0);

    const auto settings = m_config.themeSettings();
    m_headerStyle = settings.value(QStringLiteral("header_style"), QStringLiteral("gradient"));
    m_crestWatermark =
        settings.value(QStringLiteral("header_crest_watermark"), QStringLiteral("true"))
        != QLatin1String("false");
    m_headerPalette = clubHeaderPalette(m_primary, m_interactive, m_sidebar);

    qApp->setStyleSheet(buildStyleSheet());
    emit themeChanged();
}

QString ThemeManager::buildStyleSheet() const
{
    const bool night = m_mode == QLatin1String("night");

    const QString bg = m_background.name();
    const QString surface = m_surface.name();
    const QString sidebar = m_sidebar.name();
    // Club header in "gradient" style: the bar is neutral chrome like the
    // sidebar; the club colors live in the block ClubHeaderBar paints.
    const bool gradientHeader = m_headerStyle != QLatin1String("solid");
    const QString header = gradientHeader ? m_sidebar.name() : m_header.name();
    const QString primary = m_primary.name();
    // UI fills (check boxes, sliders, progress) use the accent lifted to 3:1.
    const QString interactive = m_accentUi.name();
    const QString fg = m_text.name();

    const QString primaryText = contrastText(m_primary).name();
    const QString interactiveText = contrastText(m_accentUi).name();
    const QString headerText =
        gradientHeader ? m_text.name() : contrastText(m_header).name();
    const QString primaryHover = m_primary.darker(115).name();
    const QString interactiveHover = m_accentUi.darker(115).name();

    // Derived neutrals (work on any background via alpha).
    const QString muted = rgba(m_text, 0.60);
    const QString border = rgba(m_text, night ? 0.14 : 0.18);
    const QString hover = rgba(m_text, night ? 0.08 : 0.06);
    const QString rowAlt = rgba(m_text, night ? 0.04 : 0.05);
    const QString interactiveSubtle = rgba(m_interactive, night ? 0.28 : 0.18);
    const QString sidebarSelText = contrastText(m_primary).name();

    const QString chevron = chevronDown(m_text);
    const QString check = checkMark(QColor(interactiveText));
    const QString sidebarHover = rgba(m_text, night ? 0.07 : 0.06);

    QString qss = QStringLiteral(R"(
/* ===== Base ===== */
QMainWindow, QDialog, QWizard { background-color: %BG%; color: %FG%; }
QWidget { color: %FG%; font-size: 10pt; }
QLabel { background: transparent; }
QLabel#kpiCaption { color: %MUTED%; }
QLabel#sectionTitle { font-size: 12pt; font-weight: 600; }
QLabel#pageTitle { font-size: 17pt; font-weight: 600; padding: 4px 0 4px 0; }
QLabel#cardTitle { font-size: 10.5pt; font-weight: 600; }
QStackedWidget, QScrollArea, QScrollArea > QWidget > QWidget { background-color: %BG%; }
QSplitter::handle { background: transparent; }

/* ===== Sidebar ===== */
QFrame#sidebar { background-color: %SIDEBAR%; border-right: 1px solid %BORDER%; }
QFrame#sidebar QListWidget { background: transparent; border: none; outline: none; }
QFrame#sidebar QListWidget::item {
    padding: 8px 10px; margin: 1px 6px; border-radius: 8px; color: %FG%; }
QFrame#sidebar QListWidget::item:hover:!selected { background-color: %SIDEBARHOVER%; }
QFrame#sidebar QListWidget::item:selected {
    background-color: %INTERACTIVESUBTLE%; color: %FG%; font-weight: 600;
    border-left: 3px solid %PRIMARY%; padding-left: 7px; }
QFrame#sidebar QListWidget::item:disabled {
    color: %MUTED%; padding: 12px 12px 4px 10px; background: transparent; }
QFrame#sidebar QToolButton#saveSwitcher {
    text-align: left; padding: 7px 26px 7px 8px; border-radius: 10px; font-weight: 600; }
QFrame#sidebar QToolButton#saveSwitcher::menu-indicator { right: 10px; }
QFrame#sidebar QToolButton#creditButton {
    background: transparent; border: none; color: %MUTED%; font-size: 8.5pt; padding: 2px 6px; }
QFrame#sidebar QToolButton#creditButton:hover { color: %FG%; }

/* ===== Header bar ===== */
QFrame#headerBar { background-color: %HEADER%; border-bottom: 1px solid %BORDER%; }
QFrame#headerBar QLabel { color: %HEADERTEXT%; }
QFrame#headerBar QToolButton#navButton {
    font-size: 13pt; font-weight: 600; padding: 1px 10px; min-width: 16px; }
QFrame#headerBar QToolButton#navButton:pressed { background-color: %INTERACTIVESUBTLE%; }
QFrame#headerBar QToolButton#navButton:disabled {
    color: %MUTED%; background: transparent; border-color: %BORDER%; }

/* ===== Cards ===== */
QFrame#kpiTile, QFrame#suggestionCard {
    background-color: %SURFACE%; border: 1px solid %BORDER%; border-radius: 10px; }
QFrame#suggestionCard:hover { border-color: %ACCENTUI%; }
QLabel#kpiValue { font-size: 19pt; font-weight: 650; color: %FG%; }
/* Player profile header: one quiet surface, name and figures by size. */
QFrame#profileHeader {
    background-color: %SURFACE%; border: 1px solid %BORDER%; border-radius: 12px; }
QLabel#profileName { font-size: 17pt; font-weight: 600; }
QLabel#profileFact { font-size: 13pt; font-weight: 500; }
/* Profile: roles of the favorite tactic, with ball / without ball. Fixed
   hues, independent of the club colors, so the meaning never changes. */
QFrame#kpiTile[tacticPhase="ip"] { border: 2px solid %IPMARK%; }
QFrame#kpiTile[tacticPhase="oop"] { border: 2px solid %OOPMARK%; }
QFrame#ipSwatch, QFrame#oopSwatch { border-radius: 3px; }
QFrame#ipSwatch { background-color: %IPMARK%; }
QFrame#oopSwatch { background-color: %OOPMARK%; }

/* ===== Buttons ===== */
QPushButton {
    background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 7px 16px; min-height: 20px; }
QPushButton:hover { background-color: %HOVER%; border-color: %ACCENTUI%; }
QPushButton:pressed { background-color: %INTERACTIVESUBTLE%; }
QPushButton:disabled { color: %MUTED%; border-color: %BORDER%; background: transparent; }
QPushButton:default { background-color: %PRIMARY%; color: %PRIMARYTEXT%; border-color: %PRIMARYEDGE%; font-weight: 600; }
QPushButton:default:hover { background-color: %PRIMARYHOVER%; border-color: %PRIMARYHOVER%; }
QPushButton:checked { background-color: %INTERACTIVESUBTLE%; border-color: %ACCENTUI%; }
QToolButton {
    background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 6px 12px; }
QToolButton:hover { background-color: %HOVER%; border-color: %ACCENTUI%; }
QToolButton::menu-indicator { image: url(%CHEVRON%); width: 12px; height: 12px;
    subcontrol-position: right center; subcontrol-origin: padding; right: 6px; }

/* ===== Inputs ===== */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QTextEdit, QPlainTextEdit {
    background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 6px 10px; min-height: 18px;
    selection-background-color: %INTERACTIVE%; selection-color: %INTERACTIVETEXT%; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QDateEdit:focus,
QTextEdit:focus, QPlainTextEdit:focus { border: 2px solid %ACCENTUI%; padding: 5px 9px; }
QLineEdit:disabled, QComboBox:disabled { color: %MUTED%; }
QComboBox::drop-down { border: none; width: 28px; }
QComboBox::down-arrow { image: url(%CHEVRON%); width: 14px; height: 14px; }
QComboBox QAbstractItemView {
    background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%;
    border-radius: 8px; padding: 4px;
    selection-background-color: %INTERACTIVESUBTLE%; selection-color: %FG%; }
QComboBox QAbstractItemView::item { padding: 6px 10px; border-radius: 6px; min-height: 22px; }
QSpinBox::up-button, QDoubleSpinBox::up-button, QDateEdit::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button, QDateEdit::down-button {
    background: transparent; border: none; width: 20px; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QDateEdit::up-arrow { image: url(%CHEVRON%); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow, QDateEdit::down-arrow { image: url(%CHEVRON%); width: 10px; height: 10px; }

/* ===== Check boxes / radios ===== */
QCheckBox, QRadioButton { spacing: 8px; padding: 3px 0; }
QCheckBox::indicator, QRadioButton::indicator { width: 18px; height: 18px; }
QCheckBox::indicator {
    border: 2px solid %BORDER%; border-radius: 5px; background: %SURFACE%; }
QCheckBox::indicator:hover { border-color: %ACCENTUI%; }
QCheckBox::indicator:checked {
    background-color: %INTERACTIVE%; border-color: %INTERACTIVE%; image: url(%CHECK%); }
QRadioButton::indicator { border: 2px solid %BORDER%; border-radius: 10px; background: %SURFACE%; }
QRadioButton::indicator:hover { border-color: %ACCENTUI%; }
QRadioButton::indicator:checked { border: 6px solid %INTERACTIVE%; background: %INTERACTIVETEXT%; }

/* ===== Lists / tables / trees ===== */
QListWidget, QListView { background-color: %SURFACE%; border: 1px solid %BORDER%;
    border-radius: 8px; outline: none; }
QListWidget::item, QListView::item { padding: 6px 10px; border-radius: 6px; }
QListWidget::item:selected, QListView::item:selected {
    background-color: %INTERACTIVESUBTLE%; color: %FG%; }
QListWidget::item:hover:!selected, QListView::item:hover:!selected { background-color: %HOVER%; }
QTableView, QTreeView {
    background-color: %BG%; alternate-background-color: %ROWALT%; color: %FG%;
    border: 1px solid %BORDER%; border-radius: 8px;
    gridline-color: transparent;
    selection-background-color: %INTERACTIVESUBTLE%; selection-color: %FG%; }
QHeaderView { background: transparent; }
QHeaderView::section {
    background-color: %BG%; color: %MUTED%; font-weight: 600;
    border: none; border-bottom: 2px solid %BORDER%; padding: 7px 10px; }
QHeaderView::section:hover { color: %FG%; }
QTableCornerButton::section { background-color: %BG%; border: none; border-bottom: 2px solid %BORDER%; }

/* ===== Tabs ===== */
QTabWidget::pane { border: 1px solid %BORDER%; border-radius: 8px; background-color: %BG%; top: -1px; }
QTabBar::tab {
    background: transparent; color: %MUTED%; padding: 9px 18px; margin-right: 4px;
    border: none; border-bottom: 3px solid transparent; }
QTabBar::tab:hover { color: %FG%; }
QTabBar::tab:selected { color: %FG%; border-bottom: 3px solid %ACCENTUI%; font-weight: 600; }
/* Page tabs of the active sidebar group, between header bar and page. */
QFrame#pageTabStrip { background-color: %BG%; border-bottom: 1px solid %BORDER%; }
QFrame#pageTabStrip QTabBar::tab { padding: 10px 20px; }

/* ===== Group boxes as cards ===== */
/* Title inside the card as a quiet section heading (not on the border line),
   neutral text: the club color is reserved for selection and actions. */
QGroupBox {
    border: 1px solid %BORDER%; border-radius: 10px; margin-top: 0;
    padding: 34px 6px 6px 6px; background-color: %SURFACE%; }
QGroupBox::title {
    subcontrol-origin: padding; subcontrol-position: top left; left: 14px; top: 11px;
    padding: 0; color: %FG%; font-weight: 600; font-size: 10.5pt; }
QGroupBox::indicator { width: 18px; height: 18px; border: 2px solid %BORDER%;
    border-radius: 5px; background: %BG%; }
QGroupBox::indicator:hover { border-color: %ACCENTUI%; }
QGroupBox::indicator:checked { background-color: %INTERACTIVE%; border-color: %INTERACTIVE%; image: url(%CHECK%); }

/* ===== Sliders ===== */
QSlider { min-height: 24px; }
QSlider::groove:horizontal { height: 5px; border-radius: 2px; background: %BORDER%; }
QSlider::sub-page:horizontal { height: 5px; border-radius: 2px; background: %INTERACTIVE%; }
QSlider::handle:horizontal {
    width: 16px; height: 16px; margin: -6px 0; border-radius: 8px;
    background: %INTERACTIVE%; border: 2px solid %INTERACTIVETEXT%; }
QSlider::handle:horizontal:hover { background: %INTERACTIVEHOVER%; }

/* ===== Scrollbars ===== */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %BORDER%; border-radius: 4px; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: %MUTED%; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %BORDER%; border-radius: 4px; min-width: 32px; }
QScrollBar::handle:horizontal:hover { background: %MUTED%; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ===== Main window frame ===== */
QFrame#headerBar QLabel#breadcrumb { color: %MUTED%; font-weight: 600; padding-left: 4px; }
QFrame#headerBar QPushButton#paletteButton {
    text-align: left; color: %MUTED%; min-width: 240px; padding: 6px 12px; font-weight: 400; }
QFrame#headerBar QPushButton#paletteButton:hover { color: %FG%; }
QPushButton#headerPrimary {
    background-color: %PRIMARY%; color: %PRIMARYTEXT%; border-color: %PRIMARYEDGE%; font-weight: 600; }
QPushButton#headerPrimary:hover { background-color: %PRIMARYHOVER%; border-color: %PRIMARYHOVER%; }
QFrame#noticeBar {
    background-color: rgba(240, 176, 74, 0.14); border: 1px solid rgba(240, 176, 74, 0.45);
    border-radius: 10px; }
QLabel#toast {
    background-color: %SURFACE%; color: %FG%; border: 1px solid %ACCENTUI%;
    border-radius: 10px; padding: 10px 14px; }
QFrame#segmented { background-color: %HOVER%; border-radius: 9px; }
QFrame#segmented QPushButton#segmentButton {
    background: transparent; border: none; border-radius: 7px; padding: 5px 6px;
    color: %MUTED%; font-weight: 600; min-height: 18px; }
QFrame#segmented QPushButton#segmentButton:hover { color: %FG%; }
QFrame#segmented QPushButton#segmentButton:checked {
    background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%; }
QWidget#phaseSwitch { background-color: %HOVER%; border-radius: 7px; }
QWidget#phaseSwitch QToolButton {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
    padding: 3px 10px; color: %MUTED%; font-weight: 500; }
QWidget#phaseSwitch QToolButton:hover { color: %FG%; }
QWidget#phaseSwitch QToolButton#phaseSwitchLeft:checked {
    background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%;
    border-bottom: 2px solid %IPMARK%; }
QWidget#phaseSwitch QToolButton#phaseSwitchRight:checked {
    background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%;
    border-bottom: 2px solid %OOPMARK%; }
QFrame#sidebar QToolButton#sidebarIconButton {
    background: transparent; border: 1px solid transparent; border-radius: 7px; padding: 5px; }
QFrame#sidebar QToolButton#sidebarIconButton:hover { background-color: %HOVER%; border-color: %BORDER%; }
QDialog#commandPalette {
    background-color: %SURFACE%; border: 1px solid %ACCENTUI%; border-radius: 12px; }
QListWidget#paletteList { background: transparent; border: none; }
QListWidget#paletteList::item { padding: 7px 10px; border-radius: 7px; }
QListWidget#paletteList::item:disabled { color: %MUTED%; padding: 10px 10px 2px 10px; }

/* ===== Start screen / new save wizard ===== */
QFrame#startHero { background-color: %SIDEBAR%; border-right: 1px solid %BORDER%; }
QLabel#startTitle { font-size: 16pt; font-weight: 500; }
QPushButton#heroPrimary, QPushButton#heroAction { padding: 0; border-radius: 10px; min-height: 56px; }
QPushButton#heroAction { background-color: transparent; border: 1px solid %BORDER%; }
QPushButton#heroAction:hover { background-color: %HOVER%; }
QPushButton#heroPrimary { background-color: %PRIMARY%; border-color: %PRIMARY%; }
QPushButton#heroPrimary:hover { background-color: %PRIMARYHOVER%; border-color: %PRIMARYHOVER%; }
QLabel#heroActionTitle { font-size: 10.5pt; font-weight: 500; }
QLabel#heroActionText { font-size: 9pt; font-weight: 400; color: %MUTED%; }
QPushButton#heroPrimary QLabel#heroActionTitle { color: %PRIMARYTEXT%; }
QPushButton#heroPrimary QLabel#heroActionText { color: %PRIMARYTEXTMUTED%; }
QPushButton:default:disabled { background: transparent; color: %MUTED%; border-color: %BORDER%; }
QToolButton#flatButton {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
    color: %MUTED%; padding: 4px 8px; }
QToolButton#flatButton:hover { color: %FG%; background-color: %HOVER%; }
QToolButton#flatButton::menu-indicator { image: none; width: 0; }
QListWidget#saveList { background: transparent; border: none; }
QListWidget#saveList::item {
    border: 1px solid %BORDER%; border-radius: 10px; background-color: %SURFACE%;
    padding: 0; margin: 0; }
QListWidget#saveList::item:hover:!selected { background-color: %HOVER%; }
QListWidget#saveList::item:selected {
    background-color: %INTERACTIVESUBTLE%; border: 1px solid %ACCENTUI%; }
QFrame#saveCard { background: transparent; }
QLabel#saveName { font-size: 11pt; font-weight: 500; }
QLabel#chip, QLabel#chipAccent {
    border-radius: 4px; padding: 0 6px; font-size: 8.5pt; font-weight: 500; }
QLabel#chip { background-color: %HOVER%; color: %MUTED%; }
QLabel#chipAccent { background-color: %INTERACTIVESUBTLE%; color: %FG%; }
QToolButton#cardMenuButton {
    background: transparent; border: none; border-radius: 6px; font-size: 14pt;
    color: %MUTED%; padding: 0 8px; }
QToolButton#cardMenuButton:hover { color: %FG%; background-color: %HOVER%; }
QFrame#emptyState { background-color: %SURFACE%; border: 1px solid %BORDER%; border-radius: 12px; }
QLabel#emptyTitle { font-size: 12pt; font-weight: 500; }
QLabel#emptyText { color: %MUTED%; }
QLabel#emptyStepNumber {
    color: %MUTED%; border: 1px solid %BORDER%; border-radius: 12px;
    font-size: 9pt; font-weight: 500; }
QLabel#emptyStepText { color: %FG%; }
QFrame#startFooter { border-top: 1px solid %BORDER%; }
QLabel#stepLabel { color: %MUTED%; font-weight: 600; }
QLabel#stepLabel[state="current"], QLabel#stepLabel[state="done"] { color: %FG%; }
QLabel#stepNumber {
    color: %MUTED%; font-weight: 700; font-size: 9pt; border: 2px solid %BORDER%;
    border-radius: 12px; min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; }
QLabel#stepNumber[state="current"] {
    background-color: %PRIMARY%; border-color: %PRIMARY%; color: %PRIMARYTEXT%; }
QLabel#stepNumber[state="done"] { border-color: %PRIMARY%; color: %FG%; }
QFrame#stepLine { background-color: %BORDER%; border: none; }
QLabel#fieldLabel { font-weight: 600; }
QLabel#errorText { color: #e5484d; }
QLabel#successNote {
    background-color: %INTERACTIVESUBTLE%; border-radius: 8px; padding: 10px 12px; }
QPushButton#choiceCard { padding: 0; border-radius: 10px; min-height: 84px; }
QPushButton#choiceCard:checked { border: 2px solid %ACCENTUI%; background-color: %INTERACTIVESUBTLE%; }
QLabel#choiceTitle { font-size: 14pt; font-weight: 700; }
QToolButton#foldButton { background: transparent; border: none; color: %MUTED%; padding: 2px 0; }
QToolButton#foldButton:hover { color: %FG%; }
QFrame#dropZone { border: 2px dashed %ACCENTUI%; border-radius: 12px; background-color: %HOVER%; }
QFrame#dropZone[hovered="true"] { background-color: %INTERACTIVESUBTLE%; }
QLabel#dropZoneIcon { font-size: 22pt; color: %ACCENTTEXT%; }
QLabel#dropZoneTitle { font-size: 11pt; font-weight: 600; }

/* ===== Menus / tooltips / statusbar / progress ===== */
QMenuBar { background-color: %SIDEBAR%; color: %FG%; border-bottom: 1px solid %BORDER%; padding: 2px 6px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 6px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background-color: %HOVER%; }
QMenu { background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%;
    border-radius: 10px; padding: 6px; }
QMenu::item { padding: 8px 28px 8px 14px; border-radius: 6px; }
QMenu::item:selected { background-color: %INTERACTIVESUBTLE%; }
QMenu::item:disabled { color: %MUTED%; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 6px 10px; }
QMenu::icon { padding-left: 8px; }
QToolTip { background-color: %SURFACE%; color: %FG%; border: 1px solid %BORDER%;
    padding: 6px 10px; }
QStatusBar { background-color: %SURFACE%; color: %MUTED%; border-top: 1px solid %BORDER%; }
QProgressBar { background-color: %SURFACE%; border: 1px solid %BORDER%;
    border-radius: 8px; text-align: center; color: %FG%; min-height: 18px; }
QProgressBar::chunk { background-color: %INTERACTIVE%; border-radius: 7px; }
QProgressDialog { background-color: %BG%; }
)");

    qss.replace(QLatin1String("%BG%"), bg);
    qss.replace(QLatin1String("%SURFACE%"), surface);
    qss.replace(QLatin1String("%SIDEBAR%"), sidebar);
    qss.replace(QLatin1String("%SIDEBARHOVER%"), sidebarHover);
    qss.replace(QLatin1String("%SIDEBARSELTEXT%"), sidebarSelText);
    qss.replace(QLatin1String("%HEADER%"), header);
    qss.replace(QLatin1String("%HEADERTEXT%"), headerText);
    qss.replace(QLatin1String("%FG%"), fg);
    qss.replace(QLatin1String("%PRIMARY%"), primary);
    qss.replace(QLatin1String("%PRIMARYTEXT%"), primaryText);
    qss.replace(QLatin1String("%PRIMARYHOVER%"), primaryHover);
    // Edge of a filled club-color button: visible against the page (3:1) even
    // for a pale or neon club color.
    // Phase marks (>= 3:1 against the cards they outline).
    qss.replace(QLatin1String("%IPMARK%"),
                withContrast(QColor(night ? QStringLiteral("#5b9dff") : QStringLiteral("#1d63d8")),
                             m_surface, 3.0).name());
    qss.replace(QLatin1String("%OOPMARK%"),
                withContrast(QColor(night ? QStringLiteral("#f0a03c") : QStringLiteral("#b8650a")),
                             m_surface, 3.0).name());
    qss.replace(QLatin1String("%PRIMARYEDGE%"), withContrast(m_primary, m_background, 3.0).name());
    qss.replace(QLatin1String("%PRIMARYTEXTMUTED%"), rgba(contrastText(m_primary), 0.78));
    qss.replace(QLatin1String("%INTERACTIVE%"), interactive);
    qss.replace(QLatin1String("%ACCENTTEXT%"), m_accentText.name());
    qss.replace(QLatin1String("%ACCENTUI%"), m_accentUi.name());
    qss.replace(QLatin1String("%INTERACTIVETEXT%"), interactiveText);
    qss.replace(QLatin1String("%INTERACTIVEHOVER%"), interactiveHover);
    qss.replace(QLatin1String("%INTERACTIVESUBTLE%"), interactiveSubtle);
    qss.replace(QLatin1String("%HOVER%"), hover);
    qss.replace(QLatin1String("%BORDER%"), border);
    qss.replace(QLatin1String("%MUTED%"), muted);
    qss.replace(QLatin1String("%ROWALT%"), rowAlt);
    qss.replace(QLatin1String("%CHEVRON%"), chevron);
    qss.replace(QLatin1String("%CHECK%"), check);
    return qss;
}

} // namespace fm
