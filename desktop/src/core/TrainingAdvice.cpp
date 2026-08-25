#include "TrainingAdvice.h"

#include "Attributes.h"
#include "DwrsEngine.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace fm {

namespace TrainingAdvice {

namespace {

// Attribute -> development group. Covers every attribute that can feed a DWRS
// rating (outfield + GK); anything unlisted defaults to Technical.
const QHash<QString, Group> &groupTable()
{
    static const QHash<QString, Group> table = {
        // Explosive physical.
        {QStringLiteral("Pace"), Group::Explosive},
        {QStringLiteral("Acceleration"), Group::Explosive},
        {QStringLiteral("Agility"), Group::Explosive},
        // Strength physical.
        {QStringLiteral("Strength"), Group::Strength},
        {QStringLiteral("Stamina"), Group::Strength},
        {QStringLiteral("Jumping Reach"), Group::Strength},
        {QStringLiteral("Balance"), Group::Strength},
        {QStringLiteral("Natural Fitness"), Group::Strength},
        // Technical (outfield).
        {QStringLiteral("Finishing"), Group::Technical},
        {QStringLiteral("Dribbling"), Group::Technical},
        {QStringLiteral("Passing"), Group::Technical},
        {QStringLiteral("Long Shots"), Group::Technical},
        {QStringLiteral("Marking"), Group::Technical},
        {QStringLiteral("First Touch"), Group::Technical},
        {QStringLiteral("Tackling"), Group::Technical},
        {QStringLiteral("Technique"), Group::Technical},
        {QStringLiteral("Crossing"), Group::Technical},
        {QStringLiteral("Heading"), Group::Technical},
        {QStringLiteral("Corners"), Group::Technical},
        // Technical (GK).
        {QStringLiteral("Aerial Reach"), Group::Technical},
        {QStringLiteral("Command of Area"), Group::Technical},
        {QStringLiteral("Handling"), Group::Technical},
        {QStringLiteral("Kicking"), Group::Technical},
        {QStringLiteral("One vs One"), Group::Technical},
        {QStringLiteral("Reflexes"), Group::Technical},
        {QStringLiteral("Throwing"), Group::Technical},
        // Mental.
        {QStringLiteral("Anticipation"), Group::Mental},
        {QStringLiteral("Concentration"), Group::Mental},
        {QStringLiteral("Vision"), Group::Mental},
        {QStringLiteral("Decisions"), Group::Mental},
        {QStringLiteral("Off the Ball"), Group::Mental},
        {QStringLiteral("Teamwork"), Group::Mental},
        {QStringLiteral("Composure"), Group::Mental},
        {QStringLiteral("Positioning"), Group::Mental},
        {QStringLiteral("Flair"), Group::Mental},
        // Character (mentoring only).
        {QStringLiteral("Work Rate"), Group::Character},
        {QStringLiteral("Determination"), Group::Character},
        {QStringLiteral("Bravery"), Group::Character},
        {QStringLiteral("Aggression"), Group::Character},
        {QStringLiteral("Leadership"), Group::Character},
    };
    return table;
}

} // namespace

Group groupOf(const QString &attrName)
{
    return groupTable().value(attrName, Group::Technical);
}

QString groupName(Group group)
{
    switch (group) {
    case Group::Explosive:
        return QStringLiteral("Explosivität");
    case Group::Strength:
        return QStringLiteral("Kraft/Physis");
    case Group::Technical:
        return QStringLiteral("Technik");
    case Group::Mental:
        return QStringLiteral("Mental");
    case Group::Character:
        return QStringLiteral("Charakter");
    }
    return QString();
}

double devFactor(Group group, int age, const AgeWindows &windows)
{
    const auto ramp = [age](int peak, int locked) -> double {
        if (locked <= peak)
            return age <= peak ? 1.0 : 0.0;
        if (age <= peak)
            return 1.0;
        if (age >= locked)
            return 0.0;
        return static_cast<double>(locked - age) / static_cast<double>(locked - peak);
    };
    switch (group) {
    case Group::Explosive:
        return ramp(windows.explosivePeak, windows.explosiveLocked);
    case Group::Strength:
        return ramp(windows.strengthPeak, windows.strengthLocked);
    case Group::Technical:
        return ramp(windows.technicalPeak, windows.technicalLocked);
    case Group::Mental:
        return 1.0; // never stops developing
    case Group::Character:
        return 0.0; // only via mentoring
    }
    return 0.0;
}

Advice adviseForRole(const Player &player, const QString &role, const DwrsEngine &engine,
                     const AgeWindows &windows)
{
    Advice advice;
    const QHash<int, double> perPoint = engine.attributeDwrsPerPoint(role);
    if (perPoint.isEmpty())
        return advice; // role has no ratable attributes (unknown role)
    advice.valid = true;

    constexpr double kAttrCap = 20.0;
    for (auto it = perPoint.constBegin(); it != perPoint.constEnd(); ++it) {
        const int attrIdx = it.key();
        const double value = player.attrMean(attrIdx);
        if (value <= 0.0)
            continue; // unscouted / missing — cannot advise

        const QString name = attrNames()[attrIdx];
        const Group group = groupOf(name);
        const double dev = devFactor(group, player.age, windows);
        const double headroom = std::max(0.0, kAttrCap - value);

        Recommendation rec;
        rec.attrIndex = attrIdx;
        rec.attrName = name;
        rec.group = group;
        rec.currentValue = static_cast<int>(std::lround(value));
        rec.dwrsPerPoint = it.value();
        rec.devFactor = dev;
        rec.mentoringOnly = group == Group::Character;
        rec.priority = it.value() * dev * headroom;

        if (rec.mentoringOnly) {
            // Character attrs never train via focus, but a weak one that matters
            // for the role is worth flagging for mentoring.
            if (headroom > 0.0 && it.value() > 0.0)
                advice.mentoring.push_back(rec);
        } else if (rec.priority > 0.0) {
            advice.focus.push_back(rec);
        }
    }

    const auto byPriority = [](const Recommendation &a, const Recommendation &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        if (a.dwrsPerPoint != b.dwrsPerPoint)
            return a.dwrsPerPoint > b.dwrsPerPoint;
        return a.currentValue < b.currentValue; // weaker first on a tie
    };
    std::sort(advice.focus.begin(), advice.focus.end(), byPriority);
    std::sort(advice.mentoring.begin(), advice.mentoring.end(),
              [](const Recommendation &a, const Recommendation &b) {
                  return a.dwrsPerPoint > b.dwrsPerPoint;
              });

    advice.formLimited = player.averageRating > 0.0 && player.averageRating < 7.0;
    return advice;
}

const std::vector<TrainingFocus> &trainingFocuses()
{
    // Attribute names normalized to the app's attribute set; names that do not
    // exist here (e.g. "Free Kick Taking", "Communication") simply never match
    // and contribute nothing — which is fine, they barely feed the DWRS anyway.
    static const std::vector<TrainingFocus> focuses = {
        // --- Injury Rehab (never recommended) ---
        {QStringLiteral("Quickness"), QStringLiteral("Injury Rehab"),
         {QStringLiteral("Pace"), QStringLiteral("Acceleration")}, true},
        {QStringLiteral("Agility and Balance"), QStringLiteral("Injury Rehab"),
         {QStringLiteral("Agility"), QStringLiteral("Balance")}, true},
        {QStringLiteral("Strength"), QStringLiteral("Injury Rehab"),
         {QStringLiteral("Strength"), QStringLiteral("Jumping Reach")}, true},
        {QStringLiteral("Endurance"), QStringLiteral("Injury Rehab"),
         {QStringLiteral("Stamina")}, true},
        {QStringLiteral("General Rehab"), QStringLiteral("Injury Rehab"),
         {QStringLiteral("Acceleration"), QStringLiteral("Agility"), QStringLiteral("Balance"),
          QStringLiteral("Jumping Reach"), QStringLiteral("Pace"), QStringLiteral("Strength"),
          QStringLiteral("Stamina")},
         true},
        // --- Set Pieces ---
        {QStringLiteral("Free Kick Taking"), QStringLiteral("Set Pieces"),
         {QStringLiteral("Free Kick Taking"), QStringLiteral("Technique")}, false},
        {QStringLiteral("Corner Taking"), QStringLiteral("Set Pieces"),
         {QStringLiteral("Corner Taking"), QStringLiteral("Technique")}, false},
        {QStringLiteral("Penalty Taking"), QStringLiteral("Set Pieces"),
         {QStringLiteral("Penalty Taking"), QStringLiteral("Technique")}, false},
        {QStringLiteral("Long Throws"), QStringLiteral("Set Pieces"),
         {QStringLiteral("Long Throws")}, false},
        // --- Attributes (main category for recommendations) ---
        {QStringLiteral("Quickness"), QStringLiteral("Attributes"),
         {QStringLiteral("Pace"), QStringLiteral("Acceleration")}, false},
        {QStringLiteral("Agility and Balance"), QStringLiteral("Attributes"),
         {QStringLiteral("Agility"), QStringLiteral("Balance")}, false},
        {QStringLiteral("Strength"), QStringLiteral("Attributes"),
         {QStringLiteral("Strength"), QStringLiteral("Jumping Reach")}, false},
        {QStringLiteral("Endurance"), QStringLiteral("Attributes"),
         {QStringLiteral("Stamina"), QStringLiteral("Work Rate")}, false},
        {QStringLiteral("Defensive Positioning"), QStringLiteral("Attributes"),
         {QStringLiteral("Marking"), QStringLiteral("Positioning"), QStringLiteral("Decisions")},
         false},
        {QStringLiteral("Attacking Movement"), QStringLiteral("Attributes"),
         {QStringLiteral("Off the Ball"), QStringLiteral("Anticipation"),
          QStringLiteral("Decisions")},
         false},
        {QStringLiteral("Final Third"), QStringLiteral("Attributes"),
         {QStringLiteral("Composure"), QStringLiteral("Decisions")}, false},
        {QStringLiteral("Shooting"), QStringLiteral("Attributes"),
         {QStringLiteral("Finishing"), QStringLiteral("Long Shots"), QStringLiteral("Technique")},
         false},
        {QStringLiteral("Passing"), QStringLiteral("Attributes"),
         {QStringLiteral("Vision"), QStringLiteral("Passing"), QStringLiteral("Technique")}, false},
        {QStringLiteral("Crossing"), QStringLiteral("Attributes"),
         {QStringLiteral("Crossing"), QStringLiteral("Technique")}, false},
        {QStringLiteral("Ball Control"), QStringLiteral("Attributes"),
         {QStringLiteral("First Touch"), QStringLiteral("Dribbling"), QStringLiteral("Technique")},
         false},
        {QStringLiteral("Aerial"), QStringLiteral("Attributes"),
         {QStringLiteral("Heading"), QStringLiteral("Bravery")}, false},
        {QStringLiteral("GK Reactions"), QStringLiteral("Attributes"),
         {QStringLiteral("Reflexes"), QStringLiteral("Anticipation"),
          QStringLiteral("Concentration")},
         false},
        {QStringLiteral("GK Tactical"), QStringLiteral("Attributes"),
         {QStringLiteral("Positioning"), QStringLiteral("Communication"),
          QStringLiteral("Decisions")},
         false},
        {QStringLiteral("GK Technique"), QStringLiteral("Attributes"),
         {QStringLiteral("Handling"), QStringLiteral("Composure"), QStringLiteral("Technique")},
         false},
        {QStringLiteral("GK Sweeping"), QStringLiteral("Attributes"),
         {QStringLiteral("One vs One"), QStringLiteral("Command of Area"),
          QStringLiteral("Rushing Out (Tendency)")},
         false},
        {QStringLiteral("GK Distribution (Long)"), QStringLiteral("Attributes"),
         {QStringLiteral("Kicking"), QStringLiteral("Throwing")}, false},
        {QStringLiteral("GK Distribution (Short)"), QStringLiteral("Attributes"),
         {QStringLiteral("First Touch"), QStringLiteral("Passing"), QStringLiteral("Vision")},
         false},
    };
    return focuses;
}

FocusAdvice adviseFocusesForRole(const Player &player, const QString &role,
                                 const DwrsEngine &engine, const AgeWindows &windows)
{
    FocusAdvice out;
    // Reuse the per-attribute optimization: each trainable, role-relevant
    // attribute already carries its DWRS-gain priority. A focus is then just the
    // sum of the priorities of the attributes it trains.
    const Advice attrAdvice = adviseForRole(player, role, engine, windows);
    if (!attrAdvice.valid)
        return out;
    out.valid = true;
    out.formLimited = attrAdvice.formLimited;

    QHash<QString, const Recommendation *> byName;
    for (const Recommendation &r : attrAdvice.focus)
        byName.insert(r.attrName, &r);

    // Detect a goalkeeper role from its rating attributes: GK roles rate the
    // GK-only attributes, outfield roles never do. GK training methods (name
    // starts with "GK") apply only to GK roles and vice versa — without this,
    // GK methods leak onto outfielders through shared mental attributes
    // (e.g. GK Reactions -> Anticipation/Concentration).
    static const QSet<QString> kGkMarkers = {
        QStringLiteral("Aerial Reach"), QStringLiteral("Reflexes"),
        QStringLiteral("Command of Area"), QStringLiteral("Handling"),
        QStringLiteral("One vs One")};
    bool isGkRole = false;
    {
        const QHash<int, double> perPoint = engine.attributeDwrsPerPoint(role);
        for (auto it = perPoint.constBegin(); it != perPoint.constEnd() && !isGkRole; ++it) {
            if (kGkMarkers.contains(attrNames()[it.key()]))
                isGkRole = true;
        }
    }

    for (const TrainingFocus &f : trainingFocuses()) {
        if (f.rehab)
            continue; // rehab focuses are never proposed
        const bool gkFocus = f.name.startsWith(QStringLiteral("GK"));
        if (gkFocus != isGkRole)
            continue; // GK methods only for GK roles, outfield methods only otherwise

        double sum = 0.0;
        std::vector<QPair<double, QString>> contribs;
        bool allMental = true;
        bool anyLimited = false;
        for (const QString &attr : f.attributes) {
            const auto it = byName.constFind(attr);
            if (it == byName.constEnd())
                continue; // not trainable / not in this role's DWRS / character
            const Recommendation *r = it.value();
            sum += r->priority;
            contribs.push_back({r->priority, attr});
            if (r->group != Group::Mental)
                allMental = false;
            if (r->group != Group::Mental && r->devFactor < 0.999)
                anyLimited = true;
        }
        if (sum <= 0.0)
            continue;

        std::sort(contribs.begin(), contribs.end(),
                  [](const QPair<double, QString> &a, const QPair<double, QString> &b) {
                      return a.first > b.first;
                  });
        FocusRecommendation fr;
        fr.focus = f.name;
        fr.category = f.category;
        fr.priority = sum;
        fr.allMental = allMental;
        fr.anyLimited = anyLimited;
        for (const auto &c : contribs)
            fr.attributes << c.second;
        out.focus.push_back(fr);
    }

    std::sort(out.focus.begin(), out.focus.end(),
              [](const FocusRecommendation &a, const FocusRecommendation &b) {
                  if (a.priority != b.priority)
                      return a.priority > b.priority;
                  return a.focus < b.focus;
              });
    return out;
}

} // namespace TrainingAdvice

} // namespace fm
