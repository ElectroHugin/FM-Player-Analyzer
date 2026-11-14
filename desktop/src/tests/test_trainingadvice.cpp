#include "core/AppConfig.h"
#include "core/Attributes.h"
#include "core/Definitions.h"
#include "core/DwrsEngine.h"
#include "core/Player.h"
#include "core/TrainingAdvice.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace fm;
using namespace fm::TrainingAdvice;

namespace {

QString definitionsPath()
{
    return QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json");
}

void setAttr(Player &p, const QString &name, int value)
{
    const int idx = attrIndexByName(name);
    QVERIFY2(idx >= 0, qPrintable(name));
    p.attrLo[idx] = static_cast<uint8_t>(value);
    p.attrHi[idx] = static_cast<uint8_t>(value);
}

// A winger-ish outfielder with every DWRS attribute mid-valued, so the advisor
// has headroom on all of them and the ranking is driven by weight/age.
Player baseOutfielder(int age)
{
    Player p;
    p.age = age;
    p.positionRaw = QStringLiteral("AM (R)");
    for (const QString &attr : attrNames())
        setAttr(p, attr, 10);
    return p;
}

} // namespace

class TestTrainingAdvice : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Definitions> m_definitions;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<DwrsEngine> m_engine;
    QTemporaryDir m_dir;
    AgeWindows m_windows; // defaults

    Advice advise(const Player &p, const QString &role)
    {
        return adviseForRole(p, role, *m_engine, m_windows);
    }

private slots:
    void initTestCase()
    {
        m_definitions = std::make_unique<Definitions>();
        QVERIFY2(m_definitions->load(definitionsPath()),
                 qPrintable(m_definitions->errorString()));
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
        m_engine = std::make_unique<DwrsEngine>(*m_definitions, *m_config);
    }

    void profileStandardMatchesAgeWindows()
    {
        // The standard profile gives exactly the advice of the age windows.
        const TrainingProfile standard = standardProfile(m_windows);
        for (int age : {17, 22, 26, 31}) {
            const Player p = baseOutfielder(age);
            const Advice a = advise(p, QStringLiteral("WR-S"));
            const Advice b = adviseForRole(p, QStringLiteral("WR-S"), *m_engine, standard);
            QCOMPARE(a.focus.size(), b.focus.size());
            for (size_t i = 0; i < a.focus.size(); ++i) {
                QCOMPARE(a.focus[i].attrName, b.focus[i].attrName);
                QCOMPARE(a.focus[i].priority, b.focus[i].priority);
            }
            QCOMPARE(a.mentoring.size(), b.mentoring.size());
            QVERIFY(a.valid && !a.focus.empty());
        }
        // Every attribute sits in exactly one band.
        for (const QString &attr : attrNames())
            QVERIFY2(standard.bandOf(attr) >= 0, qPrintable(attr));
    }

    void profileJsonRoundTripAndValidation()
    {
        TrainingProfile profile = standardProfile(m_windows);
        profile.bands[0].weight = 1.5;
        profile.bands.append({QStringLiteral("Own"), 19, 22, 0.5, false, {}});
        TrainingProfile back;
        QVERIFY(TrainingProfile::fromJson(profile.toJson(), &back));
        QVERIFY(back == profile);

        // Unknown attributes are dropped, duplicates keep the first band,
        // locked age never below the peak.
        QJsonObject json = profile.toJson();
        QJsonArray bands = json.value(QStringLiteral("bands")).toArray();
        QJsonObject extra;
        extra.insert(QStringLiteral("name"), QStringLiteral("X"));
        extra.insert(QStringLiteral("peak_age"), 25);
        extra.insert(QStringLiteral("locked_age"), 20);
        extra.insert(QStringLiteral("attributes"),
                     QJsonArray{QStringLiteral("Pace"), QStringLiteral("Nonsense")});
        bands.append(extra);
        json.insert(QStringLiteral("bands"), bands);
        QVERIFY(TrainingProfile::fromJson(json, &back));
        QVERIFY(back.bands.last().attributes.isEmpty());
        QCOMPARE(back.bands.last().lockedAge, 25);

        QVERIFY(!TrainingProfile::fromJson(QJsonObject{}, &back));
    }

    void profileWeightsAndMissingAttributes()
    {
        const Player p = baseOutfielder(18);
        TrainingProfile profile = standardProfile(m_windows);
        const int explosive = profile.bandOf(QStringLiteral("Pace"));
        // Without a band Pace is never advised.
        const Advice with = adviseForRole(p, QStringLiteral("WR-S"), *m_engine, profile);
        QVERIFY(std::any_of(with.focus.begin(), with.focus.end(),
                            [](const Recommendation &r) { return r.attrName == QLatin1String("Pace"); }));
        const Advice without = adviseForRole(p, QStringLiteral("WR-S"), *m_engine, [&] {
            TrainingProfile copy = profile;
            copy.bands[explosive].attributes.removeAll(QStringLiteral("Pace"));
            return copy;
        }());
        for (const Recommendation &r : without.focus)
            QVERIFY(r.attrName != QLatin1String("Pace"));
        // A zero weight takes the whole band out of the focus list.
        profile = standardProfile(m_windows);
        profile.bands[explosive].weight = 0.0;
        const Advice zero = adviseForRole(p, QStringLiteral("WR-S"), *m_engine, profile);
        for (const Recommendation &r : zero.focus)
            QVERIFY(profile.bandOf(r.attrName) != explosive);
    }

    void devFactorWindows()
    {
        // Explosive: full until 21, zero at 24, half at ~22-23.
        QCOMPARE(devFactor(Group::Explosive, 18, m_windows), 1.0);
        QCOMPARE(devFactor(Group::Explosive, 21, m_windows), 1.0);
        QCOMPARE(devFactor(Group::Explosive, 24, m_windows), 0.0);
        QVERIFY(devFactor(Group::Explosive, 23, m_windows) > 0.0);
        QVERIFY(devFactor(Group::Explosive, 23, m_windows) < 1.0);
        // Mental never locks; character never focus-trains.
        QCOMPARE(devFactor(Group::Mental, 34, m_windows), 1.0);
        QCOMPARE(devFactor(Group::Character, 18, m_windows), 0.0);
    }

    void perPointFavoursHighWeightAttribute()
    {
        // Pace is "Extremely Important" (weight 8); Positioning "Almost
        // Irrelevant" (0.2). Pace must have the larger marginal DWRS value.
        const QHash<int, double> perPoint =
            m_engine->attributeDwrsPerPoint(QStringLiteral("WR-S"));
        const double pace = perPoint.value(attrIndexByName(QStringLiteral("Pace")));
        const double positioning =
            perPoint.value(attrIndexByName(QStringLiteral("Positioning")));
        QVERIFY(pace > 0.0);
        QVERIFY(pace > positioning);
    }

    void youngPlayerTrainsPaceFirst()
    {
        const Player p = baseOutfielder(19);
        const Advice a = advise(p, QStringLiteral("WR-S"));
        QVERIFY(a.valid);
        QVERIFY(!a.focus.empty());
        // Pace/Acceleration should top the list for a young wide attacker.
        const QString top = a.focus.front().attrName;
        QVERIFY2(top == QStringLiteral("Pace") || top == QStringLiteral("Acceleration"),
                 qPrintable(top));
    }

    void oldPlayerDropsPhysicalFocus()
    {
        const Player p = baseOutfielder(30);
        const Advice a = advise(p, QStringLiteral("WR-S"));
        QVERIFY(a.valid);
        // At 30, explosive & strength & technical are locked -> no physical or
        // technical attribute may appear in the focus list; only mentals remain.
        for (const Recommendation &r : a.focus) {
            QVERIFY2(r.group == Group::Mental,
                     qPrintable(r.attrName + " group=" + groupName(r.group)));
            QVERIFY(r.devFactor > 0.0);
        }
        QVERIFY(!a.focus.empty()); // mentals still trainable
    }

    void maxedAttributeIsNotRecommended()
    {
        Player p = baseOutfielder(19);
        setAttr(p, QStringLiteral("Pace"), 20); // no headroom
        const Advice a = advise(p, QStringLiteral("WR-S"));
        for (const Recommendation &r : a.focus)
            QVERIFY(r.attrName != QStringLiteral("Pace"));
    }

    void characterAttrsGoToMentoringNotFocus()
    {
        const Player p = baseOutfielder(19);
        const Advice a = advise(p, QStringLiteral("WR-S"));
        for (const Recommendation &r : a.focus)
            QVERIFY(r.group != Group::Character);
        // Determination feeds DWRS (category "Good") -> should surface as a
        // mentoring note rather than a training focus.
        bool determinationNoted = false;
        for (const Recommendation &r : a.mentoring)
            if (r.attrName == QStringLiteral("Determination"))
                determinationNoted = true;
        QVERIFY(determinationNoted);
    }

    // --- Training-focus (in-game method) advice ---

    void youngPlayerTopFocusIsQuickness()
    {
        const Player p = baseOutfielder(19);
        const FocusAdvice a =
            adviseFocusesForRole(p, QStringLiteral("WR-S"), *m_engine, m_windows);
        QVERIFY(a.valid);
        QVERIFY(!a.focus.empty());
        // Quickness trains Pace+Acceleration (both "Extremely Important") -> top.
        QCOMPARE(a.focus.front().focus, QStringLiteral("Quickness"));
    }

    void rehabFocusesAreNeverRecommended()
    {
        const Player p = baseOutfielder(19);
        const FocusAdvice a =
            adviseFocusesForRole(p, QStringLiteral("WR-S"), *m_engine, m_windows);
        for (const FocusRecommendation &f : a.focus)
            QVERIFY2(f.category != QStringLiteral("Injury Rehab"), qPrintable(f.focus));
    }

    void oldPlayerFocusIsMentalMethod()
    {
        const Player p = baseOutfielder(30);
        const FocusAdvice a =
            adviseFocusesForRole(p, QStringLiteral("WR-S"), *m_engine, m_windows);
        QVERIFY(a.valid);
        QVERIFY(!a.focus.empty());
        // Physical/technical are locked at 30, so the top method must be a
        // purely mental one (e.g. Attacking Movement / Final Third).
        QVERIFY(a.focus.front().allMental);
        QVERIFY(a.focus.front().focus != QStringLiteral("Quickness"));
    }

    void outfieldRoleGetsNoGkMethods()
    {
        // Regression: GK methods share mental attributes with outfielders
        // (GK Reactions -> Anticipation/Concentration) and used to leak in.
        for (int age : {19, 25, 31}) {
            const Player p = baseOutfielder(age);
            const FocusAdvice a =
                adviseFocusesForRole(p, QStringLiteral("WR-S"), *m_engine, m_windows);
            for (const FocusRecommendation &f : a.focus)
                QVERIFY2(!f.focus.startsWith(QStringLiteral("GK")), qPrintable(f.focus));
        }
    }

    void gkRoleGetsOnlyGkMethods()
    {
        const Player p = baseOutfielder(22);
        const FocusAdvice a =
            adviseFocusesForRole(p, QStringLiteral("SK-S"), *m_engine, m_windows);
        QVERIFY(a.valid);
        QVERIFY(!a.focus.empty());
        for (const FocusRecommendation &f : a.focus)
            QVERIFY2(f.focus.startsWith(QStringLiteral("GK")), qPrintable(f.focus));
    }

    void focusAggregatesMultipleAttributes()
    {
        // Attacking Movement trains Off the Ball + Anticipation + Decisions; for
        // a 30-year-old (mentals only) it should carry several contributors.
        const Player p = baseOutfielder(30);
        const FocusAdvice a =
            adviseFocusesForRole(p, QStringLiteral("AM-S"), *m_engine, m_windows);
        bool found = false;
        for (const FocusRecommendation &f : a.focus) {
            if (f.focus == QStringLiteral("Attacking Movement")) {
                found = true;
                QVERIFY(f.attributes.size() >= 2);
            }
        }
        QVERIFY(found);
    }
};

QTEST_APPLESS_MAIN(TestTrainingAdvice)
#include "test_trainingadvice.moc"
