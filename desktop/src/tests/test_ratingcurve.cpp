#include <QtTest>
#include "core/RatingCurve.h"
#include <limits>
#include <cmath>

using namespace fm;

class TestRatingCurve : public QObject {
    Q_OBJECT
private slots:
    void normalizationAndRoleFactors()
    {
        for (int value : {1, 6, 12, 18, 19, 20}) {
            const auto result = evaluateCurves({{RatingCurve::linear(2), value, value, 1.5},
                                                {RatingCurve::linear(3), value, value, 1.2}});
            QVERIFY(result.normalized);
            QVERIFY(std::abs(*result.normalized - (value - 1) * 100.0 / 19) < 1e-12);
        }
        const auto result = evaluateCurves({{RatingCurve::linear(2), 20, 20, 1.5},
                                            {RatingCurve::linear(3), 1, 1, 1.2}});
        QVERIFY(std::abs(*result.normalized - 100.0 * 3 / 6.6) < 1e-12);
    }

    void harvestgreenPlateauAndNoise()
    {
        const auto pace = harvestgreenCurve({-40, -26.8, 0, 19.4}, 4);
        QVERIFY(pace);
        QCOMPARE(pace->weight, 59.4);
        QCOMPARE(*pace->contribution(18), *pace->contribution(19));
        QCOMPARE(*pace->contribution(19), *pace->contribution(20));
        QVERIFY(std::abs(*pace->contribution(6) - 13.2) < 1e-12);
        const auto vision = harvestgreenCurve({-8.2, -8.2, 0, -5.2}, 4);
        QVERIFY(vision);
        QVERIFY(std::abs(vision->weight - 5.6) < 1e-12);
        QCOMPARE(vision->shape[0], vision->shape[5]);
        QCOMPARE(vision->shape[11], vision->shape[17]);
        QCOMPARE(harvestgreenCurve({-8.2, -8.2, 0, -5.2}, 6)->weight, 0.0);
        QCOMPARE(harvestgreenCurve({2, 1, 0, -0.8}, 4)->weight, 0.0);
        QCOMPARE(harvestgreenCurve({-4, 0, 0, 0}, 4)->weight, 0.0);
    }

    void rangeIsMeanOfContributions()
    {
        const auto curve = harvestgreenCurve({-22, -6.2, 0, 0}, 4);
        QVERIFY(curve);
        const double ranged = *curve->expectedContribution(6, 18);
        QVERIFY(ranged < *curve->contribution(12));
        double sum = 0;
        for (int x = 6; x <= 18; ++x)
            sum += *curve->contribution(x);
        QVERIFY(std::abs(ranged - sum / 13) < 1e-12);
    }

    void unavailableAndInvalid()
    {
        QVERIFY(!evaluateCurves({}).normalized);
        QCOMPARE(evaluateCurves({{RatingCurve::linear(0), 0, 0, 1}}).status,
                 CurveScore::Status::NoVariation);
        QCOMPARE(evaluateCurves({{RatingCurve::linear(1), 0, 0, 1}}).status,
                 CurveScore::Status::MissingAttribute);
        for (auto range : {std::pair{0, 4}, std::pair{20, 21}, std::pair{5, 4}})
            QCOMPARE(evaluateCurves({{RatingCurve::linear(1), range.first, range.second, 1}}).status,
                     CurveScore::Status::InvalidInput);
        auto curve = RatingCurve::linear(1);
        curve.shape[4] = 0.9;
        QVERIFY(!curve.isValid());
        QVERIFY(!RatingCurve::linear(-1).isValid());
        QVERIFY(!RatingCurve::linear(std::numeric_limits<double>::infinity()).isValid());
        QVERIFY(!isotonicFit({0, std::numeric_limits<double>::quiet_NaN()}));
        QVERIFY(!harvestgreenCurve({0, 1, 2, 3}, -1));
        auto huge = RatingCurve::linear(std::numeric_limits<double>::max());
        QCOMPARE(evaluateCurves({{huge, 20, 20, 2}}).status, CurveScore::Status::InvalidInput);
    }

    void isotonicPoolsWholeViolations()
    {
        const auto fit = isotonicFit({0, 4, 2, 3});
        QVERIFY(fit);
        QCOMPARE(*fit, (std::vector<double>{0, 3, 3, 3}));
        const auto flat = isotonicFit({4, 3, 2, 1});
        QVERIFY(flat);
        QCOMPARE(flat->size(), size_t(4));
        for (double point : *flat)
            QVERIFY(std::abs(point - 2.5) < 1e-12);
    }
};

QTEST_GUILESS_MAIN(TestRatingCurve)
#include "test_ratingcurve.moc"
