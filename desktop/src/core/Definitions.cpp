#include "Definitions.h"

#include <QSet>

#include "Constants.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>

namespace fm {

bool Definitions::load(const QString &filePath, const QString &fmVersionId)
{
    m_error.clear();

    QFile file(filePath);
    if (!file.exists()) {
        m_error = QStringLiteral("Definitions file not found: %1").arg(filePath);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        m_error = QStringLiteral("Invalid JSON in %1: %2")
                      .arg(filePath, parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        m_error = QStringLiteral("Definitions root must be a JSON object: %1").arg(filePath);
        return false;
    }

    // Path and version only change on success: after a failed load, save()
    // must still write to the file the live definitions came from.
    m_filePath = filePath;
    m_fmVersionId = fmVersionId;
    m_root = doc.object();
    rebuildCache();

    file.seek(0);
    indexTacticOrder(file.readAll());
    return true;
}

void Definitions::indexTacticOrder(const QByteArray &json)
{
    // Minimal order-preserving scan of the "tactic_roles" object: records the
    // file order of tactic names and of each tactic's slot keys, which
    // QJsonObject discards (it sorts keys). Handles strings/escapes and
    // nesting; definitions.json is machine-written, so this stays simple.
    m_tacticOrder.clear();
    m_slotOrder.clear();

    const QString text = QString::fromUtf8(json);
    const int anchor = text.indexOf(QStringLiteral("\"tactic_roles\""));
    if (anchor < 0)
        return;
    int i = text.indexOf(QLatin1Char('{'), anchor);
    if (i < 0)
        return;

    int depth = 0;
    QString currentTactic;
    bool inString = false;
    QString stringValue;
    for (; i < text.size(); ++i) {
        const QChar c = text[i];
        if (inString) {
            if (c == QLatin1Char('\\') && i + 1 < text.size()) {
                stringValue.append(text[i + 1]);
                ++i;
            } else if (c == QLatin1Char('"')) {
                inString = false;
                // A string at depth 1/2 followed by ':' is a key.
                int j = i + 1;
                while (j < text.size() && text[j].isSpace())
                    ++j;
                const bool isKey = j < text.size() && text[j] == QLatin1Char(':');
                if (isKey && depth == 1) {
                    currentTactic = stringValue;
                    m_tacticOrder.append(stringValue);
                } else if (isKey && depth == 2) {
                    m_slotOrder[currentTactic].append(stringValue);
                }
            } else {
                stringValue.append(c);
            }
            continue;
        }
        if (c == QLatin1Char('"')) {
            inString = true;
            stringValue.clear();
        } else if (c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char('}')) {
            --depth;
            if (depth == 0)
                break; // end of tactic_roles object
        }
    }
}

QStringList Definitions::tacticNamesOrdered() const
{
    return m_tacticOrder.isEmpty() ? tacticNames() : m_tacticOrder;
}

QStringList Definitions::pitchOrder(QStringList slotKeys) const
{
    // GK first, then own goal -> attack, left -> right within a line.
    const auto &map = masterPositionMap(m_fmVersionId);
    const auto &strata = stratumOrder(m_fmVersionId);
    const auto key = [&](const QString &slot) {
        if (slot == QLatin1String("GK"))
            return std::make_pair(-1, 0);
        const SlotPosition pos = map.value(slot);
        return std::make_pair(strata.value(pos.stratum, 99), pos.column);
    };
    std::stable_sort(slotKeys.begin(), slotKeys.end(),
                     [&](const QString &a, const QString &b) { return key(a) < key(b); });
    return slotKeys;
}

bool Definitions::applyShippedUpdate(const QJsonObject &shipped, QJsonObject &user)
{
    const int shippedRevision = shipped.value(QLatin1String("data_revision")).toInt(0);
    if (shippedRevision <= user.value(QLatin1String("data_revision")).toInt(0))
        return false;

    QSet<QString> knownRoles;
    const QJsonObject categories = shipped.value(QLatin1String("player_roles")).toObject();
    for (auto cat = categories.begin(); cat != categories.end(); ++cat) {
        const QJsonObject roles = cat.value().toObject();
        for (auto role = roles.begin(); role != roles.end(); ++role)
            knownRoles.insert(role.key());
    }
    const auto rolesKnown = [&knownRoles](const QJsonValue &tactic) {
        // One line-up {slot: role} or two {"ip": {...}, "oop": {...}}.
        const QJsonObject obj = tactic.toObject();
        QList<QJsonObject> lineUps;
        if (obj.contains(QLatin1String("ip")) || obj.contains(QLatin1String("oop")))
            lineUps = {obj.value(QLatin1String("ip")).toObject(),
                       obj.value(QLatin1String("oop")).toObject()};
        else
            lineUps = {obj};
        for (const QJsonObject &lineUp : std::as_const(lineUps)) {
            if (lineUp.isEmpty())
                return false;
            for (auto it = lineUp.begin(); it != lineUp.end(); ++it) {
                if (!knownRoles.contains(it.value().toString()))
                    return false;
            }
        }
        return true;
    };

    QSet<QString> retired;
    for (const QJsonValue &name : shipped.value(QLatin1String("retired_tactics")).toArray())
        retired.insert(name.toString());

    QJsonObject merged = shipped;
    QJsonObject tactics = merged.value(QLatin1String("tactic_roles")).toObject();
    QJsonObject layouts = merged.value(QLatin1String("tactic_layouts")).toObject();
    const QJsonObject userTactics = user.value(QLatin1String("tactic_roles")).toObject();
    const QJsonObject userLayouts = user.value(QLatin1String("tactic_layouts")).toObject();
    for (auto it = userTactics.begin(); it != userTactics.end(); ++it) {
        if (tactics.contains(it.key()) || retired.contains(it.key()) || !rolesKnown(it.value()))
            continue;
        tactics.insert(it.key(), it.value());
        layouts.insert(it.key(), userLayouts.value(it.key()));
    }
    merged.insert(QStringLiteral("tactic_roles"), tactics);
    merged.insert(QStringLiteral("tactic_layouts"), layouts);
    user = merged;
    return true;
}

QString Definitions::rolePhase(const QString &role) const
{
    const QString phase = m_rolePhases.value(role);
    return phase.isEmpty() ? QStringLiteral("both") : phase;
}

QString Definitions::baseRoleAbbr(const QString &role)
{
    if (role.endsWith(QLatin1String("-IP")))
        return role.left(role.size() - 3);
    if (role.endsWith(QLatin1String("-OOP")))
        return role.left(role.size() - 4);
    return role;
}

QString Definitions::roleLabel(const QString &role) const
{
    const QString phase = m_rolePhases.value(role);
    if (phase == QLatin1String("ip"))
        return baseRoleAbbr(role) + QStringLiteral("-IP");
    if (phase == QLatin1String("oop"))
        return baseRoleAbbr(role) + QStringLiteral("-OOP");
    return role;
}

QStringList Definitions::tacticRoleList(const QString &tactic) const
{
    QStringList roles;
    const auto twoPhase = m_twoPhaseTactics.constFind(tactic);
    if (twoPhase != m_twoPhaseTactics.constEnd()) {
        // Both line-ups: the roles with ball first, then those without.
        QStringList ip, oop;
        for (const QString &role : std::as_const(twoPhase->ip))
            ip.append(role);
        for (const QString &role : std::as_const(twoPhase->oop))
            oop.append(role);
        ip.removeDuplicates();
        oop.removeDuplicates();
        roles = sortRolesNaturally(ip) + sortRolesNaturally(oop);
        roles.removeDuplicates();
        return roles;
    }
    for (const QString &role : m_tacticRoles.value(tactic))
        roles.append(role);
    roles.removeDuplicates();
    return sortRolesNaturally(roles);
}

QStringList Definitions::sortRolesByPhase(QStringList roles) const
{
    roles = sortRolesNaturally(roles);
    if (!hasRolePhases())
        return roles;
    std::stable_sort(roles.begin(), roles.end(), [this](const QString &a, const QString &b) {
        const auto rank = [this](const QString &r) {
            const QString phase = m_rolePhases.value(r);
            return phase == QLatin1String("ip") ? 0 : phase == QLatin1String("oop") ? 1 : 2;
        };
        return rank(a) < rank(b);
    });
    return roles;
}

QStringList Definitions::tacticSlotOrder(const QString &tactic) const
{
    const auto twoPhase = m_twoPhaseTactics.constFind(tactic);
    if (twoPhase != m_twoPhaseTactics.constEnd())
        return twoPhase->oopOrder; // file order holds "ip"/"oop", not slots
    const QStringList order = m_slotOrder.value(tactic);
    if (!order.isEmpty())
        return order;
    return m_root.value(QLatin1String("tactic_roles")).toObject().value(tactic).toObject().keys();
}

bool Definitions::save()
{
    m_error.clear();
    if (m_filePath.isEmpty()) {
        m_error = QStringLiteral("No file path set; call load() first.");
        return false;
    }

    const QString backupPath = m_filePath + QStringLiteral(".bak");

    // 1. Back up the current file (if it exists).
    QFile::remove(backupPath);
    const bool hadOriginal = QFile::exists(m_filePath);
    if (hadOriginal && !QFile::copy(m_filePath, backupPath)) {
        m_error = QStringLiteral("Could not create backup %1").arg(backupPath);
        return false;
    }

    // 2. Write the new data.
    QFile file(m_filePath);
    const QByteArray json = QJsonDocument(m_root).toJson(QJsonDocument::Indented);
    const bool written = file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                         && file.write(json) == json.size();
    file.close();

    if (!written) {
        // 3. Restore from backup on failure.
        if (hadOriginal) {
            QFile::remove(m_filePath);
            QFile::copy(backupPath, m_filePath);
        }
        m_error = QStringLiteral("Write failed for %1: %2. Restored from backup.")
                      .arg(m_filePath, file.errorString());
        return false;
    }

    // 4. Success: drop the backup.
    QFile::remove(backupPath);
    return true;
}

QStringList Definitions::roleCategoryOrder()
{
    return {QStringLiteral("Goalkeepers"), QStringLiteral("Defense"),
            QStringLiteral("Midfield"), QStringLiteral("Attack")};
}

void Definitions::rebuildCache()
{
    // Roles: category -> {abbr -> display name}, the flat display map and the
    // sorted list of valid abbreviations.
    m_playerRoles.clear();
    m_roleDisplayMap.clear();
    m_validRoles.clear();
    const QJsonObject categories = m_root.value(QLatin1String("player_roles")).toObject();
    for (auto catIt = categories.begin(); catIt != categories.end(); ++catIt) {
        QHash<QString, QString> roles;
        const QJsonObject roleObj = catIt.value().toObject();
        for (auto roleIt = roleObj.begin(); roleIt != roleObj.end(); ++roleIt) {
            const QString displayName = roleIt.value().toString();
            roles.insert(roleIt.key(), displayName);
            m_roleDisplayMap.insert(roleIt.key(), displayName);
            m_validRoles.append(roleIt.key());
        }
        m_playerRoles.insert(catIt.key(), roles);
    }
    std::sort(m_validRoles.begin(), m_validRoles.end());

    m_gkRoles.clear();
    const QJsonObject gk = categories.value(QLatin1String("Goalkeepers")).toObject();
    for (auto it = gk.begin(); it != gk.end(); ++it)
        m_gkRoles.append(it.key());
    if (m_gkRoles.isEmpty())
        m_gkRoles = {QStringLiteral("GK-D"), QStringLiteral("SK-D"), QStringLiteral("SK-S"),
                     QStringLiteral("SK-A")};

    m_roleWeights.clear();
    const QJsonObject weights = m_root.value(QLatin1String("role_specific_weights")).toObject();
    for (auto it = weights.begin(); it != weights.end(); ++it) {
        RoleWeights entry;
        const QJsonObject obj = it.value().toObject();
        const QJsonArray key = obj.value(QLatin1String("key")).toArray();
        for (const QJsonValue &v : key)
            entry.key.append(v.toString());
        const QJsonArray preferable = obj.value(QLatin1String("preferable")).toArray();
        for (const QJsonValue &v : preferable)
            entry.preferable.append(v.toString());
        m_roleWeights.insert(it.key(), entry);
    }

    m_positionToRoles.clear();
    const QJsonObject mapping = m_root.value(QLatin1String("position_to_role_mapping")).toObject();
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        QStringList roles;
        const QJsonArray arr = it.value().toArray();
        for (const QJsonValue &v : arr)
            roles.append(v.toString());
        m_positionToRoles.insert(it.key(), roles);
    }

    m_rolePhases.clear();
    const QJsonObject phases = m_root.value(QLatin1String("role_phases")).toObject();
    for (auto it = phases.begin(); it != phases.end(); ++it)
        m_rolePhases.insert(it.key(), it.value().toString());
    // One marking for every phase-split role (FM27): the full name always ends
    // in " (IP)" or " (OOP)", so "Central Midfielder" with and without ball
    // can be told apart everywhere (FM's own terms, like the role names).
    for (auto it = m_roleDisplayMap.begin(); it != m_roleDisplayMap.end(); ++it) {
        const QString phase = m_rolePhases.value(it.key());
        if (phase == QLatin1String("ip") || phase == QLatin1String("oop"))
            it.value() += phase == QLatin1String("ip") ? QStringLiteral(" (IP)")
                                                       : QStringLiteral(" (OOP)");
    }

    m_tacticRoles.clear();
    m_twoPhaseTactics.clear();
    const auto readSlots = [](const QJsonObject &obj) {
        QHash<QString, QString> slotMap;
        for (auto slotIt = obj.begin(); slotIt != obj.end(); ++slotIt)
            slotMap.insert(slotIt.key(), slotIt.value().toString());
        return slotMap;
    };
    const QJsonObject tactics = m_root.value(QLatin1String("tactic_roles")).toObject();
    for (auto tacticIt = tactics.begin(); tacticIt != tactics.end(); ++tacticIt) {
        const QJsonObject slotObj = tacticIt.value().toObject();
        if (slotObj.value(QLatin1String("ip")).isObject()
            && slotObj.value(QLatin1String("oop")).isObject()) {
            TwoPhaseTactic tactic;
            tactic.ip = readSlots(slotObj.value(QLatin1String("ip")).toObject());
            tactic.oop = readSlots(slotObj.value(QLatin1String("oop")).toObject());
            tactic.ipOrder = pitchOrder(tactic.ip.keys());
            tactic.oopOrder = pitchOrder(tactic.oop.keys());
            m_tacticRoles.insert(tacticIt.key(), tactic.oop);
            m_twoPhaseTactics.insert(tacticIt.key(), tactic);
            continue;
        }
        m_tacticRoles.insert(tacticIt.key(), readSlots(slotObj));
    }
    m_tacticNames = tactics.keys();

    m_tacticLayouts.clear();
    const QJsonObject layouts = m_root.value(QLatin1String("tactic_layouts")).toObject();
    for (auto tacticIt = layouts.begin(); tacticIt != layouts.end(); ++tacticIt) {
        QHash<QString, QStringList> strata;
        const QJsonObject stratumObj = tacticIt.value().toObject();
        for (auto stratumIt = stratumObj.begin(); stratumIt != stratumObj.end(); ++stratumIt) {
            QStringList slotList;
            const QJsonArray arr = stratumIt.value().toArray();
            for (const QJsonValue &v : arr)
                slotList.append(v.toString());
            strata.insert(stratumIt.key(), slotList);
        }
        m_tacticLayouts.insert(tacticIt.key(), strata);
    }

    if (m_root.contains(QLatin1String("personalities"))) {
        m_personalities.clear();
        const QJsonObject obj = m_root.value(QLatin1String("personalities")).toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            m_personalities.insert(it.key(), it.value().toString());
    } else {
        m_personalities = personalityDefaults();
    }
    // Case-insensitive fallback for personalityCategory(), replacing the former
    // linear scan over the table on every call.
    m_personalitiesLower.clear();
    for (auto it = m_personalities.constBegin(); it != m_personalities.constEnd(); ++it) {
        const QString lower = it.key().toLower();
        if (!m_personalitiesLower.contains(lower))
            m_personalitiesLower.insert(lower, it.value());
    }

    // Last: depends on the position mapping and the valid roles above.
    m_naturalSorter = buildNaturalRoleSorter();
}

QStringList Definitions::rolesWithWeights() const
{
    return m_root.value(QLatin1String("role_specific_weights")).toObject().keys();
}

QString Definitions::personalityCategory(const QString &name) const
{
    if (name.isEmpty())
        return QString();
    const auto exact = m_personalities.constFind(name);
    if (exact != m_personalities.constEnd())
        return exact.value();
    return m_personalitiesLower.value(name.trimmed().toLower());
}

QHash<QString, QPair<int, int>> Definitions::buildNaturalRoleSorter() const
{
    // role -> game positions it can play (reverse of position_to_role_mapping).
    QHash<QString, QStringList> roleToPositions;
    for (auto it = m_positionToRoles.constBegin(); it != m_positionToRoles.constEnd(); ++it) {
        for (const QString &role : it.value())
            roleToPositions[role].append(it.key());
    }

    QHash<QString, QPair<int, int>> sorter;
    const auto &slotMap = masterPositionMap(m_fmVersionId);
    const auto &slotPositions = tacticalSlotToGamePositions(m_fmVersionId);
    const auto &strata = stratumOrder(m_fmVersionId);
    const QSet<QString> gkRoles(m_gkRoles.cbegin(), m_gkRoles.cend());

    for (const QString &role : m_validRoles) {
        if (gkRoles.contains(role)) {
            sorter.insert(role, {0, 0});
            continue;
        }
        const QStringList positions = roleToPositions.value(role);
        if (positions.isEmpty()) {
            sorter.insert(role, {99, 99});
            continue;
        }
        // Highest (most attacking) slot the role can occupy; ties break left.
        QPair<int, int> best(-1, -1);
        for (auto slotIt = slotMap.constBegin(); slotIt != slotMap.constEnd(); ++slotIt) {
            const QStringList validGamePositions = slotPositions.value(slotIt.key());
            bool matches = false;
            for (const QString &position : positions) {
                if (validGamePositions.contains(position)) {
                    matches = true;
                    break;
                }
            }
            if (!matches)
                continue;
            const int stratumScore = strata.value(slotIt.value().stratum, 99);
            const QPair<int, int> current(stratumScore, slotIt.value().column);
            if (current.first > best.first
                || (current.first == best.first && current.second < best.second)) {
                best = current;
            }
        }
        sorter.insert(role, best.first < 0 ? QPair<int, int>(99, 99) : best);
    }
    return sorter;
}

QStringList Definitions::sortRolesNaturally(QStringList roles) const
{
    const auto &sorter = m_naturalSorter;
    std::stable_sort(roles.begin(), roles.end(), [&sorter](const QString &a, const QString &b) {
        return sorter.value(a, {99, 99}) < sorter.value(b, {99, 99});
    });
    return roles;
}

} // namespace fm
