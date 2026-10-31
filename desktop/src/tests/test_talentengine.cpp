#include "core/Attributes.h"
#include "core/Definitions.h"
#include "core/Player.h"
#include "core/TalentEngine.h"

#include <QtTest>

using namespace fm;

// Backlog #37 / P7: Talent Score =
//   best DWRS + 2 per year under the age cap + (Det + WR - 20) / 4 + personality.

class TestTalentEngine : public QObject
{
    Q_OBJECT

private:
    Definitions m_defs;

    static void setExact(Player &p, Attr attr, int value)
    {
        p.attrLo[idx(attr)] = static_cast<uint8_t>(value);
        p.attrHi[idx(attr)] = static_cast<uint8_t>(value);
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(m_defs.load(QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json")),
                 qPrintable(m_defs.errorString()));
    }

    void personalityBonus()
    {
        QCOMPARE(TalentEngine::personalityBonus(m_defs, QStringLiteral("Model Citizen")),
                 TalentEngine::kGoodPersonalityBonus);
        QCOMPARE(TalentEngine::personalityBonus(m_defs, QStringLiteral("Slack")),
                 TalentEngine::kBadPersonalityBonus);
        QCOMPARE(TalentEngine::personalityBonus(m_defs, QStringLiteral("Balanced")), 0.0);
        QCOMPARE(TalentEngine::personalityBonus(m_defs, QString()), 0.0);
        QCOMPARE(TalentEngine::personalityBonus(m_defs, QStringLiteral("Not A Personality")), 0.0);
    }

    void formula()
    {
        // 70 + 2*(24-18) + (15+15-20)/4 + 3 = 70 + 12 + 2.5 + 3
        QCOMPARE(TalentEngine::calculateTalentScore(m_defs, 70.0, 18.0, 15.0, 15.0,
                                                    QStringLiteral("Model Citizen"), 24.0),
                 87.5);
        // Older than the cap is a penalty; low mentals and a bad personality subtract.
        QCOMPARE(TalentEngine::calculateTalentScore(m_defs, 60.0, 26.0, 5.0, 7.0,
                                                    QStringLiteral("Slack"), 24.0),
                 60.0 - 4.0 - 2.0 - 5.0);
    }

    void talentForPlayerUsesAttributes()
    {
        Player p;
        p.age = 20;
        p.personality = QStringLiteral("Balanced");
        setExact(p, Attr::Determination, 18);
        setExact(p, Attr::WorkRate, 14);
        QCOMPARE(TalentEngine::talentForPlayer(m_defs, p, 50.0, 24.0), 50.0 + 8.0 + 3.0);
    }

    void maskedRangesCountAsZero()
    {
        // Legacy parity: "12-15" fails float() in the Python app and counts as 0.
        Player p;
        p.age = 24;
        p.attrLo[idx(Attr::Determination)] = 12;
        p.attrHi[idx(Attr::Determination)] = 15;
        setExact(p, Attr::WorkRate, 20);
        QCOMPARE(TalentEngine::talentForPlayer(m_defs, p, 50.0, 24.0), 50.0 + (0.0 + 20.0 - 20.0) / 4.0);
    }

    void missingAgeGivesNoRunway()
    {
        Player p; // age 0
        setExact(p, Attr::Determination, 10);
        setExact(p, Attr::WorkRate, 10);
        QCOMPARE(TalentEngine::talentForPlayer(m_defs, p, 40.0, 24.0), 40.0);
    }

    void goalkeepersUseTheirOwnCap()
    {
        Player gk;
        gk.positionRaw = QStringLiteral("GK");
        Player st;
        st.positionRaw = QStringLiteral("ST (C)");
        QCOMPARE(TalentEngine::ageCapForPlayer(gk, 24, 27), 27.0);
        QCOMPARE(TalentEngine::ageCapForPlayer(st, 24, 27), 24.0);
    }
};

QTEST_GUILESS_MAIN(TestTalentEngine)
#include "test_talentengine.moc"
