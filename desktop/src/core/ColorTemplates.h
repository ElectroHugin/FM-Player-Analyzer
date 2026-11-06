#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace fm {

// A complete color scheme saved by the user under a name: all seven color
// roles of both modes (night_*/day_* keys of ThemeSettings).
struct ColorTemplate {
    QString id;     // "user:<uuid>", also stored as ThemeSettings/theme_preset
    QString name;
    QString league; // empty = "Custom"
    QHash<QString, QString> colors;
};

// The user's own color templates. App-wide on purpose: they live in the data
// directory (color_templates.json next to config.ini), not in a save's
// profile, so every save can use them and app updates never touch them.
// Applying one only changes the open save's profile (SettingsPage).
class ColorTemplateStore
{
public:
    explicit ColorTemplateStore(QString filePath);

    // The 14 color keys a template holds ("night_background_color", …).
    static const QStringList &colorKeys();
    static bool isTemplateId(const QString &presetId);

    // Reads the file; a missing file is an empty store. Returns false (and
    // keeps the previous list) if the file exists but cannot be read.
    bool load();

    const QList<ColorTemplate> &templates() const { return m_templates; }
    const ColorTemplate *find(const QString &id) const;
    // Leagues used by templates (non-empty, sorted), for the save dialog.
    QStringList leagues() const;

    // Saves a template. Same name in the same league replaces that template
    // (keeps its id). Missing color keys are rejected. Returns the id, or an
    // empty string on failure (see lastError()).
    QString save(const QString &name, const QString &league,
                 const QHash<QString, QString> &colors);
    bool remove(const QString &id);

    QString lastError() const { return m_error; }
    QString filePath() const { return m_path; }

private:
    bool write();

    QString m_path;
    QList<ColorTemplate> m_templates;
    QString m_error;
};

} // namespace fm
