#include "TacticLibrary.h"

#include "Database.h"
#include "Definitions.h"
#include "PitchGeometry.h"
#include "Version.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace fm {

namespace TacticLibrary {

namespace {

const QString kFormatId = QStringLiteral("fm-player-analyzer-tactic");

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &v : value.toArray())
        out << v.toString();
    return out;
}

// Adds/removes a name in one of the string lists of the root.
void listInsert(QJsonObject &root, const QString &key, const QString &name)
{
    QStringList list = stringList(root.value(key));
    if (!list.contains(name)) {
        list << name;
        root.insert(key, QJsonArray::fromStringList(list));
    }
}

void listRemove(QJsonObject &root, const QString &key, const QString &name)
{
    QStringList list = stringList(root.value(key));
    if (list.removeAll(name) > 0) {
        if (list.isEmpty())
            root.remove(key);
        else
            root.insert(key, QJsonArray::fromStringList(list));
    }
}

bool isTwoPhase(const QJsonObject &roles)
{
    return roles.contains(QLatin1String("ip")) || roles.contains(QLatin1String("oop"));
}

QList<QJsonObject> lineUpsOf(const QJsonObject &roles)
{
    if (isTwoPhase(roles))
        return {roles.value(QLatin1String("ip")).toObject(),
                roles.value(QLatin1String("oop")).toObject()};
    return {roles};
}

QJsonObject baseLineUp(const QJsonObject &roles)
{
    return isTwoPhase(roles) ? roles.value(QLatin1String("oop")).toObject() : roles;
}

// Category of a role in a definitions root ("" if unknown).
QString categoryOf(const QJsonObject &root, const QString &role)
{
    const QJsonObject categories = root.value(QLatin1String("player_roles")).toObject();
    for (auto cat = categories.begin(); cat != categories.end(); ++cat) {
        if (cat.value().toObject().contains(role))
            return cat.key();
    }
    return {};
}

ImportedRole describeRole(const QJsonObject &root, const QString &role)
{
    ImportedRole out;
    out.id = role;
    out.category = categoryOf(root, role);
    out.name = root.value(QLatin1String("player_roles"))
                   .toObject()
                   .value(out.category)
                   .toObject()
                   .value(role)
                   .toString();
    out.phase = root.value(QLatin1String("role_phases")).toObject().value(role).toString();
    const QJsonObject weights =
        root.value(QLatin1String("role_specific_weights")).toObject().value(role).toObject();
    out.key = stringList(weights.value(QLatin1String("key")));
    out.preferable = stringList(weights.value(QLatin1String("preferable")));
    const QJsonObject mapping = root.value(QLatin1String("position_to_role_mapping")).toObject();
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        if (stringList(it.value()).contains(role))
            out.positions << it.key();
    }
    out.positions.sort();
    return out;
}

QJsonObject roleToJson(const ImportedRole &role)
{
    QJsonObject obj{{QStringLiteral("name"), role.name},
                    {QStringLiteral("category"), role.category},
                    {QStringLiteral("positions"), QJsonArray::fromStringList(role.positions)},
                    {QStringLiteral("key"), QJsonArray::fromStringList(role.key)},
                    {QStringLiteral("preferable"), QJsonArray::fromStringList(role.preferable)}};
    if (!role.phase.isEmpty())
        obj.insert(QStringLiteral("phase"), role.phase);
    return obj;
}

bool sameRole(const ImportedRole &a, const ImportedRole &b)
{
    QStringList ak = a.key, bk = b.key, ap = a.preferable, bp = b.preferable;
    ak.sort();
    bk.sort();
    ap.sort();
    bp.sort();
    return a.name == b.name && a.phase == b.phase && ak == bk && ap == bp
           && a.positions == b.positions;
}

} // namespace

QString exchangeSuffix()
{
    return QStringLiteral("fmpatactic");
}

QJsonObject layoutFor(const QString &fmVersionId, const QJsonObject &lineUp)
{
    const auto &slotMap = masterPositionMap(fmVersionId);
    QJsonObject layout;
    // Slots in pitch order within each line, so the layout is stable.
    QStringList keys = lineUp.keys();
    std::sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b) {
        return slotMap.value(a).column < slotMap.value(b).column;
    });
    for (const QString &slot : std::as_const(keys)) {
        if (slot == QLatin1String("GK"))
            continue;
        const auto it = slotMap.constFind(slot);
        if (it == slotMap.constEnd())
            continue;
        QJsonArray line = layout.value(it->stratum).toArray();
        line.append(slot);
        layout.insert(it->stratum, line);
    }
    return layout;
}

void putTactic(QJsonObject &root, const QString &fmVersionId, const QString &name,
               const QJsonObject &roles, const QJsonObject &layout)
{
    QJsonObject tactics = root.value(QLatin1String("tactic_roles")).toObject();
    tactics.insert(name, roles);
    root.insert(QStringLiteral("tactic_roles"), tactics);
    QJsonObject layouts = root.value(QLatin1String("tactic_layouts")).toObject();
    layouts.insert(name, layout.isEmpty() ? layoutFor(fmVersionId, baseLineUp(roles)) : layout);
    root.insert(QStringLiteral("tactic_layouts"), layouts);
    listRemove(root, QStringLiteral("deleted_tactics"), name);
    listInsert(root, QStringLiteral("edited_tactics"), name);
}

bool removeTactic(QJsonObject &root, const QString &name)
{
    QJsonObject tactics = root.value(QLatin1String("tactic_roles")).toObject();
    if (!tactics.contains(name))
        return false;
    tactics.remove(name);
    root.insert(QStringLiteral("tactic_roles"), tactics);
    QJsonObject layouts = root.value(QLatin1String("tactic_layouts")).toObject();
    layouts.remove(name);
    root.insert(QStringLiteral("tactic_layouts"), layouts);
    listRemove(root, QStringLiteral("edited_tactics"), name);
    listInsert(root, QStringLiteral("deleted_tactics"), name);
    return true;
}

bool renameTactic(QJsonObject &root, const QString &oldName, const QString &newName)
{
    const QJsonObject tactics = root.value(QLatin1String("tactic_roles")).toObject();
    if (!tactics.contains(oldName) || newName.trimmed().isEmpty() || tactics.contains(newName))
        return false;
    const QJsonObject roles = tactics.value(oldName).toObject();
    const QJsonObject layout =
        root.value(QLatin1String("tactic_layouts")).toObject().value(oldName).toObject();
    removeTactic(root, oldName);
    // putTactic without a version: keep the stored layout as it is.
    QJsonObject all = root.value(QLatin1String("tactic_roles")).toObject();
    all.insert(newName, roles);
    root.insert(QStringLiteral("tactic_roles"), all);
    QJsonObject layouts = root.value(QLatin1String("tactic_layouts")).toObject();
    layouts.insert(newName, layout);
    root.insert(QStringLiteral("tactic_layouts"), layouts);
    listRemove(root, QStringLiteral("deleted_tactics"), newName);
    listInsert(root, QStringLiteral("edited_tactics"), newName);
    return true;
}

namespace {
const QStringList &favoriteKeys()
{
    static const QStringList keys{QStringLiteral("favorite_tactic_1"),
                                  QStringLiteral("favorite_tactic_2"),
                                  QStringLiteral("national_fav_tactic_1"),
                                  QStringLiteral("national_fav_tactic_2")};
    return keys;
}
} // namespace

void renameTacticReferences(Database &db, const QString &oldName, const QString &newName)
{
    for (const QString &key : favoriteKeys()) {
        if (db.setting(key) == oldName)
            db.setSetting(key, newName);
    }
    const QString bridges = db.setting(QStringLiteral("bridges/") + oldName);
    if (!bridges.isEmpty()) {
        db.setSetting(QStringLiteral("bridges/") + newName, bridges);
        db.removeSetting(QStringLiteral("bridges/") + oldName);
    }
}

void removeTacticReferences(Database &db, const QString &name)
{
    for (const QString &key : favoriteKeys()) {
        if (db.setting(key) == name)
            db.removeSetting(key);
    }
    db.removeSetting(QStringLiteral("bridges/") + name);
}

void pruneBridges(Database &db, const QString &tactic, const QStringList &ipSlots,
                  const QStringList &oopSlots)
{
    const QString key = QStringLiteral("bridges/") + tactic;
    const QString stored = db.setting(key);
    if (stored.isEmpty())
        return;
    QJsonObject bridges = QJsonDocument::fromJson(stored.toUtf8()).object();
    // Two players on one slot are impossible as well, but an edit only
    // removes or renames slots, so checking existence is enough.
    QStringList drop;
    for (auto it = bridges.begin(); it != bridges.end(); ++it) {
        const QJsonArray pair = it.value().toArray();
        if (pair.size() != 2 || !ipSlots.contains(pair.at(0).toString())
            || !oopSlots.contains(pair.at(1).toString()))
            drop << it.key();
    }
    if (drop.isEmpty())
        return;
    for (const QString &uid : std::as_const(drop))
        bridges.remove(uid);
    if (bridges.isEmpty())
        db.removeSetting(key);
    else
        db.setSetting(key, QString::fromUtf8(QJsonDocument(bridges).toJson(QJsonDocument::Compact)));
}

QByteArray exportTactic(const Definitions &definitions, const QString &name)
{
    const QJsonObject root = definitions.root();
    const QJsonObject roles =
        root.value(QLatin1String("tactic_roles")).toObject().value(name).toObject();
    if (roles.isEmpty())
        return {};
    QJsonObject tactic{{QStringLiteral("name"), name}};
    if (isTwoPhase(roles)) {
        tactic.insert(QStringLiteral("ip"), roles.value(QLatin1String("ip")));
        tactic.insert(QStringLiteral("oop"), roles.value(QLatin1String("oop")));
    } else {
        tactic.insert(QStringLiteral("lineup"), roles);
    }
    tactic.insert(QStringLiteral("layout"),
                  root.value(QLatin1String("tactic_layouts")).toObject().value(name));

    QSet<QString> used;
    for (const QJsonObject &lineUp : lineUpsOf(roles)) {
        for (auto it = lineUp.begin(); it != lineUp.end(); ++it)
            used.insert(it.value().toString());
    }
    QStringList sorted(used.begin(), used.end());
    sorted.sort();
    QJsonObject roleDefs;
    for (const QString &role : std::as_const(sorted))
        roleDefs.insert(role, roleToJson(describeRole(root, role)));

    const QJsonObject file{
        {QStringLiteral("format"), kFormatId},
        {QStringLiteral("format_version"), kExchangeFormatVersion},
        {QStringLiteral("app_version"), appVersion()},
        {QStringLiteral("fm_version"), definitions.fmVersionId()},
        {QStringLiteral("exported_at"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("tactic"), tactic},
        {QStringLiteral("roles"), roleDefs},
    };
    return QJsonDocument(file).toJson(QJsonDocument::Indented);
}

ImportCheck checkImport(const QByteArray &data, const Definitions &definitions)
{
    ImportCheck check;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        check.error = QStringLiteral("not a JSON object: %1").arg(parseError.errorString());
        return check;
    }
    const QJsonObject file = doc.object();
    if (file.value(QLatin1String("format")).toString() != kFormatId) {
        check.error = QStringLiteral("not a tactic file of the analyzer");
        return check;
    }
    const int version = file.value(QLatin1String("format_version")).toInt(0);
    if (version < 1) {
        check.error = QStringLiteral("format_version missing");
        return check;
    }
    check.fileFmVersion = file.value(QLatin1String("fm_version")).toString();
    const QJsonObject tactic = file.value(QLatin1String("tactic")).toObject();
    check.name = tactic.value(QLatin1String("name")).toString().trimmed();
    if (version > kExchangeFormatVersion) {
        check.status = ImportCheck::TooNew;
        return check;
    }
    if (check.fileFmVersion != definitions.fmVersionId()) {
        check.status = ImportCheck::WrongFmVersion;
        return check;
    }
    if (check.name.isEmpty()) {
        check.error = QStringLiteral("tactic name missing");
        return check;
    }

    // Line-ups: the shape has to match the profile (two phases or one).
    const bool fileTwoPhase = tactic.contains(QLatin1String("ip"));
    if (fileTwoPhase != definitions.hasRolePhases()) {
        check.error = QStringLiteral("line-up shape does not match the FM version");
        return check;
    }
    if (fileTwoPhase)
        check.roles = QJsonObject{{QStringLiteral("ip"), tactic.value(QLatin1String("ip"))},
                                  {QStringLiteral("oop"), tactic.value(QLatin1String("oop"))}};
    else
        check.roles = tactic.value(QLatin1String("lineup")).toObject();

    const PitchGeometry &geometry = pitchGeometry(definitions.fmVersionId());
    QSet<QString> used;
    for (const QJsonObject &lineUp : lineUpsOf(check.roles)) {
        if (lineUp.size() != 11 || !lineUp.contains(QLatin1String("GK"))) {
            check.error = QStringLiteral("a line-up needs a goalkeeper and ten outfield players");
            return check;
        }
        for (auto it = lineUp.begin(); it != lineUp.end(); ++it) {
            if (it.key() != QLatin1String("GK") && !geometry.slot(it.key())) {
                check.error = QStringLiteral("unknown slot %1").arg(it.key());
                return check;
            }
            const QString role = it.value().toString();
            if (role.isEmpty()) {
                check.error = QStringLiteral("empty role on slot %1").arg(it.key());
                return check;
            }
            used.insert(role);
        }
    }
    check.layout = tactic.value(QLatin1String("layout")).toObject();
    // A layout has to name exactly the outfield slots of the base line-up.
    {
        QStringList layoutSlots;
        for (auto it = check.layout.begin(); it != check.layout.end(); ++it)
            layoutSlots << stringList(it.value());
        QStringList baseSlots = baseLineUp(check.roles).keys();
        baseSlots.removeAll(QStringLiteral("GK"));
        layoutSlots.sort();
        baseSlots.sort();
        if (layoutSlots != baseSlots)
            check.layout = layoutFor(definitions.fmVersionId(), baseLineUp(check.roles));
    }

    const QJsonObject root = definitions.root();
    const QJsonObject fileRoles = file.value(QLatin1String("roles")).toObject();
    QStringList sorted(used.begin(), used.end());
    sorted.sort();
    for (const QString &role : std::as_const(sorted)) {
        const QJsonObject def = fileRoles.value(role).toObject();
        ImportedRole imported;
        imported.id = role;
        imported.name = def.value(QLatin1String("name")).toString();
        imported.category = def.value(QLatin1String("category")).toString();
        imported.phase = def.value(QLatin1String("phase")).toString();
        imported.positions = stringList(def.value(QLatin1String("positions")));
        imported.positions.sort();
        imported.key = stringList(def.value(QLatin1String("key")));
        imported.preferable = stringList(def.value(QLatin1String("preferable")));
        if (definitions.roleDisplayMap().contains(role)) {
            if (!def.isEmpty() && !sameRole(imported, describeRole(root, role)))
                check.differingRoles << role;
            continue;
        }
        const bool complete = !imported.name.isEmpty() && !imported.category.isEmpty()
                              && !imported.positions.isEmpty() && !imported.key.isEmpty()
                              && (!definitions.hasRolePhases() || !imported.phase.isEmpty());
        if (complete)
            check.rolesToAdd << imported;
        else
            check.unknownRoles << role;
    }
    check.nameTaken = definitions.tacticNames().contains(check.name);
    check.status = check.unknownRoles.isEmpty() ? ImportCheck::Ok : ImportCheck::MissingRoles;
    return check;
}

void applyImport(QJsonObject &root, const QString &fmVersionId, const ImportCheck &check,
                 const QString &finalName)
{
    if (check.status != ImportCheck::Ok)
        return;
    QJsonObject categories = root.value(QLatin1String("player_roles")).toObject();
    QJsonObject weights = root.value(QLatin1String("role_specific_weights")).toObject();
    QJsonObject mapping = root.value(QLatin1String("position_to_role_mapping")).toObject();
    QJsonObject phases = root.value(QLatin1String("role_phases")).toObject();
    for (const ImportedRole &role : check.rolesToAdd) {
        QJsonObject category = categories.value(role.category).toObject();
        category.insert(role.id, role.name);
        categories.insert(role.category, category);
        weights.insert(role.id,
                       QJsonObject{{QStringLiteral("key"), QJsonArray::fromStringList(role.key)},
                                   {QStringLiteral("preferable"),
                                    QJsonArray::fromStringList(role.preferable)}});
        for (const QString &position : role.positions) {
            QStringList list = stringList(mapping.value(position));
            if (!list.contains(role.id)) {
                list << role.id;
                mapping.insert(position, QJsonArray::fromStringList(list));
            }
        }
        if (!role.phase.isEmpty())
            phases.insert(role.id, role.phase);
        listInsert(root, QStringLiteral("custom_roles"), role.id);
    }
    root.insert(QStringLiteral("player_roles"), categories);
    root.insert(QStringLiteral("role_specific_weights"), weights);
    root.insert(QStringLiteral("position_to_role_mapping"), mapping);
    if (!phases.isEmpty())
        root.insert(QStringLiteral("role_phases"), phases);
    putTactic(root, fmVersionId, finalName, check.roles, check.layout);
}

QString uniqueName(const QString &name, const QStringList &taken)
{
    if (!taken.contains(name))
        return name;
    for (int i = 2;; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(name).arg(i);
        if (!taken.contains(candidate))
            return candidate;
    }
}

} // namespace TacticLibrary

} // namespace fm
