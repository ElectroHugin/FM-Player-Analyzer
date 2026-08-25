#include "core/AppConfig.h"
#include "core/Attributes.h"
#include "core/Definitions.h"
#include "core/DwrsEngine.h"
#include "core/Player.h"
#include "core/TrainingAdvice.h"

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
};

QTEST_APPLESS_MAIN(TestTrainingAdvice)
#include "test_trainingadvice.moc"
