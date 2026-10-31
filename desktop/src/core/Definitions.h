#pragma once

#include "Constants.h"

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

    // Loads from the given JSON file, which holds the roles/tactics of the
    // given FM version. Returns false and sets errorString() on missing/invalid
    // file; the previously loaded definitions (path and version included) stay
    // intact in that case.
    bool load(const QString &filePath, const QString &fmVersionId = defaultFmVersionId());

    // Saves back to the file given to load(). Creates a .bak first, restores
    // it on failure (same semantics as legacy save_definitions).
    bool save();

    QString errorString() const { return m_error; }
    QString filePath() const { return m_filePath; }
    // FM version these definitions belong to; selects the matching attribute
    // importance tiers and weights (globalStatCategories() & co.).
    QString fmVersionId() const { return m_fmVersionId; }

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

    // Tactic name -> {slot -> role abbr}. For a two-phase tactic (FM26+, see
    // twoPhaseTactics()) this is its out-of-possession line-up, the base shape.
    const QHash<QString, QHash<QString, QString>> &tacticRoles() const { return m_tacticRoles; }

    // FM26+ tactics: two independent line-ups of slot -> role, one in
    // possession ("ip") and one out of possession ("oop"), with no fixed link
    // between their slots; SquadBuilder::calculateTwoPhaseSquad pairs them.
    // JSON: "tactic_roles": {name: {"ip": {slot: role}, "oop": {slot: role}}}.
    struct TwoPhaseTactic {
        QHash<QString, QString> ip;
        QHash<QString, QString> oop;
        QStringList ipOrder;  // slots in pitch order (GK first, then L -> R per line)
        QStringList oopOrder;
    };
    const QHash<QString, TwoPhaseTactic> &twoPhaseTactics() const { return m_twoPhaseTactics; }
    bool isTwoPhaseTactic(const QString &tactic) const { return m_twoPhaseTactics.contains(tactic); }

    // Phase of a role: "ip", "oop" ("role_phases" in the JSON) or "both"
    // (FM24 roles, unknown roles).
    QString rolePhase(const QString &role) const;
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
    QStringList pitchOrder(QStringList slotKeys) const;
    // Re-derives every cached table below from m_root.
    void rebuildCache();
    QHash<QString, QPair<int, int>> buildNaturalRoleSorter() const;

    QJsonObject m_root;
    QString m_filePath;
    QString m_fmVersionId = defaultFmVersionId();
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
    QHash<QString, TwoPhaseTactic> m_twoPhaseTactics;
    QHash<QString, QString> m_rolePhases;
    QStringList m_tacticNames;
    QHash<QString, QHash<QString, QStringList>> m_tacticLayouts;
    QHash<QString, QString> m_personalities;
    QHash<QString, QString> m_personalitiesLower; // lowercased name -> category
    QHash<QString, QString> m_roleDisplayMap;
    QHash<QString, QPair<int, int>> m_naturalSorter;
};

} // namespace fm
