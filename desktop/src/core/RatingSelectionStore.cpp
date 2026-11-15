#include "RatingSelectionStore.h"
#include "ClassicRatingSnapshot.h"
#include "Database.h"
#include "Definitions.h"
#include "ProfileStore.h"
#include "RatingSnapshot.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QUuid>
namespace fm {
namespace {
std::optional<RatingSelection> fail(QString *error, const QString &message) {
    if (error) *error = message; return {};
}
QString generation() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QJsonObject encode(const RatingSelection &s) {
    return {{"format", "fm-rating-selection"}, {"version", 1}, {"game", s.game},
        {"generation", s.generation}, {"computationKey", s.computationKey}, {"migrationDecision", s.migrationDecision},
        {"profile", s.profile.toJson()}, {"definitions", s.definitions}, {"legacyBasis", s.legacyBasis}, {"switches", s.switches}};
}
}
QString RatingSelectionStore::documentName() { return QStringLiteral("dwrs_rating_selection"); }
std::optional<RatingSelection> RatingSelectionStore::load(QString *error)
{
    if (error) error->clear();
    ProfileStore store(m_database);
    if (!store.ensureTables()) return fail(error, store.errorString());
    QByteArray bytes;
    if (!store.readDocument(documentName(), bytes)) {
        if (!store.errorString().isEmpty()) return fail(error, store.errorString());
        return {};
    }
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return fail(error, "invalid-rating-selection");
    const auto json = document.object();
    if (json.size() != 10 || json.value("format") != QJsonValue("fm-rating-selection") || json.value("version") != QJsonValue(1)
        || !json.value("profile").isObject() || !json.value("definitions").isObject()
        || !json.value("legacyBasis").isObject() || !json.value("switches").isArray()) return fail(error, "invalid-rating-selection");
    RatingSelection result;
    result.game = json.value("game").toString(); result.generation = json.value("generation").toString();
    result.computationKey = json.value("computationKey").toString(); result.migrationDecision = json.value("migrationDecision").toString();
    if (result.generation.isEmpty() || (result.game != "fm24" && result.game != "fm27")
        || (result.migrationDecision != "pending" && result.migrationDecision != "kept"
            && result.migrationDecision != "switched" && result.migrationDecision != "not-needed")) return fail(error, "invalid-rating-selection");
    const auto profile = RatingProfile::fromJson(json.value("profile").toObject(), error); if (!profile) return {};
    result.profile = *profile; result.definitions = json.value("definitions").toObject();
    result.legacyBasis = json.value("legacyBasis").toObject(); result.switches = json.value("switches").toArray();
    if (!result.legacyBasis.isEmpty()) {
        const auto legacy = ClassicRatingSnapshot::fromJson(result.legacyBasis, error);
        if (!legacy || legacy->game() != result.game) return fail(error, "invalid-legacy-basis");
    }
    if (result.migrationDecision == "pending" && (result.game != "fm27" || result.legacyBasis.isEmpty()))
        return fail(error, "invalid-migration-decision");
    Definitions definitions;
    if (!definitions.loadFromJson(QJsonDocument(result.definitions).toJson(), result.game)) return fail(error, definitions.errorString());
    const auto compiled = RatingSnapshot::compile(result.profile, definitions, error);
    if (!compiled || compiled->cacheKey() != result.computationKey) return fail(error, "rating-selection-revision-mismatch");
    return result;
}
bool RatingSelectionStore::write(const RatingSelection &selection, QString *error)
{
    ProfileStore store(m_database);
    const auto basisName = QStringLiteral("dwrs_rating_basis_") + selection.computationKey;
    QByteArray archived;
    if (!store.readDocument(basisName, archived)) {
        if (!store.errorString().isEmpty()) { if (error) *error = store.errorString(); return false; }
        const QJsonObject basis{{"game", selection.game}, {"profile", selection.profile.toJson()},
                                {"definitions", selection.definitions}, {"computationKey", selection.computationKey}};
        if (!store.writeDocument(basisName, QJsonDocument(basis).toJson(QJsonDocument::Compact), true)) {
            if (error) *error = store.errorString(); return false;
        }
    } else {
        const auto basis = QJsonDocument::fromJson(archived).object();
        const auto profile = RatingProfile::fromJson(basis.value("profile").toObject(), error);
        Definitions definitions;
        if (!profile || !definitions.loadFromJson(QJsonDocument(basis.value("definitions").toObject()).toJson(), basis.value("game").toString())) {
            if (error) *error = QStringLiteral("invalid-rating-archive"); return false;
        }
        const auto compiled = RatingSnapshot::compile(*profile, definitions, error);
        if (!compiled || compiled->cacheKey() != selection.computationKey) {
            if (error) *error = QStringLiteral("invalid-rating-archive"); return false;
        }
    }
    if (!store.writeDocument(documentName(), QJsonDocument(encode(selection)).toJson(QJsonDocument::Compact), true)) {
        if (error) *error = store.errorString(); return false;
    }
    return true;
}
std::optional<RatingSelection> RatingSelectionStore::initialize(const RatingProfile &profile, const Definitions &definitions,
    bool existingSave, std::shared_ptr<const ClassicRatingSnapshot> legacy, QString *error)
{
    QString localError; if (!error) error = &localError; error->clear();
    ScopedTransaction transaction(m_database);
    if (!transaction.isActive()) return fail(error, m_database.errorString());
    const auto previous = load(error);
    if (previous) { if (!transaction.commit()) return fail(error, m_database.errorString()); return previous; }
    if (!error->isEmpty()) return {};
    if (legacy && legacy->game() != definitions.fmVersionId()) return fail(error, "incompatible-legacy-basis");
    if (existingSave && profile.calculationVersion != QStringLiteral("classic-midpoint-v1")) return fail(error, "existing-save-requires-classic");
    if (existingSave && (!legacy || legacy->game() != definitions.fmVersionId())) return fail(error, "legacy-basis-required");
    const auto compiled = RatingSnapshot::compile(profile, definitions, error); if (!compiled) return {};
    RatingSelection result; result.game = definitions.fmVersionId(); result.generation = generation(); result.profile = profile;
    result.computationKey = compiled->cacheKey(); result.definitions = QJsonDocument::fromJson(definitions.toJson()).object();
    result.migrationDecision = existingSave && result.game == "fm27" ? QStringLiteral("pending") : QStringLiteral("not-needed");
    if (legacy) result.legacyBasis = legacy->toJson();
    if (!write(result, error)) return {};
    if (!transaction.commit()) return fail(error, m_database.errorString());
    return result;
}
std::optional<RatingSelection> RatingSelectionStore::activate(const RatingProfile &profile, const Definitions &definitions,
    const QString &expectedGeneration, const QString &saveDate, QString *error)
{
    if (error) error->clear();
    if (!saveDate.isEmpty() && !QDate::fromString(saveDate, Qt::ISODate).isValid()) return fail(error, "invalid-save-date");
    const auto compiled = RatingSnapshot::compile(profile, definitions, error); if (!compiled) return {};
    ScopedTransaction transaction(m_database);
    if (!transaction.isActive()) return fail(error, m_database.errorString());
    auto current = load(error); if (!current) { if (!error || error->isEmpty()) return fail(error, "selection-not-initialized"); return {}; }
    if (current->generation != expectedGeneration) return fail(error, "selection-conflict");
    if (current->game != definitions.fmVersionId()) return fail(error, "incompatible-game");
    if (current->computationKey != compiled->cacheKey()) {
        current->switches.append(QJsonObject{{"from", current->computationKey}, {"to", compiled->cacheKey()},
            {"at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"saveDate", saveDate},
            {"profileId", profile.id}, {"profileRevision", profile.revision()}});
    }
    current->profile = profile; current->computationKey = compiled->cacheKey(); current->generation = generation();
    current->definitions = QJsonDocument::fromJson(definitions.toJson()).object();
    if (current->migrationDecision == "pending") current->migrationDecision = "switched";
    if (!write(*current, error)) return {};
    if (!transaction.commit()) return fail(error, m_database.errorString());
    return current;
}
std::optional<RatingSelection> RatingSelectionStore::keepPrevious(const QString &expectedGeneration, QString *error)
{
    if (error) error->clear();
    ScopedTransaction transaction(m_database);
    if (!transaction.isActive()) return fail(error, m_database.errorString());
    auto current = load(error); if (!current) { if (!error || error->isEmpty()) return fail(error, "selection-not-initialized"); return {}; }
    if (current->generation != expectedGeneration) return fail(error, "selection-conflict");
    if (current->migrationDecision != "pending") return fail(error, "migration-already-decided");
    current->migrationDecision = "kept"; current->generation = generation();
    if (!write(*current, error)) return {};
    if (!transaction.commit()) return fail(error, m_database.errorString());
    return current;
}
}
