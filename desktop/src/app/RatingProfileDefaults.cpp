#include "RatingProfileDefaults.h"
#include "core/BuiltinRatingProfiles.h"
#include "core/AppConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

namespace fm {
QList<RatingProfile> loadBuiltinRatingProfiles(QString *error)
{
    if (error) error->clear();
    const auto fail = [&](const QString &message) { if (error) *error = message; return QList<RatingProfile>{}; };
    QList<Definitions> definitions;
    for (const auto &game : {QStringLiteral("fm24"), QStringLiteral("fm27")}) {
        Definitions d;
        const auto path = game == "fm24" ? QStringLiteral(":/defaults/definitions.json") : QStringLiteral(":/defaults/definitions_fm27.json");
        if (!d.load(path, game)) return fail(d.errorString());
        definitions.append(d);
    }
    // Use clean defaults without reading or changing any save or host settings.
    QTemporaryDir temporary;
    if (!temporary.isValid()) return fail(QStringLiteral("temporary-defaults-unavailable"));
    AppConfig defaults(temporary.filePath(QStringLiteral("defaults.ini")));
    const auto classic = classicRatingProfile(definitions, defaults, error); if (!classic) return {};
    QFile file(QStringLiteral(":/defaults/harvestgreen-comparison.json"));
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QJsonParseError parse; const auto data = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !data.isObject()) return fail(QStringLiteral("invalid-builtin-source"));
    const auto harvest = harvestgreenRatingProfile(definitions, defaults, data.object(), error); if (!harvest) return {};
    return {*classic, *harvest};
}
}
