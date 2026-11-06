#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

#include "core/ThemeColors.h"

namespace fm {

class AppConfig;

// Applies the day/night theme of the open save's profile (AppConfig
// themeSettings(); the start screen without a save uses the last save's look)
// as an app-wide stylesheet and exposes the palette to custom-painted widgets.
//
// The theme is described by seven named color roles per mode so color can be
// distributed instead of one "secondary background" tinting everything:
//   background   – page background
//   surface      – panels, cards, inputs, group boxes
//   sidebar      – navigation sidebar background
//   header       – top identity bar background
//   primary      – primary buttons, active nav entry (the club's main color)
//   interactive  – tabs, sliders, checkboxes, focus, selection (the accent)
//   text         – primary text
// border and muted text are derived from these.
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    explicit ThemeManager(AppConfig &config, QObject *parent = nullptr);

    // The seven configurable role names (without the "night_"/"day_" prefix
    // and "_color" suffix used as settings keys).
    static const QStringList &roleNames();
    // Settings key for a role in a mode, e.g. ("night","primary") ->
    // "night_primary_color".
    static QString settingsKey(const QString &mode, const QString &role);

    QString mode() const { return m_mode; } // "day" | "night"
    void setMode(const QString &mode);
    void toggle();

    // Re-read colors from config (after the settings page saved them).
    void apply();
    // Re-read mode and colors (another save's profile was loaded).
    void reload();

    // --- Role colors (current mode) ---
    QColor background() const { return m_background; }
    QColor surface() const { return m_surface; }
    QColor sidebar() const { return m_sidebar; }
    QColor header() const { return m_header; }
    QColor primary() const { return m_primary; }
    QColor interactive() const { return m_interactive; }
    QColor text() const { return m_text; }
    // Backward-compatible alias used by custom-painted widgets.
    QColor secondaryBackground() const { return m_surface; }

    // --- Derived, contrast-safe colors (club colors can be anything) ---
    // The accent as text/icon on background and cards (>= 4.5:1).
    QColor accentText() const { return m_accentText; }
    // The accent for focus rings, underlines, outlines (>= 3:1, WCAG 1.4.11).
    QColor accentUi() const { return m_accentUi; }
    // Club header: "gradient" (club colors, default) or "solid" (header color).
    QString headerStyle() const { return m_headerStyle; }
    // Faint crest silhouette in the club header.
    bool crestWatermark() const { return m_crestWatermark; }
    // Colors of the club header block for the current mode.
    ClubHeaderPalette headerPalette() const { return m_headerPalette; }

signals:
    void themeChanged();

private:
    QString buildStyleSheet() const;
    QColor roleColor(const QString &role) const;

    AppConfig &m_config;
    QString m_mode;
    QColor m_background, m_surface, m_sidebar, m_header, m_primary, m_interactive, m_text;
    QColor m_accentText, m_accentUi;
    QString m_headerStyle;
    bool m_crestWatermark = true;
    ClubHeaderPalette m_headerPalette;
};

} // namespace fm
