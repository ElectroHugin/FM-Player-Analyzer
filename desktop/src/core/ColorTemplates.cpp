#include "ColorTemplates.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace fm {

namespace {

constexpr int kFormat = 1;
const QString kIdPrefix = QStringLiteral("user:");

} // namespace

ColorTemplateStore::ColorTemplateStore(QString filePath)
    : m_path(std::move(filePath))
{
}

const QStringList &ColorTemplateStore::colorKeys()
{
    static const QStringList keys = [] {
        QStringList list;
        for (const char *mode : {"night", "day"}) {
            for (const char *role : {"background", "surface", "sidebar", "header", "primary",
                                     "interactive", "text"})
                list << QStringLiteral("%1_%2_color").arg(QLatin1String(mode), QLatin1String(role));
        }
        return list;
    }();
    return keys;
}

bool ColorTemplateStore::isTemplateId(const QString &presetId)
{
    return presetId.startsWith(kIdPrefix);
}

bool ColorTemplateStore::load()
{
    m_error.clear();
    QFile file(m_path);
    if (!file.exists()) {
        m_templates.clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        m_error = parseError.errorString();
        return false;
    }

    // A newer format may add fields; whatever this version understands is used.
    QList<ColorTemplate> loaded;
    for (const QJsonValue &value : doc.object().value(QStringLiteral("templates")).toArray()) {
        const QJsonObject object = value.toObject();
        ColorTemplate entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.name = object.value(QStringLiteral("name")).toString().trimmed();
        entry.league = object.value(QStringLiteral("league")).toString().trimmed();
        const QJsonObject colors = object.value(QStringLiteral("colors")).toObject();
        bool complete = isTemplateId(entry.id) && !entry.name.isEmpty();
        for (const QString &key : colorKeys()) {
            const QString color = colors.value(key).toString();
            if (!QColor::isValidColorName(color))
                complete = false;
            entry.colors.insert(key, color);
        }
        if (complete)
            loaded.append(entry);
    }
    m_templates = loaded;
    return true;
}

const ColorTemplate *ColorTemplateStore::find(const QString &id) const
{
    for (const ColorTemplate &entry : m_templates) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

QStringList ColorTemplateStore::leagues() const
{
    QStringList result;
    for (const ColorTemplate &entry : m_templates) {
        if (!entry.league.isEmpty() && !result.contains(entry.league))
            result << entry.league;
    }
    std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    return result;
}

QString ColorTemplateStore::save(const QString &name, const QString &league,
                                 const QHash<QString, QString> &colors)
{
    m_error.clear();
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        m_error = QStringLiteral("empty name");
        return {};
    }
    ColorTemplate entry;
    entry.name = trimmedName;
    entry.league = league.trimmed();
    for (const QString &key : colorKeys()) {
        const QString color = colors.value(key);
        if (!QColor::isValidColorName(color)) {
            m_error = QStringLiteral("missing color %1").arg(key);
            return {};
        }
        entry.colors.insert(key, QColor(color).name());
    }

    const QList<ColorTemplate> before = m_templates;
    auto existing = std::find_if(m_templates.begin(), m_templates.end(),
                                 [&](const ColorTemplate &t) {
                                     return t.name.compare(entry.name, Qt::CaseInsensitive) == 0
                                            && t.league.compare(entry.league,
                                                                Qt::CaseInsensitive) == 0;
                                 });
    if (existing != m_templates.end()) {
        entry.id = existing->id;
        *existing = entry;
    } else {
        entry.id = kIdPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_templates.append(entry);
    }
    if (!write()) {
        m_templates = before;
        return {};
    }
    return entry.id;
}

bool ColorTemplateStore::remove(const QString &id)
{
    m_error.clear();
    const QList<ColorTemplate> before = m_templates;
    const auto removed = m_templates.removeIf([&](const ColorTemplate &t) { return t.id == id; });
    if (removed == 0)
        return true;
    if (!write()) {
        m_templates = before;
        return false;
    }
    return true;
}

bool ColorTemplateStore::write()
{
    QJsonArray list;
    for (const ColorTemplate &entry : m_templates) {
        QJsonObject colors;
        for (const QString &key : colorKeys())
            colors.insert(key, entry.colors.value(key));
        list.append(QJsonObject{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("league"), entry.league},
            {QStringLiteral("colors"), colors},
        });
    }
    const QJsonObject root{{QStringLiteral("format"), kFormat},
                           {QStringLiteral("templates"), list}};

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    // Written to a temp file and swapped in: a crash never leaves half a file.
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        m_error = file.errorString();
        return false;
    }
    return true;
}

} // namespace fm
