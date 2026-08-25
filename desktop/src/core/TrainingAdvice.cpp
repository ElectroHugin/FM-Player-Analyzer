#include "TrainingAdvice.h"

#include "Attributes.h"
#include "DwrsEngine.h"

#include <QHash>

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

} // namespace TrainingAdvice

} // namespace fm
