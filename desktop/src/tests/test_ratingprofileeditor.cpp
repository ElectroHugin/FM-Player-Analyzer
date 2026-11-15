#include <QtTest>
#include <QTemporaryDir>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
#include <QFontDatabase>
#include <QTranslator>
#include "core/AppConfig.h"
#include "theming/ThemeManager.h"
#include "widgets/RatingProfileEditor.h"
#include "widgets/RatingCurveEditor.h"

using namespace fm;
class TestRatingProfileEditor : public QObject {
    Q_OBJECT
    static RatingProfile profile() {
        RatingProfile p; p.id = "personal"; p.name = "Personal";
        p.compatibleGames = {QStringLiteral("fm27")};
        p.attributes.insert("Pace", RatingCurve::linear(19));
        p.attributes.insert("Vision", RatingCurve::linear(5));
        return p;
    }
    static void clickAction(RatingProfileEditor &editor, int number) {
        auto *button = editor.findChild<QPushButton *>(QStringLiteral("profileAction%1").arg(number));
        QVERIFY(button); button->click();
    }
private slots:
    void initTestCase() {
        const auto id = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        if (id >= 0) QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).front(), 10));
    }
    void draftsSaveAndConflict() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("profiles.json");
        RatingProfileLibrary library; QVERIFY(library.insert(profile()).isEmpty());
        QVERIFY(library.save(path).isEmpty());
        RatingProfileEditor editor(path, "fm27");
        auto *attributes = editor.findChild<QListWidget *>(QStringLiteral("ratingAttributes"));
        QVERIFY(attributes); QCOMPARE(attributes->count(), 47);
        for (int i = 0; i < attributes->count(); ++i)
            if (attributes->item(i)->data(Qt::UserRole).toString() == QStringLiteral("Pace")) attributes->setCurrentRow(i);
        auto *curve = editor.findChild<RatingCurveEditor *>(); QVERIFY(curve);
        QVERIFY(curve->setContribution(20, 25)); QVERIFY(editor.hasDraftChanges());
        auto disk = RatingProfileLibrary::load(path, nullptr); QVERIFY(disk);
        QCOMPARE(disk->profiles().value("personal").attributes.value("Pace").weight, 19.0);
        clickAction(editor, 0); QVERIFY(!editor.hasDraftChanges());
        disk = RatingProfileLibrary::load(path, nullptr); QVERIFY(disk);
        QCOMPARE(disk->profiles().value("personal").attributes.value("Pace").weight, 25.0);

        auto external = disk->profiles().value("personal"); external.name = "Changed elsewhere";
        QVERIFY(disk->update(external, RatingProfileLibrary::documentRevision(disk->profiles().value("personal"))).isEmpty());
        QVERIFY(disk->save(path).isEmpty());
        QVERIFY(curve->setContribution(20, 30)); clickAction(editor, 0);
        QVERIFY(editor.hasDraftChanges());
        disk = RatingProfileLibrary::load(path, nullptr); QVERIFY(disk);
        QCOMPARE(disk->profiles().value("personal").name, QStringLiteral("Changed elsewhere"));
        QCOMPARE(disk->profiles().value("personal").attributes.value("Pace").weight, 25.0);
    }
    void discardAndRestoreStayLocal() {
        QTemporaryDir directory; const auto path = directory.filePath("profiles.json");
        RatingProfileLibrary library; QVERIFY(library.insert(profile()).isEmpty());
        auto changed = profile(); changed.attributes["Pace"].weight = 25;
        QVERIFY(library.update(changed, RatingProfileLibrary::documentRevision(profile())).isEmpty());
        QVERIFY(library.save(path).isEmpty());
        RatingProfileEditor editor(path, "fm27");
        auto *attributes = editor.findChild<QListWidget *>(QStringLiteral("ratingAttributes"));
        QVERIFY(attributes); QCOMPARE(attributes->count(), 47);
        for (int i = 0; i < attributes->count(); ++i)
            if (attributes->item(i)->data(Qt::UserRole).toString() == QStringLiteral("Pace")) attributes->setCurrentRow(i);
        clickAction(editor, 2); QVERIFY(editor.hasDraftChanges());
        auto *curve = editor.findChild<RatingCurveEditor *>(); QVERIFY(curve);
        QCOMPARE(curve->curve().weight, 19.0);
        QCOMPARE(RatingProfileLibrary::load(path, nullptr)->profiles().value("personal").attributes.value("Pace").weight, 25.0);
        QTimer::singleShot(0, [] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (message) message->button(QMessageBox::Discard)->click();
        });
        clickAction(editor, 3); QVERIFY(!editor.hasDraftChanges());
        QCOMPARE(curve->curve().weight, 25.0);
    }
    void rendersProfileEditor_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("width");
        for (const auto &language : {QStringLiteral("en"), QStringLiteral("de")})
            for (const auto &mode : {QStringLiteral("day"), QStringLiteral("night")})
                for (int width : {800, 1000})
                    QTest::newRow(qPrintable(language + "-" + mode + QString::number(width))) << language << mode << width;
    }
    void rendersProfileEditor() {
        QFETCH(QString, language); QFETCH(QString, mode); QFETCH(int, width);
        QTranslator translator;
        if (language == QStringLiteral("en")) {
            QVERIFY(translator.load(QStringLiteral(EDITOR_QM)));
            QApplication::installTranslator(&translator);
        }
        QTemporaryDir directory; const auto path = directory.filePath("profiles.json");
        RatingProfileLibrary library; QVERIFY(library.insert(profile()).isEmpty()); QVERIFY(library.save(path).isEmpty());
        RatingProfileEditor editor(path, "fm27");
        auto *attributes = editor.findChild<QListWidget *>(QStringLiteral("ratingAttributes"));
        QVERIFY(attributes); QCOMPARE(attributes->count(), 47);
        for (int i = 0; i < attributes->count(); ++i)
            if (attributes->item(i)->data(Qt::UserRole).toString() == QStringLiteral("Pace")) attributes->setCurrentRow(i); AppConfig config(directory.filePath("settings.ini")); ThemeManager theme(config);
        theme.setMode(mode); theme.apply();
        QPalette palette; palette.setColor(QPalette::Base, theme.surface());
        palette.setColor(QPalette::Text, theme.text());
        auto grid = theme.text(); grid.setAlpha(45); palette.setColor(QPalette::Mid, grid);
        palette.setColor(QPalette::Highlight, theme.accentUi()); editor.setChartPalette(palette);
        auto background = editor.palette(); background.setColor(QPalette::Window, theme.surface());
        editor.setPalette(background); editor.setAutoFillBackground(true);
        editor.resize(width, 640); editor.show(); QTest::qWait(20);
        const auto screenshot = qEnvironmentVariable("FM_PROFILE_EDITOR_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(editor.grab().save(screenshot + "-" + language + "-" + mode + QString::number(width) + ".png"));
        QVERIFY(editor.width() <= width);
        QCOMPARE(editor.findChild<QPushButton *>(QStringLiteral("profileAction0"))->text(),
                 language == QStringLiteral("en") ? QStringLiteral("Save profile") : QStringLiteral("Profil speichern"));
        QCOMPARE(editor.findChild<QLineEdit *>(QStringLiteral("ratingProfileName"))->text(), QStringLiteral("Personal"));
        QApplication::removeTranslator(&translator);
        qApp->setStyleSheet({});
    }
};
QTEST_MAIN(TestRatingProfileEditor)
#include "test_ratingprofileeditor.moc"
