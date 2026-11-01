# FM27 – Kader- und Registrierungsregeln

<!-- Quelle der Meldelisten-Regeln für FM27 (core/Registration.cpp, quotaFor). -->

> **Was der Analyzer davon umsetzt:** Kadergröße, Mindestzahl im Land ausgebildeter
> Spieler und Mindestzahl im Verein ausgebildeter Spieler (als Höchstzahl der übrigen
> Plätze) sowie die U21-Befreiung, wo sie belegt ist (Premier League, Ukraine). Ligen
> mit 99 Plätzen (Bundesliga, Ligue 1, Österreich) haben keine Meldeliste. Nicht
> umgesetzt: Ausländer- und Nicht-EU-Grenzen, Startelf-Grenzen, Gehaltsgrenzen,
> Torhüter-Regeln der Süper Lig und Jugend-Ausnahmen, die von der Nationalität abhängen.
> Weitere Ligen bitte per GitHub-Issue mit Screenshot der *Squad Selection Rules*.


## Zweck und Quellen

Diese Sammlung dokumentiert die Regeln aus vom Nutzer bereitgestellten Football-Manager-27-Screenshots. Erfasst sind alle fünf großen europäischen Ligen und sechs weitere Ligen.

- Quelle: FM27-Spielansichten „Squad Selection Rules“; für Österreich eine Registrierungsansicht mit Kadergrenzen.
- Die genaue Saison und Spielversion der Screenshots wurden nicht festgehalten. Die Regeln sind daher als Belege für die gezeigten Spielstände zu verstehen, nicht als Zusicherung für jede Saison oder Datenbank.
- Reale Verbandsregeln wurden in der Unterhaltung recherchiert, sind aber NICHT die Grundlage der nachfolgenden Werte.
- „Dokumentiert“ bedeutet: Die sichtbaren Angaben wurden übertragen. Nicht alle Sonderfälle sind damit abschließend geklärt.
- Geburtsstichtage für Jugendkategorien werden auf ausdrücklichen Wunsch des Nutzers nicht benötigt.
- Fehlende Angaben bedeuten „nicht belegt“, nicht automatisch „keine Einschränkung“.
- Nationalität, Status als einheimischer Spieler und Ausbildungsstatus sind getrennte Kriterien. „Nicht-EU“ und „ausländisch“ dürfen nicht gleichgesetzt werden.
- Im Verein ausgebildete Spieler sind Teil der im Land ausgebildeten Spieler; die Quoten werden nicht addiert.
- Bei Ausbildungsquoten können eventuell unbesetzte Plätze die zulässige Kadergröße reduzieren. Die vollständige Behandlung dieses Falls ist durch die Screenshots nicht belegt; Mindestquoten nicht ungeprüft als absolutes Registrierungsverbot implementieren.
- Rot/grün markierte Prüfungen waren teilweise widersprüchlich. Maßgeblich für diese Sammlung sind die angezeigten Regeltexte und Grenzwerte, nicht die Farben oder aktuellen Beispielkader.

## 1. Deutschland – Bundesliga

Status: Vom Nutzer bestätigt und gesichert.

| Kriterium | Vorgabe |
|---|---|
| Kadergröße | Mindestens 15, maximal 99 |
| In Deutschland ausgebildet | Mindestens 8 |
| Im eigenen Verein ausgebildet | Mindestens 4 |
| Deutsche Spieler | Mindestens 12 |
| Definition Landesausbildung | 3 Jahre in Deutschland zwischen dem 15. und 21. Geburtstag |

Die detaillierte Ausbildungsdauer für die Vereinsquote und Jugend-Ausnahmen sind in dieser Ansicht nicht separat angegeben. Die 99 ist eine belegte FM-Anzeige, keine hier belegte reale Ligagrenze.

## 2. England – Premier League

Status: Screenshot dokumentiert; U21-Ausnahme zusätzlich ausdrücklich vom Nutzer bestätigt.

| Kriterium | Vorgabe |
|---|---|
| Registrierter Kader | Mindestens 15, maximal 25 |
| In England ausgebildet | Angezeigte Mindestquote: 8 |
| Definition Landesausbildung | 3 Jahre vor dem 21. Geburtstag |
| U21 | Müssen nicht registriert werden; außerhalb der 25er-Liste |

Der Screenshot zeigt 20 registrierte Spieler und nur 7 lokal ausgebildete Spieler mit grünem Haken. Das ist mit einer Reduzierung nutzbarer Kaderplätze bei fehlenden Homegrown-Spielern vereinbar, belegt aber nicht allein die vollständige Berechnungslogik. Die Spielansicht sagt „England“; eine Erweiterung auf Wales wurde aus diesem Screenshot nicht abgeleitet.

## 3. Spanien – LaLiga

Status: Screenshot dokumentiert; Jugend-Ausnahme und Begriff „anderes Team“ teilweise offen.

| Kriterium | Vorgabe |
|---|---|
| Registrierter Kader | Mindestens 15, maximal 25 |
| Feldspieler | Maximal 23 |
| Nicht-EU-Spieler | Maximal 3 nach Einstufung des Spiels |
| Startelf | Maximal 4 Spieler, die für ein anderes Team registriert sind |
| Gesamte Kadergehälter | Unter 6.500.000 EUR pro Woche im gezeigten Fall |
| Jugendstatus | Statusmeldung: „No longer qualifies as an U19 EU player“ |

Einordnung:

- Die Gehaltsgrenze ist als Wert des gezeigten Vereins/Spielstands zu behandeln, NICHT als ligaweit einheitlicher Wert.
- „Anderes Team“ ist wörtlich übertragen. Welche Mannschaften darunter fallen, ist nicht belegt; nicht ungeprüft als Einschränkung für frühere Transfers interpretieren.
- Die Statusmeldung weist auf eine Ausnahme für U19-EU-Spieler hin. Vollständige Voraussetzungen und Reichweite sind nicht belegt.
- Der Screenshot zeigt 2 Nicht-EU-Spieler bei maximal 3, dennoch rot. Daraus folgt keine zusätzliche Regel.

## 4. Italien – Serie A

Status: Screenshot dokumentiert.

| Kriterium | Vorgabe |
|---|---|
| Registrierter Kader | Mindestens 15, maximal 25 |
| In Italien ausgebildet | Mindestens 8 |
| Im eigenen Verein ausgebildet | Mindestens 4 |
| Definition Vereinsausbildung | 3 Jahre zwischen dem 15. und 21. Geburtstag |
| Besonderheit Vereinsausbildung | Laut Spiel unabhängig von zwischenzeitlichen Leihen zu anderen Vereinen |

Die Definition der Landesausbildung wird nicht separat erläutert. Eine Jugend-Ausnahme ist hier nicht angegeben. Der Beispielkader enthält 23 Spieler und nur 2 vereinsausgebildete Spieler mit grünem Haken; die Behandlung fehlender Ausbildungsplätze muss bei einer Umsetzung berücksichtigt bzw. gesondert geprüft werden.

## 5. Frankreich – Ligue 1

Status: Screenshot dokumentiert.

| Kriterium | Vorgabe |
|---|---|
| Kadergröße | Mindestens 15, maximal 99 |
| Nicht-EU-Spieler | Maximal 4 nach Einstufung des Spiels |

Keine Ausbildungsquote oder Jugend-Ausnahme in dieser Ansicht angegeben. Welche Nationalitäten oder Abkommen FM für die Nicht-EU-Einstufung berücksichtigt, ist hier nicht dokumentiert. Die Anzeige ist trotz 1 Nicht-EU-Spieler bei maximal 4 rot; keine weitere Regel daraus ableiten.

## 6. Portugal – Primeira Liga / Liga Portugal

Status: Screenshot für einen Verein MIT B-Team dokumentiert; Variante ohne B-Team unvollständig.

| Kriterium | Vorgabe |
|---|---|
| Kadergröße mit B-Team | Mindestens 15, maximal 68 |
| In Portugal ausgebildet – allgemeine angezeigte Quote | Mindestens 8 |
| In Portugal ausgebildet – Vereine mit B-Team | Mindestens 10 |
| Definition Landesausbildung | 3 Jahre zwischen dem 15. und 21. Geburtstag |
| Spieler im ersten Seniorenjahr | Mindestens 7 („first-year senior players“) |
| U23-Spieler bei Vereinen mit B-Team | Maximal 31 |

Einordnung:

- Die 8er- und 10er-Quote werden nicht addiert; für den gezeigten Verein mit B-Team gilt die 10er-Quote.
- Der genaue Umfang des registrierten Kaders über erste Mannschaft/B-Team hinweg ist nicht erläutert.
- Kadergrenzen für Vereine ohne B-Team sind nicht belegt.
- Die genaue Definition von „first-year senior players“ ist offen. Nicht automatisch mit U21 oder U23 gleichsetzen.

## 7. Schweiz – Super League

Status: Screenshot dokumentiert.

| Kriterium | Vorgabe |
|---|---|
| Registrierter Kader | Mindestens 15, maximal 25 |
| In der Schweiz ausgebildet | Mindestens 8 |
| Nicht-EU-Spieler im Kader | Maximal 10 nach Einstufung des Spiels |
| Nicht-EU-Spieler in der Startelf | Maximal 5 |

Die genaue Definition der Landesausbildung, eine eigene Vereinsausbildungsquote und Jugend-Ausnahmen sind nicht angegeben. Die rote Prüfung trotz 3 Nicht-EU-Spielern im Kader bei maximal 10 wird nicht als zusätzliche Regel gewertet.

## 8. Österreich – Bundesliga

Status: Registrierungsansicht mit zwei Kadergrenzen dokumentiert.

| Kriterium | Vorgabe |
|---|---|
| Kadergröße | Mindestens 15, maximal 99 |

Weitere Einschränkungen sind auf dem Screenshot nicht sichtbar. Dies belegt nicht die Abwesenheit anderer Spieltags- oder Förderbedingungen. Insbesondere wurde der Österreicher-Topf nicht anhand dieses Screenshots geprüft.

## 9. Russland – Premier Liga / RPL

Status: Screenshot dokumentiert; vollständige Bedingungen der Jugend-Ausnahme offen.

| Kriterium | Vorgabe |
|---|---|
| Registrierter Kader | Mindestens 15, maximal 55 |
| Russische U21-Spieler im Kader | Mindestens 30 |
| In Russland ausgebildet | Mindestens 8 |
| Im eigenen Verein ausgebildet | Mindestens 4 |
| Ausländische Spieler im Kader | Maximal 12 |
| Ausländische Spieler in der Startelf | Maximal 7 |
| Jugendstatus | Statusmeldungen: „No longer qualifies as domestic U18“ |

Die Statusmeldungen weisen auf eine Registrierungsbefreiung für einheimische U18-Spieler hin. Vollständige Bedingungen und die genaue Definition von „domestic“ sind nicht belegt. Die Mindestquote von 30 russischen U21-Spielern ist eine separate Regel und darf nicht mit der U18-Ausnahme zusammengefasst werden. Ausbildungszeiträume sind in dieser Ansicht nicht erläutert.

## 10. Ukraine – Premier League / UPL

Status: Screenshot dokumentiert; vollständige Bedingungen der Jugend-Ausnahme offen.

| Kriterium | Vorgabe |
|---|---|
| Registrierter Kader | Mindestens 15, maximal 25 |
| Im eigenen Verein oder bei einem Verein in der Ukraine ausgebildet | Insgesamt mindestens 8 |
| Im eigenen Verein ausgebildet | Mindestens 4 innerhalb der Ausbildungsquote |
| Definition Vereinsausbildung | 3 Jahre zwischen dem 15. und 21. Geburtstag |
| Ausländische Spieler in der Startelf | Maximal 7 |
| Jugendstatus | Statusmeldungen: „No longer qualifies as U21“ |

Die Statusmeldungen weisen auf eine U21-Registrierungsbefreiung hin; vollständige Bedingungen sind nicht belegt. Eine Nationalitätsbeschränkung dieser Ausnahme steht dort nicht. Eine Obergrenze ausländischer Spieler im gesamten registrierten Kader ist hier nicht angegeben.

## 11. Türkei – Süper Lig

Status: Screenshot dokumentiert; genaue Einstufung als einheimisch und Jugend-Ausnahme teilweise offen.

| Kriterium | Vorgabe |
|---|---|
| Registrierter Kader | Mindestens 15, maximal 28 |
| Ausländische Spieler im Kader | Maximal 14 |
| Ausländische Spieler über 24 Jahre | Maximal 10 |
| Übergang bei Altersregel | Spieler aus der Vorsaison dürfen laut Spielhinweis ein Jahr älter sein |
| Einheimische/türkische U23-Spieler | Mindestens 4 („Türkiye Under-23 players“) |
| Torhüter insgesamt | Maximal 3 |
| Einheimische Torhüter | Mindestens 2 |
| Jugendstatus | Statusmeldung für die Liga: „No longer qualifies as domestic U21“ |

Sonderregel laut Screenshot: Spieler aus turksprachigen Nationen, die ihrem aktuellen Verein zwischen dem 1. Juni 2023 und dem 31. Mai 2024 beigetreten sind, zählen als einheimisch. Die genaue Länderliste und der vollständige Anwendungsbereich dieser Einstufung sind nicht belegt.

Die Statusmeldung weist auf eine Registrierungsbefreiung für einheimische U21-Spieler hin. Die vollständigen Bedingungen sind offen. U23-Mindestquote und U21-Ausnahme sind getrennte Regeln. Eine Grenze ausländischer Spieler in der Startelf ist hier nicht angegeben.

## Ausgelassen und noch offen

- Niederlande – Eredivisie: auf Wunsch des Nutzers übersprungen, weil das Spiel die entsprechende Ansicht zu Beginn nicht zeigte. Keine Regeln bestätigt.
- Alle 11 vorgesehenen Ligen sind mit den sichtbaren Screenshot-Angaben dokumentiert.
- Offene Details nicht durch vermeintlich passende reale Regeln oder Regeln älterer FM-Versionen ersetzen. Ergänzungen separat als externe Recherche oder Annahme kennzeichnen.
- Besonders zu prüfen, falls für die Weiterverarbeitung nötig: Jugend-Ausnahmen, Behandlung fehlender Ausbildungsplätze, Portugal ohne B-Team, Bedeutung des ersten Seniorenjahrs, LaLiga-Regel zu anderen Teams sowie genaue Einheimischen-/Nicht-EU-Einstufungen.
