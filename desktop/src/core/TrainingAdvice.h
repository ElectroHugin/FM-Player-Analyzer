#pragma once

#include "Player.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <vector>

namespace fm {

class DwrsEngine;
class RatingSnapshot;

// Training advisor. Frames "what should this player train?" as a DWRS
// optimization: among the attributes that (a) actually feed the chosen role's
// DWRS, (b) are still trainable at the player's age, and (c) have headroom left,
// which give the biggest rating gain. The marginal DWRS value of each attribute
// comes straight from DwrsEngine, so advice always matches the app's own rating.
namespace TrainingAdvice {

// Development groups with distinct age windows (community FM lore, tunable).
enum class Group {
    Explosive,  // Pace, Acceleration, Agility — the youth spike, hard ceiling early
    Strength,   // Strength, Stamina, Jumping Reach, Balance — matures a bit later
    Technical,  // Finishing, Passing, First Touch, … (incl. GK technicals)
    Mental,     // Anticipation, Decisions, Vision, … — never stops developing
    Character   // Work Rate, Determination — only via mentoring, not focus training
};

// Age windows (from AppConfig [Training]). devFactor = 1 up to peak, linear to 0
// at locked; mental is always 1, character always 0.
struct AgeWindows {
    int explosivePeak = 21, explosiveLocked = 24;
    int strengthPeak = 23, strengthLocked = 25;
    int technicalPeak = 21, technicalLocked = 27;
};

// --- Training profile (per save, Settings -> Training) -----------------------
// Bands of attributes that develop alike: an age window, a weight and the
// attributes in it. The standard profile is the five groups above with the
// configured age windows. An attribute in no band is never advised.
inline constexpr int kLifelongAge = 50; // peak age from here on: trainable lifelong

struct TrainingBand {
    QString name;
    int peakAge = 21;    // fully trainable up to this age
    int lockedAge = 27;  // no longer trainable from this age (linear between)
    double weight = 1.0; // multiplies the band's priority
    bool mentoringOnly = false; // only via mentoring (character)
    QStringList attributes;

    bool lifelong() const { return !mentoringOnly && peakAge >= kLifelongAge; }
    double devFactor(int age) const;
    bool operator==(const TrainingBand &other) const = default;
};

struct TrainingProfile {
    QList<TrainingBand> bands;

    // Index of the band holding the attribute; -1 = in no band.
    int bandOf(const QString &attrName) const;
    QJsonObject toJson() const;
    // Validates names, ages, weights and attributes (each in one band at
    // most, unknown names dropped). error in English for logs.
    static bool fromJson(const QJsonObject &json, TrainingProfile *out, QString *error = nullptr);
    bool operator==(const TrainingProfile &other) const = default;
};

struct AgeWindows;
TrainingProfile standardProfile(const AgeWindows &windows);

Group groupOf(const QString &attrName);
QString groupName(Group group); // German label for the UI
double devFactor(Group group, int age, const AgeWindows &windows);

struct Recommendation {
    int attrIndex = -1;
    QString attrName;
    Group group = Group::Technical;
    QString band;          // band name in the training profile
    bool lifelong = false; // band trainable at any age
    int currentValue = 0;      // rounded attribute mean
    double dwrsPerPoint = 0.0; // marginal normalized DWRS per +1 for the role
    double devFactor = 0.0;    // 0..1 trainability at this age
    double priority = 0.0;     // Classic: marginal * age * headroom; curves: expected +1 gain * age
    bool mentoringOnly = false;
};

struct Advice {
    std::vector<Recommendation> focus;     // trainable, priority > 0, best first
    std::vector<Recommendation> mentoring; // character attrs worth improving (note)
    bool formLimited = false;              // average rating < 7.0 -> develops slowly
    bool valid = false;                    // false when the role has no ratings/attrs
};

// role must be a valid DWRS role for the player. windows come from AppConfig.
Advice adviseForRole(const Player &player, const QString &role,
                     const DwrsEngine &engine, const AgeWindows &windows);

// Nonlinear profiles use the expected +1 gain at the current attribute range.
Advice adviseForRole(const Player &player, const QString &role,
                     const RatingSnapshot &snapshot, const AgeWindows &windows);
// The same with a full training profile (what the app uses).
Advice adviseForRole(const Player &player, const QString &role, const DwrsEngine &engine,
                     const TrainingProfile &profile);
Advice adviseForRole(const Player &player, const QString &role, const RatingSnapshot &snapshot,
                     const TrainingProfile &profile);

// --- Training focuses (the in-game training methods) --------------------------
// FM does not train single attributes; the player picks a training focus that
// maps to one or more attributes. The advisor therefore recommends focuses, and
// picking the best is an optimization: a focus that trains three role-relevant
// attributes at once can beat a single-attribute one. Injury-Rehab focuses are
// never recommended (they only apply to injured players).

struct TrainingFocus {
    QString name;
    QString category;       // "Injury Rehab" | "Set Pieces" | "Attributes"
    QStringList attributes; // attributes this focus trains
    bool rehab = false;     // Injury-Rehab category -> excluded from advice
};

// The fixed focus->attributes table (see the mapping list). Placed here beside
// the attribute-group table, mirroring the other hardcoded game-fact mappings.
const std::vector<TrainingFocus> &trainingFocuses();

struct FocusRecommendation {
    QString focus;
    QString category;
    QStringList attributes; // contributing attributes, biggest DWRS lever first
    double priority = 0.0;  // summed DWRS-gain proxy over its attributes
    bool allMental = false; // every contributor is mental (trainable lifelong)
    bool anyLimited = false; // some contributor only partly trainable at this age
};

struct FocusAdvice {
    std::vector<FocusRecommendation> focus; // ranked desc, priority > 0
    bool valid = false;
    bool formLimited = false;
};

// Ranks the training focuses by how much they would raise the player's DWRS in
// the given role, honoring age windows and headroom, excluding injury rehab.
FocusAdvice adviseFocusesForRole(const Player &player, const QString &role,
                                 const DwrsEngine &engine, const AgeWindows &windows);

FocusAdvice adviseFocusesForRole(const Player &player, const QString &role,
                                 const RatingSnapshot &snapshot, const AgeWindows &windows);
FocusAdvice adviseFocusesForRole(const Player &player, const QString &role,
                                 const DwrsEngine &engine, const TrainingProfile &profile);
FocusAdvice adviseFocusesForRole(const Player &player, const QString &role,
                                 const RatingSnapshot &snapshot, const TrainingProfile &profile);

} // namespace TrainingAdvice

} // namespace fm
