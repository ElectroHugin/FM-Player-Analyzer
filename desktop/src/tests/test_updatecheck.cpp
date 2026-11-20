#include "core/UpdateCheck.h"

#include <QtTest>

using namespace fm;

namespace {

QByteArray release(const char *tag, bool prerelease, bool draft = false)
{
    return QStringLiteral("{\"tag_name\":\"%1\",\"name\":\"%1\",\"prerelease\":%2,"
                          "\"draft\":%3,\"body\":\"- notes for %1\","
                          "\"html_url\":\"https://example.invalid/%1\"}")
        .arg(QString::fromLatin1(tag), prerelease ? QStringLiteral("true") : QStringLiteral("false"),
             draft ? QStringLiteral("true") : QStringLiteral("false"))
        .toUtf8();
}

QByteArray list(std::initializer_list<QByteArray> releases)
{
    QByteArrayList parts(releases);
    return "[" + parts.join(',') + "]";
}

} // namespace

class TestUpdateCheck : public QObject
{
    Q_OBJECT

private slots:
    void changelogNotesPickTheNewVersions()
    {
        const QString changelog = QStringLiteral(
            "# Changelog\n\n## Unreleased\n\n- coming\n\n## 2.3.2 – 2026-11-21\n\n- two\n\n"
            "## 2.3.1 – 2026-11-20\n\n- one\n\n## 2.3.0 – 2026-11-15\n\n**Block**\n\n- zero\n");
        // Unknown previous version: only the running one.
        QCOMPARE(changelogNotes(changelog, QString(), QStringLiteral("2.3.1")),
                 QStringLiteral("- one"));
        // Update over several versions: each with its heading, newest first.
        QCOMPARE(changelogNotes(changelog, QStringLiteral("2.3.0"), QStringLiteral("2.3.2")),
                 QStringLiteral("### 2.3.2 – 2026-11-21\n\n- two\n\n### 2.3.1 – 2026-11-20\n\n- one"));
        // A test build shows the collected changes of the coming version.
        QCOMPARE(changelogNotes(changelog, QStringLiteral("2.3.1"), QStringLiteral("2.3.2-test.1")),
                 QStringLiteral("- coming"));
        QCOMPARE(changelogNotes(changelog, QString(), QStringLiteral("2.3.2-test.1")),
                 QStringLiteral("- coming"));
        // From a test build to its final release.
        QCOMPARE(changelogNotes(changelog, QStringLiteral("2.3.2-test.3"), QStringLiteral("2.3.2")),
                 QStringLiteral("- two"));
        // Nothing new, unknown version, no changelog.
        QVERIFY(changelogNotes(changelog, QStringLiteral("2.3.2"), QStringLiteral("2.3.2")).isEmpty());
        QVERIFY(changelogNotes(changelog, QString(), QStringLiteral("2.4.0")).isEmpty());
        QVERIFY(changelogNotes(QString(), QString(), QStringLiteral("2.3.1")).isEmpty());
        // Sub-headings inside a section stay.
        QVERIFY(changelogNotes(changelog, QString(), QStringLiteral("2.3.0")).contains(QStringLiteral("**Block**")));
    }

    void parsesVersions()
    {
        const auto plain = AppVersion::parse(QStringLiteral("v2.3.1"));
        QVERIFY(plain);
        QCOMPARE(plain->major, 2);
        QCOMPARE(plain->minor, 3);
        QCOMPARE(plain->patch, 1);
        QVERIFY(!plain->isPreRelease());
        const auto test = AppVersion::parse(QStringLiteral("2.3.1-test.12"));
        QVERIFY(test);
        QVERIFY(test->isPreRelease());
        QCOMPARE(test->preReleaseNumber, 12);
        QCOMPARE(test->toString(), QStringLiteral("2.3.1-test.12"));
        QVERIFY(!AppVersion::parse(QStringLiteral("latest")));
        QVERIFY(!AppVersion::parse(QString()));
    }

    void ordersVersions()
    {
        const auto v = [](const char *text) { return *AppVersion::parse(QString::fromLatin1(text)); };
        QVERIFY(compareVersions(v("2.3.0"), v("2.3.1")) < 0);
        QVERIFY(compareVersions(v("2.10.0"), v("2.9.9")) > 0);
        QVERIFY(compareVersions(v("2.3.1-test.2"), v("2.3.1")) < 0);
        QVERIFY(compareVersions(v("2.3.1-test.2"), v("2.3.1-test.10")) < 0);
        QVERIFY(compareVersions(v("2.3.1"), v("2.3.2-test.1")) < 0);
        QCOMPARE(compareVersions(v("v2.3.1"), v("2.3.1")), 0);
    }

    void stableChannelSkipsTestBuildsAndDrafts()
    {
        const QByteArray json = list({release("v2.5.0", false, true), release("v2.4.1-test.1", true),
                                      release("v2.4.0", false), release("v2.3.1", false)});
        const auto found = newestReleaseAfter(json, QStringLiteral("2.3.1"), false);
        QVERIFY(found);
        QCOMPARE(found->version, QStringLiteral("2.4.0"));
        QCOMPARE(found->pageUrl, QStringLiteral("https://example.invalid/v2.4.0"));
        QCOMPARE(found->notes, QStringLiteral("- notes for v2.4.0"));
        QVERIFY(!found->preRelease);
    }

    void testChannelIncludesTestBuilds()
    {
        const QByteArray json = list({release("v2.4.1-test.1", true), release("v2.4.0", false)});
        const auto found = newestReleaseAfter(json, QStringLiteral("2.3.1"), true);
        QVERIFY(found);
        QCOMPARE(found->version, QStringLiteral("2.4.1-test.1"));
        QVERIFY(found->preRelease);
    }

    void testBuildUserGetsTheFinalRelease()
    {
        // Running a test build, test builds not wanted: the final release of the
        // same version is newer.
        const QByteArray json = list({release("v2.3.1", false), release("v2.3.1-test.2", true)});
        const auto found = newestReleaseAfter(json, QStringLiteral("2.3.1-test.2"), false);
        QVERIFY(found);
        QCOMPARE(found->version, QStringLiteral("2.3.1"));
    }

    void upToDateAndBadReplies()
    {
        QString error;
        const QByteArray json = list({release("v2.3.1", false), release("v2.3.0", false)});
        QVERIFY(!newestReleaseAfter(json, QStringLiteral("2.3.1"), true, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!newestReleaseAfter(QByteArrayLiteral("{\"message\":\"rate limit\"}"),
                                    QStringLiteral("2.3.1"), false, &error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!newestReleaseAfter(QByteArrayLiteral("not json"), QStringLiteral("2.3.1"), false,
                                    &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUpdateCheck)
#include "test_updatecheck.moc"
