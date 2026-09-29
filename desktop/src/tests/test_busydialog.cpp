#include "widgets/BusyProgressDialog.h"

#include <QPointer>
#include <QtTest>

using namespace fm;

// Backlog #28: the progress dialog shown during imports/recalcs must not be
// dismissable while the worker runs — otherwise the main window becomes usable
// underneath a live worker (second import, engine reload, app shutdown).
class TestBusyDialog : public QObject
{
    Q_OBJECT

private slots:
    void escapeDoesNotCloseWhileBusy()
    {
        BusyProgressDialog dialog(QStringLiteral("busy"), nullptr);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        QTest::keyClick(&dialog, Qt::Key_Escape);
        QVERIFY(dialog.isVisible());
        QVERIFY(!dialog.wasCanceled());
    }

    void closeRequestIgnoredWhileBusy()
    {
        BusyProgressDialog dialog(QStringLiteral("busy"), nullptr);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        QVERIFY(!dialog.close()); // Alt+F4 / system menu path
        QVERIFY(dialog.isVisible());
        dialog.reject();
        QVERIFY(dialog.isVisible());
    }

    void reachingMaximumDoesNotHide()
    {
        // Workers report their last step before the final database write.
        BusyProgressDialog dialog(QStringLiteral("busy"), nullptr);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        dialog.setValue(dialog.maximum());
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.value(), dialog.maximum());
    }

    void finishClosesAndDeletes()
    {
        QPointer<BusyProgressDialog> dialog = new BusyProgressDialog(QStringLiteral("busy"),
                                                                     nullptr);
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog));

        dialog->finish();
        QVERIFY(dialog->isFinished());
        QVERIFY(!dialog->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
    }
};

QTEST_MAIN(TestBusyDialog)
#include "test_busydialog.moc"
