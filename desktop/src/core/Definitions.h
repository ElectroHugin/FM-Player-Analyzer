#pragma once

#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>

namespace fm {

// Role key/preferable attribute lists (role_specific_weights entry).
struct RoleWeights {
    QStringList key;
    QStringList preferable;
};

// Loads, queries and saves config/definitions.json — the user-editable domain
// data: roles, role weights, position->role mapping, tactics, personalities.
// Port of legacy definitions_loader.py + definitions_handler.py + the dynamic
// accessors in constants.py.
//
// All derived lookup tables are built once whenever the JSON root changes
// (load()/setRoot()) and returned by reference: several of them sit on hot
// paths (per-player filters, per-cell table styling), where re-parsing the JSON
// on every call dominated the cost.
class Definitions
{
public:
    Definitions() { rebuildCache(); }

    // Loads from the given JSON file. Returns false and sets errorString() on
    // missing/invalid file.
    bool load(const QString &filePath);

    // Saves back to the file given to load(). Creates a .bak first, restores
    // it on failure (same semantics as legacy save_definitions).
    bool save();

    QString errorString() const { return m_error; }
    QString filePath() const { return m_filePath; }

    // --- Accessors (mirroring constants.py dynamic getters) ---

    // Category ("Goalkeepers", "Defense", ...) -> {abbr -> "Full Name (Duty)"}.
    const QHash<QString, QHash<QString, QString>> &playerRoles() const { return m_playerRoles; }

    // Fixed display order of role categories.
    static QStringList roleCategoryOrder();

    // All role abbreviations, sorted (get_valid_roles).
    const QStringList &validRoles() const { return m_validRoles; }

    // Goalkeeper role abbreviations (from the "Goalkeepers" category, with the
    // legacy fallback list).
    const QStringList &gkRoles() const { return m_gkRoles; }

    // Role abbr -> key/preferable attribute name lists.
    RoleWeights roleWeights(const QString &role) const { return m_roleWeights.value(role); }
    QStringList rolesWithWeights() const;

    // Game position ("D (C)") -> eligible role abbreviations.
    const QHash<QString, QStringList> &positionToRoleMapping() const { return m_positionToRoles; }

    // Tactic name -> {slot -> role abbr}.
    const QHash<QString, QHash<QString, QString>> &tacticRoles() const { return m_tacticRoles; }
    const QStringList &tacticNames() const { return m_tacticNames; }

    // JSON-file insertion order (QJsonObject sorts keys alphabetically, but
    // the legacy Python dicts preserve file order, and squad-selection
    // tie-breaks depend on it). Falls back to alphabetical if the raw file
    // is unavailable.
    QStringList tacticNamesOrdered() const;
    QStringList tacticSlotOrder(const QString &tactic) const;

    // Tactic name -> {stratum -> [slots]}.
    const QHash<QString, QHash<QString, QStringList>> &tacticLayouts() const
    {
        return m_tacticLayouts;
    }

    // Personality -> "good" | "neutral" | "bad" (definitions override or
    // built-in defaults).
    const QHash<QString, QString> &personalities() const { return m_personalities; }

    // Category for a personality string; case-/whitespace-insensitive
    // fallback; "" if unknown (get_personality_category).
    QString personalityCategory(const QString &name) const;

    // Role abbr -> full display name (across all categories).
    const QHash<QString, QString> &roleDisplayMap() const { return m_roleDisplayMap; }

    // Natural on-pitch role order (port of legacy get_natural_role_sorter):
    // GK roles first, then by stratum (Defense .. Strikers) and column L-R.
    // Returns (stratumScore, column) per role; unknown roles sort last.
    const QHash<QString, QPair<int, int>> &naturalRoleSorter() const { return m_naturalSorter; }

    // Sorts the given role abbreviations naturally (stable within equal keys).
    QStringList sortRolesNaturally(QStringList roles) const;

    // Raw access for editing (New Role / New Tactic pages).
    QJsonObject root() const { return m_root; }
    void setRoot(const QJsonObject &root)
    {
        m_root = root;
        rebuildCache();
    }

private:
    void indexTacticOrder(const QByteArray &json);
    // Re-derives every cached table below from m_root.
    void rebuildCache();
    QHash<QString, QPair<int, int>> buildNaturalRoleSorter() const;

    QJsonObject m_root;
    QString m_filePath;
    QString m_error;
    QStringList m_tacticOrder;                    // tactic names in file order
    QHash<QString, QStringList> m_slotOrder;      // tactic -> slots in file order

    // Derived from m_root by rebuildCache().
    QHash<QString, QHash<QString, QString>> m_playerRoles;
    QStringList m_validRoles;
    QStringList m_gkRoles;
    QHash<QString, RoleWeights> m_roleWeights;
    QHash<QString, QStringList> m_positionToRoles;
    QHash<QString, QHash<QString, QString>> m_tacticRoles;
    QStringList m_tacticNames;
    QHash<QString, QHash<QString, QStringList>> m_tacticLayouts;
    QHash<QString, QString> m_personalities;
    QHash<QString, QString> m_personalitiesLower; // lowercased name -> category
    QHash<QString, QString> m_roleDisplayMap;
    QHash<QString, QPair<int, int>> m_naturalSorter;
};

} // namespace fm
