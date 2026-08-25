#pragma once

#include "Player.h"

#include <QString>

#include <vector>

namespace fm {

class DwrsEngine;

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

Group groupOf(const QString &attrName);
QString groupName(Group group); // German label for the UI
double devFactor(Group group, int age, const AgeWindows &windows);

struct Recommendation {
    int attrIndex = -1;
    QString attrName;
    Group group = Group::Technical;
    int currentValue = 0;      // rounded attribute mean
    double dwrsPerPoint = 0.0; // marginal normalized DWRS per +1 for the role
    double devFactor = 0.0;    // 0..1 trainability at this age
    double priority = 0.0;     // dwrsPerPoint * devFactor * headroom
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

} // namespace TrainingAdvice

} // namespace fm
