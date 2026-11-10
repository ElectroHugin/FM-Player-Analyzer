#include <QtTest>
#include <QImage>
#include <QDoubleSpinBox>
#include <QPainter>
#include "widgets/RatingCurveEditor.h"
#include "core/RatingProfile.h"
#include "core/RatingSnapshot.h"
#include "core/Definitions.h"

using namespace fm;
class TestRatingCurveEditor : public QObject {
    Q_OBJECT
private slots:
    void allPointsAndUndo() {
        RatingCurveEditor editor;
        editor.setCurve(RatingCurve::linear(19));
        QSignalSpy changed(&editor, &RatingCurveEditor::curveChanged);
        QVERIFY(editor.setContribution(1, -1));
        QCOMPARE(editor.curve().baseline, -1.0);
        QCOMPARE(editor.curve().weight, 20.0);
        QCOMPARE(*editor.curve().contribution(2), 1.0);
        QVERIFY(editor.setContribution(20, 22));
        QCOMPARE(editor.curve().weight, 23.0);
        QVERIFY(!editor.setContribution(10, 30));
        QCOMPARE(changed.size(), 2);
        editor.undo(); QCOMPARE(editor.curve().weight, 20.0);
        editor.undo(); QCOMPARE(editor.curve().baseline, 0.0);
        editor.redo(); QCOMPARE(editor.curve().baseline, -1.0);
    }
    void focusDoesNotRoundStoredCurve() {
        RatingCurveEditor editor; auto curve = RatingCurve::linear(19.123456789012345);
        curve.baseline = 0.12345678901234567; editor.setCurve(curve);
        const auto before = editor.curve(); QSignalSpy changed(&editor, &RatingCurveEditor::curveChanged);
        for (auto *spin : editor.findChildren<QDoubleSpinBox *>())
            QMetaObject::invokeMethod(spin, "editingFinished", Qt::DirectConnection);
        QCOMPARE(changed.size(), 0); QCOMPARE(editor.findChild<QDoubleSpinBox *>(QStringLiteral("curvePoint20"))->decimals(), 2); QCOMPARE(editor.curve().weight, before.weight);
        QCOMPARE(editor.curve().baseline, before.baseline); QCOMPARE(editor.curve().shape, before.shape);
    }
    void baselineRoundTripAndNormalization() {
        auto curve = RatingCurve::linear(19); curve.baseline = 50;
        const auto score = evaluateCurves({{curve, 10, 10, 1.5}});
        QVERIFY(score.normalized);
        QCOMPARE(score.absolute, 88.5);
        QVERIFY(std::abs(*score.normalized - 900.0/19) < 1e-12);
        RatingProfile profile;
        profile.id = QStringLiteral("test"); profile.name = QStringLiteral("Test");
        profile.compatibleGames = {QStringLiteral("fm24")};
        profile.attributes.insert(QStringLiteral("Pace"), curve);
        auto imported = RatingProfile::fromJson(profile.toJson(), nullptr);
        QVERIFY(imported); QCOMPARE(imported->toJson(), profile.toJson());
        Definitions definitions;
        QVERIFY(definitions.load(QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json")));
        auto compiled = RatingSnapshot::compile(profile, definitions, nullptr);
        QVERIFY(compiled);
        Player player; player.attrLo[idx(Attr::Pace)] = player.attrHi[idx(Attr::Pace)] = 10;
        const auto actual = compiled->evaluate(player, QStringLiteral("AF-A"));
        QCOMPARE(actual.absolute, 70.8);
        QVERIFY(std::abs(*actual.normalized - *score.normalized) < 1e-12);
    }
    void rendersEditor() {
        RatingCurveEditor editor;
        editor.setCurve(*harvestgreenCurve({-40, -26.8, 0, 19.4}, 4));
        editor.resize(900, 520); editor.show(); QTest::qWait(20);
        const auto path = qEnvironmentVariable("FM_CURVE_EDITOR_SCREENSHOT");
        if (!path.isEmpty()) QVERIFY(editor.grab().save(path));
        QVERIFY(editor.findChild<QWidget *>(QStringLiteral("curvePoint20")));
    }
};
QTEST_MAIN(TestRatingCurveEditor)
#include "test_ratingcurveeditor.moc"
