#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/Database.h"
#include "core/Definitions.h"
#include "core/GameTerms.h"
#include "core/GameLanguage.h"
#include "core/HtmlImporter.h"
#include "core/ImportTable.h"

using namespace fm;

namespace {

const QString kFm27 = QStringLiteral("fm27");

// One player as the FM View Exporter writes him in each game language (texts
// as the game writes them; the player is made up).
struct Sample {
    QString language;
    QStringList headers;
    QStringList row;
};

QList<Sample> samples()
{
    return {
        {QStringLiteral("en"),
         {QStringLiteral("Unique ID"), QStringLiteral("Name"), QStringLiteral("Position"),
          QStringLiteral("Age"), QStringLiteral("Club"), QStringLiteral("Nation"),
          QStringLiteral("Height"), QStringLiteral("Preferred Foot"), QStringLiteral("Left Foot"),
          QStringLiteral("Right Foot"), QStringLiteral("Media Handling"),
          QStringLiteral("Playing Time"), QStringLiteral("Wage"), QStringLiteral("Transfer Value"),
          QStringLiteral("Rating"), QStringLiteral("Personality"), QStringLiteral("Pace"),
          QStringLiteral("Anticipation")},
         {QStringLiteral("9000001"), QStringLiteral("Test Player"),
          QStringLiteral("D (RL), WB (R), DM, M (C)"), QStringLiteral("24"),
          QStringLiteral("FC Test"), QStringLiteral("Germany"), QStringLiteral("182 cm"),
          QStringLiteral("Left-Footed Only"), QStringLiteral("Very Strong"), QStringLiteral("Weak"),
          QStringLiteral("Media-friendly, Reserved"), QStringLiteral("Important Player"),
          QStringLiteral("€7.25K - €8.75K p/w"), QStringLiteral("€6.8M - €11M"),
          QStringLiteral("6.80"), QStringLiteral("Fairly Professional"), QStringLiteral("14"),
          QStringLiteral("13")}},
        {QStringLiteral("de"),
         {QStringLiteral("Unique ID"), QStringLiteral("Name"), QStringLiteral("Position"),
          QStringLiteral("Alter"), QStringLiteral("Verein"), QStringLiteral("Nation"),
          QString::fromUtf8("Größe"), QString::fromUtf8("Starker Fuß"),
          QString::fromUtf8("Linker Fuß"), QString::fromUtf8("Rechter Fuß"),
          QStringLiteral("Medienumgang"), QStringLiteral("Einsatzzeiten"), QStringLiteral("Gehalt"),
          QStringLiteral("Transferwert"), QStringLiteral("Wertung"),
          QString::fromUtf8("Persönlichkeit"), QStringLiteral("Schnelligkeit"),
          QStringLiteral("Antizipation")},
         {QStringLiteral("9000001"), QStringLiteral("Test Player"),
          QStringLiteral("V (RL), FV (R), DM, M (Z)"), QStringLiteral("24"),
          QStringLiteral("FC Test"), QStringLiteral("Deutschland"), QStringLiteral("182 cm"),
          QStringLiteral("Nur Links"), QStringLiteral("Sehr stark"), QStringLiteral("Schwach"),
          QStringLiteral("Medienfreundlich, Verschlossen"), QStringLiteral("Schlüsselspieler"),
          QString::fromUtf8("€7.25K – €8.75K /W."), QStringLiteral("€6.8Mio. - €11Mio."),
          QStringLiteral("6,80"), QStringLiteral("Relativ professionell"), QStringLiteral("14"),
          QStringLiteral("13")}},
        {QStringLiteral("fr"),
         {QStringLiteral("Unique ID"), QStringLiteral("Nom"), QStringLiteral("Position"),
          QString::fromUtf8("Âge"), QStringLiteral("Club"), QStringLiteral("Nation"),
          QStringLiteral("Taille"), QStringLiteral("Meilleur pied"), QStringLiteral("Pied gauche"),
          QStringLiteral("Pied droit"), QString::fromUtf8("Rapports avec les médias"),
          QStringLiteral("Temps de jeu"), QStringLiteral("Salaire"),
          QStringLiteral("Montant transfert"), QStringLiteral("Note"),
          QString::fromUtf8("Personnalité"), QStringLiteral("Vitesse"),
          QStringLiteral("Anticipation")},
         {QStringLiteral("9000001"), QStringLiteral("Test Player"),
          QStringLiteral("D (DG), AL (D), MD, M (C)"), QStringLiteral("24"),
          QStringLiteral("FC Test"), QStringLiteral("Allemagne"), QStringLiteral("182 cm"),
          QStringLiteral("Gaucher uniquement"), QString::fromUtf8("Très fort"),
          QStringLiteral("Faible"), QString::fromUtf8("Apprécié des médias, Réservé"),
          QStringLiteral("Joueur important"), QStringLiteral("€7.25K - €8.75K p/s"),
          QStringLiteral("€6.8M - €11M"), QStringLiteral("6,80"),
          QStringLiteral("Assez professionnel"), QStringLiteral("14"), QStringLiteral("13")}},
        {QStringLiteral("it"),
         {QStringLiteral("Unique ID"), QStringLiteral("Nome"), QStringLiteral("Posizione"),
          QString::fromUtf8("Età"), QString::fromUtf8("Società"), QStringLiteral("Nazione"),
          QStringLiteral("Altezza"), QStringLiteral("Piede preferito"),
          QStringLiteral("Piede sinistro"), QStringLiteral("Piede destro"),
          QStringLiteral("Gestione dei media"), QStringLiteral("Minutaggio"),
          QStringLiteral("Stipendio"), QStringLiteral("Costo trasferimento"),
          QStringLiteral("Valutazione"), QString::fromUtf8("Personalità"),
          QString::fromUtf8("Velocità"), QStringLiteral("Intuito")},
         {QStringLiteral("9000001"), QStringLiteral("Test Player"),
          QStringLiteral("D (DS), Est (D), M, C (C)"), QStringLiteral("24"),
          QStringLiteral("FC Test"), QStringLiteral("Germania"), QStringLiteral("182 cm"),
          QStringLiteral("Solo sinistro"), QStringLiteral("Molto forte"), QStringLiteral("Debole"),
          QStringLiteral("Amico dei media, Riservato"), QStringLiteral("Giocatore importante"),
          QStringLiteral("(€7.25m - €8.75m p/s)"), QStringLiteral("€6.8M - €11M"),
          QStringLiteral("6,80"), QStringLiteral("Abbastanza professionale"), QStringLiteral("14"),
          QStringLiteral("13")}},
        {QStringLiteral("zh_CN"),
         {QStringLiteral("Unique ID"), QString::fromUtf8("名字"), QString::fromUtf8("位置"),
          QString::fromUtf8("年龄"), QString::fromUtf8("俱乐部"), QString::fromUtf8("国家或地区"),
          QString::fromUtf8("身高"), QString::fromUtf8("惯用脚"), QString::fromUtf8("左脚"),
          QString::fromUtf8("右脚"), QString::fromUtf8("公关风格"), QString::fromUtf8("上场时间"),
          QString::fromUtf8("工资"), QString::fromUtf8("转会身价"), QString::fromUtf8("评分"),
          QString::fromUtf8("个性"), QString::fromUtf8("速度"), QString::fromUtf8("预判")},
         {QStringLiteral("9000001"), QStringLiteral("Test Player"),
          QString::fromUtf8("后卫 (右左), 翼卫 (右), 后腰, 中场 (中)"), QStringLiteral("24"),
          QStringLiteral("FC Test"), QString::fromUtf8("德国"), QString::fromUtf8("182厘米"),
          QString::fromUtf8("只会左脚"), QString::fromUtf8("精准"), QString::fromUtf8("欠佳"),
          QString::fromUtf8("亲善媒体, 不苟言笑"), QString::fromUtf8("重要球员"),
          QString::fromUtf8("€7250 - €8750周薪"), QString::fromUtf8("€680万 - €1100万"),
          QStringLiteral("6.80"), QString::fromUtf8("相当专业"), QStringLiteral("14"),
          QStringLiteral("13")}},
        {QStringLiteral("ru"),
         {QStringLiteral("Unique ID"), QString::fromUtf8("Имя"), QString::fromUtf8("Позиции"),
          QString::fromUtf8("Возраст"), QString::fromUtf8("Клуб"), QString::fromUtf8("Страна"),
          QString::fromUtf8("Рост"), QString::fromUtf8("«Рабочая» нога"),
          QString::fromUtf8("Левая нога"), QString::fromUtf8("Правая нога"),
          QString::fromUtf8("Общение с прессой"), QString::fromUtf8("Игровое время"),
          QString::fromUtf8("Зарплата"), QString::fromUtf8("Сумма трансфера"),
          QString::fromUtf8("Оценка"), QString::fromUtf8("Характер"), QString::fromUtf8("Скорость"),
          QString::fromUtf8("Интуиция")},
         {QStringLiteral("9000001"), QStringLiteral("Test Player"),
          QString::fromUtf8("З (ПЛ), КЗ (П), ОП, П (Ц)"), QStringLiteral("24"),
          QStringLiteral("FC Test"), QString::fromUtf8("Германия"), QString::fromUtf8("182 см"),
          QString::fromUtf8("Только левоногий"), QString::fromUtf8("Очень сильная"),
          QString::fromUtf8("Слабая"), QString::fromUtf8("Открытый для прессы, Немногословный"),
          QString::fromUtf8("Важный игрок"), QString::fromUtf8("€7.25К - €8.75К в/н"),
          QString::fromUtf8("€6.8М - €11М"), QStringLiteral("6,80"),
          QString::fromUtf8("Достаточно профессиональный"), QStringLiteral("14"),
          QStringLiteral("13")}},
    };
}

QString csvLine(const QStringList &cells)
{
    QStringList out;
    for (QString cell : cells) {
        cell.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        out << QLatin1Char('"') + cell + QLatin1Char('"');
    }
    return out.join(QLatin1Char(',')) + QLatin1Char('\n');
}

} // namespace

class TestGameLanguage : public QObject
{
    Q_OBJECT

private slots:
    void detectsEveryLanguage()
    {
        for (const Sample &sample : samples())
            QCOMPARE(GameLanguage::detect(sample.headers, kFm27), sample.language);
    }

    void tooFewHeadersDoNotDecide()
    {
        QCOMPARE(GameLanguage::detect({QStringLiteral("Unique ID"), QStringLiteral("Position")}, kFm27),
                 QString());
        // FM24 exports are English only.
        QCOMPARE(GameLanguage::detect(samples().at(1).headers, QStringLiteral("fm24")), QString());
    }

    void resolveUsesTheHeadersFirst()
    {
        QCOMPARE(GameLanguage::resolve(QStringLiteral("auto"), QString()), QStringLiteral("en"));
        QCOMPARE(GameLanguage::resolve(QString(), QString()), QStringLiteral("en"));
        QCOMPARE(GameLanguage::resolve(QStringLiteral("fr"), QString()), QStringLiteral("fr"));
        QCOMPARE(GameLanguage::resolve(QStringLiteral("fr"), QStringLiteral("de")), QStringLiteral("de"));
    }

    void everyLanguageReadsLikeEnglish()
    {
        const Sample english = samples().at(0);
        for (const Sample &sample : samples()) {
            ImportTable table;
            table.headers = sample.headers;
            table.rows = {sample.row};
            GameLanguage::toEnglish(&table, sample.language, kFm27);
            for (int c = 0; c < english.headers.size(); ++c) {
                const QString context = sample.language + QLatin1Char(' ') + english.headers.at(c);
                QVERIFY2(table.headers.at(c) == english.headers.at(c),
                         qPrintable(context + QStringLiteral(" header: ") + table.headers.at(c)));
                QString expected = english.row.at(c);
                if (english.headers.at(c) == QLatin1String("Nation") && sample.language != QLatin1String("en"))
                    expected = QStringLiteral("Germany");
                QVERIFY2(table.rows.at(0).at(c) == expected,
                         qPrintable(context + QStringLiteral(": ") + table.rows.at(0).at(c)));
            }
        }
    }

    void money()
    {
        QCOMPARE(GameLanguage::moneyToEnglish(QString::fromUtf8("€2.7Mio. - €5.8Mio."), QStringLiteral("de")),
                 QStringLiteral("€2.7M - €5.8M"));
        QCOMPARE(GameLanguage::moneyToEnglish(QStringLiteral("€190m - €1.9M"), QStringLiteral("it")),
                 QStringLiteral("€190K - €1.9M"));
        QCOMPARE(GameLanguage::moneyToEnglish(QString::fromUtf8("€1.2亿 - €1.43亿"), QStringLiteral("zh_CN")),
                 QStringLiteral("€120M - €143M"));
        QCOMPARE(GameLanguage::moneyToEnglish(QString::fromUtf8("€7.7万/周"), QStringLiteral("zh_CN")),
                 QStringLiteral("€77K p/w"));
        QCOMPARE(GameLanguage::moneyToEnglish(QString::fromUtf8("€230К/нед"), QStringLiteral("ru")),
                 QStringLiteral("€230K p/w"));
        // Unknown texts stay as they are.
        QCOMPARE(GameLanguage::moneyToEnglish(QStringLiteral("Unbekannt"), QStringLiteral("de")),
                 QStringLiteral("Unbekannt"));
        QCOMPARE(GameLanguage::moneyToEnglish(QStringLiteral("-"), QStringLiteral("de")), QStringLiteral("-"));
    }

    void positions()
    {
        QCOMPARE(GameLanguage::positionsToEnglish(QStringLiteral("TW"), QStringLiteral("de")),
                 QStringLiteral("GK"));
        QCOMPARE(GameLanguage::positionsToEnglish(QStringLiteral("V/FV (L), OM (RLZ)"), QStringLiteral("de")),
                 QStringLiteral("D/WB (L), AM (RLC)"));
        // Italian "M" is the defensive midfielder, "C" the midfielder.
        QCOMPARE(GameLanguage::positionsToEnglish(QStringLiteral("M, C (C), T (S)"), QStringLiteral("it")),
                 QStringLiteral("DM, M (C), AM (L)"));
        // Russian "П": midfielder as a position, right as a side.
        QCOMPARE(GameLanguage::positionsToEnglish(QString::fromUtf8("П (П)"), QStringLiteral("ru")),
                 QStringLiteral("M (R)"));
    }

    void gameTermsFollowTheUiLanguage()
    {
        // Display only: values stay English in the app, the UI shows the
        // game's wording of the UI language.
        Definitions::setDisplayLanguage(QStringLiteral("de"));
        QCOMPARE(GameTerms::text(QStringLiteral("personality"), QStringLiteral("Driven")),
                 QStringLiteral("Erfolgshungrig"));
        QCOMPARE(GameTerms::text(QStringLiteral("foot_strength"), QStringLiteral("Fairly Strong")),
                 QStringLiteral("Gut"));
        QCOMPARE(GameTerms::text(QStringLiteral("personality"), QStringLiteral("Unknown")),
                 QStringLiteral("Unknown"));
        QCOMPARE(GameTerms::attribute(QStringLiteral("Teamwork")), QStringLiteral("Teamwork"));
        QCOMPARE(GameTerms::attribute(QStringLiteral("Acceleration")), QStringLiteral("Antritt"));
        QCOMPARE(GameTerms::attribute(QStringLiteral("One vs One")), QStringLiteral("Eins gegen Eins"));
        QCOMPARE(GameTerms::positions(QStringLiteral("D (LC), WB (L)")),
                 QStringLiteral("V (LZ), FV (L)"));
        QCOMPARE(GameTerms::positions(QStringLiteral("D/WB/M (R), AM (C), GK")),
                 QStringLiteral("V/FV/M (R), OM (Z), TW"));
        Definitions::setDisplayLanguage(QStringLiteral("en"));
        QCOMPARE(GameTerms::positions(QStringLiteral("D (LC)")), QStringLiteral("D (LC)"));
        QCOMPARE(GameTerms::text(QStringLiteral("personality"), QStringLiteral("Driven")),
                 QStringLiteral("Driven"));
    }

    void germanCsvImport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Sample german = samples().at(1);
        const QString path = dir.filePath(QStringLiteral("squad_de.csv"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("\xEF\xBB\xBF") + csvLine(german.headers).toUtf8() + csvLine(german.row).toUtf8());
        file.close();

        Database db(QStringLiteral("game_language_test"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.db"))));
        // The setting says French, the headers say German: the headers win,
        // and the result tells the UI so it can suggest the right setting.
        const ImportResult result = HtmlImporter::importFile(path, db, {}, {}, kFm27, {}, nullptr, {},
                                                             QStringLiteral("fr"));
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(result.gameLanguage, QStringLiteral("de"));
        QCOMPARE(result.detectedGameLanguage, QStringLiteral("de"));
        QVERIFY2(result.unknownColumns.isEmpty(), qPrintable(result.unknownColumns.join(QLatin1Char(','))));

        const std::vector<Player> players = db.loadPlayers();
        QCOMPARE(players.size(), size_t(1));
        const Player &p = players.front();
        QCOMPARE(p.positionRaw, QStringLiteral("D (RL), WB (R), DM, M (C)"));
        QCOMPARE(p.preferredFoot, QStringLiteral("Left-Footed Only"));
        QCOMPARE(p.leftFoot, QStringLiteral("Very Strong"));
        QCOMPARE(p.rightFoot, QStringLiteral("Weak"));
        QCOMPARE(p.personality, QStringLiteral("Fairly Professional"));
        QCOMPARE(p.agreedPlayingTime, QStringLiteral("Important Player"));
        QCOMPARE(p.nationality, QStringLiteral("GER"));
        QCOMPARE(p.heightCm, 182);
        QCOMPARE(p.averageRating, 6.8);
        QCOMPARE(p.transferValue, 6.8e6);
    }
};

QTEST_GUILESS_MAIN(TestGameLanguage)
#include "test_gamelanguage.moc"
