#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/SigningAdvisor.h"
#include "core/SquadBuilder.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace fm;

namespace {

const QString kBpd = QStringLiteral("BPD-D");
const QString kDm = QStringLiteral("DM-S");
const QString kAm = QStringLiteral("AM-A");

struct Spec {
    const char *name;
    const char *position;
    double bpd; // 0 = no rating for the role
    double dm;
    double am;
};

// Players + their ratings; the pointers stay valid while the fixture lives.
struct Fixture {
    std::vector<Player> players;
    RoleRatings ratings;

    explicit Fixture(std::initializer_list<Spec> specs)
    {
        players.reserve(specs.size());
        for (const Spec &spec : specs) {
            Player p;
            p.uid = QString::fromLatin1(spec.name);
            p.name = p.uid;
            p.positionRaw = QString::fromLatin1(spec.position);
            const auto rate = [&](const QString &role, double value) {
                if (value > 0) {
                    p.assignedRoles << role;
                    ratings[role].insert(p.uid, value);
                }
            };
            rate(kBpd, spec.bpd);
            rate(kDm, spec.dm);
            rate(kAm, spec.am);
            players.push_back(p);
        }
    }

    std::vector<const Player *> pick(std::initializer_list<const char *> names) const
    {
        std::vector<const Player *> result;
        for (const char *name : names) {
            for (const Player &p : players) {
                if (p.uid == QLatin1String(name))
                    result.push_back(&p);
            }
        }
        return result;
    }
};

const SlotNeed *needFor(const SigningAnalysis &analysis, const QString &slot)
{
    for (const SlotNeed &need : analysis.needs) {
        if (need.slot == slot)
            return &need;
    }
    return nullptr;
}

} // namespace

class TestSigningAdvisor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_definitions = std::make_unique<Definitions>();
        QVERIFY2(m_definitions->load(QStringLiteral(LEGACY_DIR)
                                     + QStringLiteral("/config/definitions.json")),
                 qPrintable(m_definitions->errorString()));
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
        m_builder = std::make_unique<SquadBuilder>(*m_definitions, *m_config);
    }

    void ranksPositionsByMeasuredGain()
    {
        // The weak spot is the second centre-back (62), not the AMC who merely
        // sits below the team median: the affordable market has a 75 BPD, a DM
        // a little better than ours and an AMC just one point better.
        const Fixture f{
            {"Dc1", "D (C)", 70, 0, 0},      {"Dc2", "D (C)", 62, 0, 0},
            {"Dm1", "DM", 0, 74, 0},         {"Dm2", "DM", 0, 73, 0},
            {"Amc", "AM (C)", 0, 0, 71},
            {"MBpd", "D (C)", 75, 0, 0},     {"MDm", "DM", 0, 76, 0},
            {"MAmc", "AM (C)", 0, 0, 72},    {"MWeak", "DM", 0, 60, 0},
        };
        const QHash<QString, QString> positions{
            {QStringLiteral("DCL"), kBpd}, {QStringLiteral("DCR"), kBpd},
            {QStringLiteral("DMCL"), kDm}, {QStringLiteral("DMCR"), kDm},
            {QStringLiteral("AMC"), kAm}};
        const QStringList slotOrder{QStringLiteral("DCL"), QStringLiteral("DCR"),
                                    QStringLiteral("DMCL"), QStringLiteral("DMCR"),
                                    QStringLiteral("AMC")};

        const SigningAnalysis analysis = SigningAdvisor::analyze(
            *m_builder, f.pick({"Dc1", "Dc2", "Dm1", "Dm2", "Amc"}),
            f.pick({"MBpd", "MDm", "MAmc", "MWeak"}), positions, slotOrder, f.ratings);

        QCOMPARE(analysis.xiTotal, 70.0 + 62 + 74 + 73 + 71);
        QCOMPARE(analysis.marketSize, 4);
        // Biggest lever first: a centre-back slot, +13 with the 75 BPD.
        const SlotNeed &top = analysis.needs.front();
        QCOMPARE(positions.value(top.slot), kBpd);
        QCOMPARE(top.options.front().player->uid, QStringLiteral("MBpd"));
        QCOMPARE(top.options.front().xiGain, 13.0);
        // The weaker centre-back drops to the B-team.
        QCOMPARE(static_cast<int>(top.options.front().moves.size()), 1);
        QCOMPARE(top.options.front().moves.front().player->uid, QStringLiteral("Dc2"));
        QVERIFY(top.options.front().moves.front().toBTeam);

        // AMC: one market player, one point better.
        const SlotNeed *amc = needFor(analysis, QStringLiteral("AMC"));
        QVERIFY(amc);
        QCOMPARE(amc->marketCount, 1);
        QCOMPARE(amc->marketBetter, 1);
        QCOMPARE(amc->marketBest, 72.0);
        QCOMPARE(amc->bestGain(), 1.0);

        // The 60 DM improves nothing and is no option anywhere.
        for (const SlotNeed &need : analysis.needs)
            for (const SigningOption &option : need.options)
                QVERIFY(option.player->uid != QStringLiteral("MWeak"));
        // Gains descend over the slots.
        for (size_t i = 1; i < analysis.needs.size(); ++i)
            QVERIFY(analysis.needs[i - 1].bestGain() >= analysis.needs[i].bestGain());
    }

    void countsKnockOnEffects()
    {
        // Versatile V plays centre-back today (78) because nobody else can,
        // though he is the better DM (79). A 77 centre-back — weaker than V
        // there — still gains 8: V moves to DM, which beats the 70 DM.
        const Fixture f{
            {"Dc1", "D (C)", 80, 0, 0},
            {"V", "D (C), DM", 78, 79, 0},
            {"Dm1", "DM", 0, 70, 0},
            {"MDc", "D (C)", 77, 0, 0},
        };
        const QHash<QString, QString> positions{{QStringLiteral("DCL"), kBpd},
                                                {QStringLiteral("DCR"), kBpd},
                                                {QStringLiteral("DMCL"), kDm}};
        const QStringList slotOrder{QStringLiteral("DCL"), QStringLiteral("DCR"),
                                    QStringLiteral("DMCL")};

        const SigningAnalysis analysis =
            SigningAdvisor::analyze(*m_builder, f.pick({"Dc1", "V", "Dm1"}), f.pick({"MDc"}),
                                    positions, slotOrder, f.ratings);
        const SlotNeed &top = analysis.needs.front();
        QCOMPARE(positions.value(top.slot), kBpd);
        QCOMPARE(top.options.size(), size_t(1));
        const SigningOption &option = top.options.front();
        QCOMPARE(option.xiGain, 8.0); // 80 + 77 + 79 vs 80 + 78 + 70
        QCOMPARE(option.bTeamGain, 70.0); // Dm1 now plays for the B-team
        QStringList moved;
        for (const SquadMove &move : option.moves)
            moved << move.player->uid;
        moved.sort();
        QCOMPARE(moved, (QStringList{QStringLiteral("Dm1"), QStringLiteral("V")}));
        for (const SquadMove &move : option.moves) {
            if (move.player->uid == QStringLiteral("V")) {
                QCOMPARE(move.toSlot, QStringLiteral("DMCL"));
                QVERIFY(!move.toBTeam);
            }
        }
        // The DM slot itself shows no signing: nobody on the market plays there.
        const SlotNeed *dm = needFor(analysis, QStringLiteral("DMCL"));
        QVERIFY(dm && dm->options.empty() && dm->marketCount == 0);
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Definitions> m_definitions;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<SquadBuilder> m_builder;
};

QTEST_GUILESS_MAIN(TestSigningAdvisor)
#include "test_signingadvisor.moc"
