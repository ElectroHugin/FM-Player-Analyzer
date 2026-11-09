// Tactical views of a two-phase tactic (2.2.3): the combined pitch puts each
// player midway between his two places and routes clicks to him; the club
// overview's markers show the starter and route clicks too. Headless.
#include "core/AppConfig.h"
#include "core/Utils.h"
#include "core/PitchGeometry.h"
#include "theming/ThemeManager.h"
#include "widgets/StrengthGridWidget.h"
#include "widgets/TacticPitchWidget.h"

#include <QContextMenuEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace fm;

class TestPitchViews : public QObject
{
    Q_OBJECT

private:
    // Field rectangle of TacticPitchWidget (rect minus 1 px frame and 8 px margin).
    static QPointF onField(const QWidget &w, const QPointF &fraction, double margin)
    {
        const QRectF field = QRectF(w.rect()).adjusted(1 + margin, 1 + margin, -1 - margin,
                                                       -1 - margin);
        return {field.left() + field.width() * fraction.x(),
                field.top() + field.height() * fraction.y()};
    }

private slots:
    void independentRoleColours()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        ThemeManager theme(config);
        TacticPitchWidget pitch(theme);
        pitch.resize(480, 742);
        TacticPitchWidget::CombinedCard card;
        card.uid = QStringLiteral("saibari");
        card.ipSlot = QStringLiteral("AMC");
        card.oopSlot = QStringLiteral("MC");
        card.name = QStringLiteral("Saibari");
        pitch.setCombinedTeam({card}, {{card.ipSlot, QStringLiteral("AM-IP")}},
            {{QStringLiteral("Attacking Midfield"), {card.ipSlot}}},
            {{card.oopSlot, QStringLiteral("PCM")}},
            {{QStringLiteral("Midfield"), {card.oopSlot}}});
        TacticPitchWidget::setColorRowSource([](const QString &role) {
            return role == QLatin1String("AM-IP") ? 2 : 3;
        });
        const auto rendered = pitch.grab().toImage(); // isolated offscreen widget, no desktop capture
        TacticPitchWidget::setColorRowSource({});
        int purple = 0, blue = 0;
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x) {
                purple += rendered.pixelColor(x, y) == QColor(0x5e, 0x2e, 0x8f);
                blue += rendered.pixelColor(x, y) == QColor(0x27, 0x55, 0xa6);
            }
        QVERIFY(purple > 100);
        QVERIFY(blue > 100);
    }

    void strengthUsesAbsoluteDwrsColours()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        ThemeManager theme(config);
        StrengthGridWidget pitch(theme);
        StrengthGridWidget::Marker marker;
        marker.uid = QStringLiteral("player");
        marker.anchor = QPointF(0.5, 0.5);
        marker.value = 73;
        pitch.setMarkers({marker});
        const auto rendered = pitch.grab().toImage();
        int matching = 0;
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x)
                matching += rendered.pixelColor(x, y) == dwrsCellStyle(73).background;
        QVERIFY(matching > 100);
    }

    void combinedPortraitsUseTheSameSource()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        ThemeManager theme(config);
        TacticPitchWidget pitch(theme);
        pitch.resize(480, 742);
        TacticPitchWidget::CombinedCard card;
        card.uid = QStringLiteral("portrait-player");
        card.name = QStringLiteral("Portrait Player");
        card.ipSlot = card.oopSlot = QStringLiteral("STC");
        card.value = card.ipRating = card.oopRating = 70;
        const QHash<QString, QString> roles{{QStringLiteral("STC"), QStringLiteral("AF")}};
        const QHash<QString, QStringList> layout{{QStringLiteral("Strikers"), {QStringLiteral("STC")}}};
        pitch.setCombinedTeam({card}, roles, layout, roles, layout);
        const auto before = pitch.grab().toImage();
        QString requested;
        TacticPitchWidget::setPortraitSource([&](const QString &uid, int size) {
            requested = uid;
            QPixmap face(size, size);
            face.fill(QColor(255, 0, 255));
            return face;
        });
        const auto after = pitch.grab().toImage();
        TacticPitchWidget::setPortraitSource({});
        QCOMPARE(requested, card.uid);
        QVERIFY(before != after);
        int portraitPixels = 0;
        for (int y = 0; y < after.height(); ++y)
            for (int x = 0; x < after.width(); ++x)
                portraitPixels += after.pixelColor(x, y) == QColor(255, 0, 255);
        QVERIFY(portraitPixels > 100);
    }

    void combinedCardSitsBetweenBothPlaces()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        ThemeManager theme(config);
        TacticPitchWidget pitch(theme);
        pitch.resize(480, 480 * 105 / 68);
        pitch.setFmVersionId(QStringLiteral("fm24"));

        // A right-back who moves into midfield with the ball; a keeper; and
        // a free place without the ball.
        const QHash<QString, QString> ip{{QStringLiteral("GK"), QStringLiteral("GKD")},
                                         {QStringLiteral("MCR"), QStringLiteral("IFB")},
                                         {QStringLiteral("STC"), QStringLiteral("AF")}};
        const QHash<QString, QString> oop{{QStringLiteral("GK"), QStringLiteral("GKD")},
                                          {QStringLiteral("DR"), QStringLiteral("FB")},
                                          {QStringLiteral("STC"), QStringLiteral("PF")}};
        const QHash<QString, QStringList> ipLayout{
            {QStringLiteral("Midfield"), {QStringLiteral("MCR")}},
            {QStringLiteral("Strikers"), {QStringLiteral("STC")}}};
        const QHash<QString, QStringList> oopLayout{
            {QStringLiteral("Defense"), {QStringLiteral("DR")}},
            {QStringLiteral("Strikers"), {QStringLiteral("STC")}}};
        TacticPitchWidget::CombinedCard back;
        back.uid = QStringLiteral("back");
        back.name = QStringLiteral("Test Back");
        back.ipSlot = QStringLiteral("MCR");
        back.oopSlot = QStringLiteral("DR");
        back.ipRating = 70;
        back.oopRating = 60;
        back.value = 65;
        TacticPitchWidget::CombinedCard keeper = back;
        keeper.uid = QStringLiteral("keeper");
        keeper.ipSlot = keeper.oopSlot = QStringLiteral("GK");
        pitch.setCombinedTeam({back, keeper}, ip, ipLayout, oop, oopLayout);
        QVERIFY(pitch.isCombined());
        const QImage image = pitch.grab().toImage(); // paints, fills the hit boxes
        QVERIFY(!image.isNull());
        if (const QString out = qEnvironmentVariable("FMPA_TEST_PNG"); !out.isEmpty())
            image.save(out + QStringLiteral("/combined.png"));

        const QPointF mid = combinedAnchor(slotAnchors(QStringLiteral("fm24"), ipLayout).value(QStringLiteral("MCR")),
                                           slotAnchors(QStringLiteral("fm24"), oopLayout).value(QStringLiteral("DR")));
        QSignalSpy menu(&pitch, &TacticPitchWidget::playerContextMenuRequested);
        QSignalSpy empty(&pitch, &TacticPitchWidget::emptySlotContextMenuRequested);
        const QPoint at = onField(pitch, mid, 8).toPoint();
        QContextMenuEvent onCard(QContextMenuEvent::Mouse, at, pitch.mapToGlobal(at));
        QApplication::sendEvent(&pitch, &onCard);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.takeFirst().at(0).toString(), QStringLiteral("back"));

        // The striker place is free in both line-ups: two markers of their
        // own, keyed by phase, at the striker slot (no guessed pairing).
        // Both sit on the same spot and are spread sideways: click left of it.
        const QPointF stAnchor =
            slotAnchors(QStringLiteral("fm24"), ipLayout).value(QStringLiteral("STC"));
        const QPoint st = onField(pitch, stAnchor - QPointF(0.06, 0), 8).toPoint();
        QContextMenuEvent onFree(QContextMenuEvent::Mouse, st, pitch.mapToGlobal(st));
        QApplication::sendEvent(&pitch, &onFree);
        QCOMPARE(empty.count(), 1);
        const QString key = empty.takeFirst().at(0).toString();
        QCOMPARE(key, QStringLiteral("ip:STC")); // with ball first, so on the left
    }

    void overviewMarkersRouteClicks()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        ThemeManager theme(config);
        StrengthGridWidget grid(theme);
        grid.setPitchWidth(300);
        StrengthGridWidget::Marker starter;
        starter.uid = QStringLiteral("s1");
        starter.anchor = QPointF(0.5, 0.5);
        starter.header = QStringLiteral("CM · DM");
        starter.value = 66;
        starter.impact = QStringLiteral("XI −4.0");
        starter.tooltip = QStringLiteral("detail");
        StrengthGridWidget::Marker freePlace;
        freePlace.anchor = QPointF(0.5, 0.5); // same spot: must be moved apart
        freePlace.header = QStringLiteral("DR");
        freePlace.impact = QStringLiteral("free");
        grid.setMarkers({starter, freePlace});
        const QImage image = grid.grab().toImage();
        if (const QString out = qEnvironmentVariable("FMPA_TEST_PNG"); !out.isEmpty())
            image.save(out + QStringLiteral("/overview.png"));
        QSignalSpy menu(&grid, &StrengthGridWidget::playerContextMenuRequested);
        // The two markers were spread sideways; the starter came first (left).
        const QRectF field = QRectF(grid.rect()).adjusted(7, 7, -7, -7);
        const QPoint left(static_cast<int>(field.left() + field.width() * 0.42),
                          static_cast<int>(field.center().y()));
        QContextMenuEvent event(QContextMenuEvent::Mouse, left, grid.mapToGlobal(left));
        QApplication::sendEvent(&grid, &event);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.takeFirst().at(0).toString(), QStringLiteral("s1"));
        const QPoint right(static_cast<int>(field.left() + field.width() * 0.58),
                           static_cast<int>(field.center().y()));
        QContextMenuEvent onFree(QContextMenuEvent::Mouse, right, grid.mapToGlobal(right));
        QApplication::sendEvent(&grid, &onFree);
        QCOMPARE(menu.count(), 0); // an empty place has no player menu
    }
};

QTEST_MAIN(TestPitchViews)
#include "test_pitchviews.moc"
