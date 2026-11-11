#include "RatingProfileLibrary.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>

namespace fm {
namespace {
QByteArray digest(const QByteArray &data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256); }
QByteArray profileBytes(const RatingProfile &p) { return QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact); }
bool nameTaken(const QMap<QString, RatingProfile> &profiles, const RatingProfile &candidate)
{
    for (const auto &p : profiles)
        if (p.id != candidate.id && p.name.trimmed().compare(candidate.name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}
}

QString RatingProfileLibrary::documentRevision(const RatingProfile &profile)
{
    return QString::fromLatin1(digest(profileBytes(profile)).toHex());
}

QString RatingProfileLibrary::seedBuiltins(const QList<RatingProfile> &profiles)
{
    auto candidate = *this;
    for (const auto &profile : profiles) {
        if (candidate.m_originals.contains(profile.id)) continue;
        const auto error = candidate.insert(profile); if (!error.isEmpty()) return error;
    }
    *this = std::move(candidate); return {};
}

QString RatingProfileLibrary::insert(const RatingProfile &profile)
{
    const auto error = profile.validate();
    if (!error.isEmpty()) return error;
    if (m_originals.contains(profile.id)) return QStringLiteral("id-conflict");
    if (nameTaken(m_profiles, profile)) return QStringLiteral("name-conflict");
    m_profiles.insert(profile.id, profile);
    m_originals.insert(profile.id, profile);
    if (!m_snapshots.contains(profile.revision())) m_snapshots.insert(profile.revision(), profile);
    return {};
}

QString RatingProfileLibrary::update(const RatingProfile &profile, const QString &expectedDocumentRevision)
{
    const auto current = m_profiles.constFind(profile.id);
    if (current == m_profiles.constEnd()) return QStringLiteral("profile-not-found");
    if (documentRevision(*current) != expectedDocumentRevision) return QStringLiteral("concurrent-edit");
    const auto error = profile.validate();
    if (!error.isEmpty()) return error;
    if (nameTaken(m_profiles, profile)) return QStringLiteral("name-conflict");
    m_profiles.insert(profile.id, profile);
    // Preserve the first immutable snapshot for a formula revision. Historical
    // display metadata belongs to each history event, not this lookup.
    if (!m_snapshots.contains(profile.revision())) m_snapshots.insert(profile.revision(), profile);
    return {};
}

QString RatingProfileLibrary::duplicate(const QString &id, const QString &name, QString *newId)
{
    const auto original = m_profiles.constFind(id);
    if (original == m_profiles.constEnd()) return QStringLiteral("profile-not-found");
    auto copy = *original;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = name;
    const auto error = insert(copy);
    if (error.isEmpty() && newId) *newId = copy.id;
    return error;
}

QString RatingProfileLibrary::remove(const QString &id)
{
    if (!m_profiles.remove(id)) return QStringLiteral("profile-not-found");
    // Formula snapshots remain available for history. Originals stay available
    // internally and are not silently reinserted by application updates.
    return {};
}

QString RatingProfileLibrary::restore(const QString &id)
{
    if (!m_profiles.contains(id) || !m_originals.contains(id)) return QStringLiteral("profile-not-found");
    auto original = m_originals.value(id);
    original.name = m_profiles.value(id).name;
    return update(original, documentRevision(m_profiles.value(id)));
}

bool RatingProfileLibrary::customized(const QString &id) const
{
    return m_profiles.contains(id) && m_originals.contains(id)
           && m_profiles.value(id).revision() != m_originals.value(id).revision();
}

std::optional<RatingProfile> RatingProfileLibrary::formulaSnapshot(const QString &revision) const
{
    const auto it = m_snapshots.constFind(revision);
    return it == m_snapshots.constEnd() ? std::nullopt : std::optional(*it);
}

QByteArray RatingProfileLibrary::toJson() const
{
    auto encode = [](const QMap<QString, RatingProfile> &profiles) {
        QJsonObject result;
        for (auto it = profiles.begin(); it != profiles.end(); ++it)
            result.insert(it.key(), it->toJson());
        return result;
    };
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("fm-dwrs-library")},
                                    {QStringLiteral("version"), 1},
                                    {QStringLiteral("profiles"), encode(m_profiles)},
                                    {QStringLiteral("originals"), encode(m_originals)},
                                    {QStringLiteral("snapshots"), encode(m_snapshots)}}).toJson();
}

std::optional<RatingProfileLibrary> RatingProfileLibrary::fromJson(const QByteArray &data, QString *error)
{
    if (error) error->clear();
    auto fail = [&](const QString &message) -> std::optional<RatingProfileLibrary> {
        if (error) *error = message;
        return std::nullopt;
    };
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(data, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("invalid-json"));
    const auto root = document.object();
    if (root.size() != 5 || root.value(QStringLiteral("format")) != QStringLiteral("fm-dwrs-library")
        || root.value(QStringLiteral("version")) != 1)
        return fail(QStringLiteral("unsupported-library-format"));
    RatingProfileLibrary result;
    for (const auto &key : {QStringLiteral("profiles"), QStringLiteral("originals"), QStringLiteral("snapshots")}) {
        if (!root.value(key).isObject()) return fail(QStringLiteral("invalid-library-section"));
        const auto section = root.value(key).toObject();
        auto &target = key == QStringLiteral("profiles") ? result.m_profiles
                       : key == QStringLiteral("originals") ? result.m_originals : result.m_snapshots;
        for (auto it = section.begin(); it != section.end(); ++it) {
            if (!it->isObject()) return fail(QStringLiteral("invalid-profile"));
            QString issue;
            const auto p = RatingProfile::fromJson(it->toObject(), &issue);
            if (!p) return fail(issue);
            if (it.key() != (key == QStringLiteral("snapshots") ? p->revision() : p->id))
                return fail(QStringLiteral("profile-key-mismatch"));
            if (key == QStringLiteral("profiles") && nameTaken(target, *p))
                return fail(QStringLiteral("name-conflict"));
            target.insert(it.key(), *p);
        }
    }
    for (const auto &p : result.m_profiles) {
        if (!result.m_originals.contains(p.id) || !result.m_snapshots.contains(p.revision()))
            return fail(QStringLiteral("incomplete-library"));
    }
    return result;
}

std::optional<RatingProfileLibrary> RatingProfileLibrary::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    const auto bytes = file.readAll();
    auto result = fromJson(bytes, error);
    if (result) {
        result->m_loadedDigest = digest(bytes);
        result->m_loadedPath = QFileInfo(path).absoluteFilePath();
    }
    return result;
}

QString RatingProfileLibrary::save(const QString &path)
{
    // Ensure even detached/corrupt state cannot replace a valid library.
    QString error;
    const auto bytes = toJson();
    if (!fromJson(bytes, &error)) return error;
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) return QStringLiteral("library-busy");
    const auto absolutePath = QFileInfo(path).absoluteFilePath();
    QFile old(path);
    if (old.exists()) {
        if (m_loadedPath != absolutePath || !old.open(QIODevice::ReadOnly)
            || digest(old.readAll()) != m_loadedDigest)
            return QStringLiteral("library-conflict");
        old.close();
    } else if (m_loadedPath == absolutePath && !m_loadedDigest.isEmpty()) {
        return QStringLiteral("library-conflict");
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    m_loadedPath = absolutePath;
    m_loadedDigest = digest(bytes);
    return {};
}
} // namespace fm
