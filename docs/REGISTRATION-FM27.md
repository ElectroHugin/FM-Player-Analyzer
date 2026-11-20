# FM27 – Squad and registration rules

<!-- Source of the registration list rules for FM27 (core/Registration.cpp, quotaFor). -->

> **What the Analyzer implements:** squad size, minimum number of players trained
> in the nation and minimum number of players trained at the club (as the
> maximum of the remaining places), and the U21 exemption where it is documented
> (Premier League, Ukraine). For LaLiga the registration list also takes (since
> 2.2.1, from 2.2.2 only there) a wage budget (sum of the registered players) and
> a minimum wage, both optional because the amounts change in the game. Since
> 2.2.2 the non-EU limit of the registration list applies (LaLiga 3,
> Switzerland 10); who counts as non-EU is marked by hand. In Switzerland the
> Best XI also keeps at most 5 non-EU players in the starting eleven. Leagues
> with 99 places (Bundesliga, Ligue 1, Austria) have no registration list. Not
> implemented: foreigner limits, the Ligue 1 non-EU limit (no registration list,
> only a hint), other starting eleven limits, the Süper Lig goalkeeper rules and
> youth exemptions that depend on nationality.
> Further leagues please via GitHub issue with a screenshot of the *Squad
> Selection Rules*.


## Purpose and sources

This collection documents the rules from Football Manager 27 screenshots
provided by the user. It covers all five big European leagues and six further
leagues.

- Source: FM27 game screens "Squad Selection Rules"; for Austria a registration
  screen with squad limits.
- The exact season and game version of the screenshots were not recorded. The
  rules are therefore evidence for the saves shown, not a guarantee for every
  season or database.
- Real association rules were researched in the conversation but are NOT the
  basis of the values below.
- "Documented" means the visible information was transcribed. Not every special
  case is settled by that.
- Birth date cut-offs for youth categories are not needed, at the user's
  explicit request.
- Missing information means "not documented", not automatically "no
  restriction".
- Nationality, status as a domestic player and training status are separate
  criteria. "Non-EU" and "foreign" must not be equated.
- Club-trained players are part of the nation-trained players; the quotas are
  not added up.
- With training quotas, unfilled places may reduce the allowed squad size. The
  full handling of this case is not documented by the screenshots; do not
  implement minimum quotas unchecked as an absolute registration ban.
- Checks marked red/green were partly contradictory. The rule texts and limits
  shown are authoritative for this collection, not the colours or current
  example squads.

## 1. Germany – Bundesliga

Status: confirmed and secured by the user.

| Criterion | Rule |
|---|---|
| Squad size | At least 15, at most 99 |
| Trained in Germany | At least 8 |
| Trained at the club | At least 4 |
| German players | At least 12 |
| Definition of nation training | 3 years in Germany between the 15th and 21st birthday |

The detailed training period for the club quota and youth exemptions are not
shown separately on this screen. The 99 is a documented FM display, not a real
league limit documented here.

## 2. England – Premier League

Status: screenshot documented; U21 exemption also explicitly confirmed by the
user.

| Criterion | Rule |
|---|---|
| Registered squad | At least 15, at most 25 |
| Trained in England | Displayed minimum quota: 8 |
| Definition of nation training | 3 years before the 21st birthday |
| U21 | Need not be registered; outside the list of 25 |

The screenshot shows 20 registered players and only 7 home-grown players with a
green tick. That is consistent with fewer usable squad places when home-grown
players are missing, but does not prove the full calculation on its own. The
game screen says "England"; an extension to Wales was not derived from this
screenshot.

## 3. Spain – LaLiga

Status: screenshot documented; youth exemption and the term "other team" partly
open.

| Criterion | Rule |
|---|---|
| Registered squad | At least 15, at most 25 |
| Outfield players | At most 23 |
| Non-EU players | At most 3 by the game's classification |
| Starting eleven | At most 4 players registered for another team |
| Total squad wages | Below EUR 6,500,000 per week in the case shown |
| Youth status | Status message: "No longer qualifies as an U19 EU player" |

Notes:

- Treat the wage limit as a value of the club/save shown, NOT as a uniform
  league-wide value.
- "Other team" is transcribed literally. Which teams it covers is not
  documented; do not interpret it unchecked as a restriction for earlier
  transfers.
- The status message points to an exemption for U19 EU players. Full conditions
  and scope are not documented.
- The screenshot shows 2 non-EU players with a maximum of 3, still red. No
  additional rule follows from this.

## 4. Italy – Serie A

Status: screenshot documented.

| Criterion | Rule |
|---|---|
| Registered squad | At least 15, at most 25 |
| Trained in Italy | At least 8 |
| Trained at the club | At least 4 |
| Definition of club training | 3 years between the 15th and 21st birthday |
| Club training detail | According to the game, independent of loans to other clubs in between |

The definition of nation training is not explained separately. No youth
exemption is shown here. The example squad has 23 players and only 2
club-trained players with a green tick; the handling of missing training places
must be considered or checked separately in an implementation.

## 5. France – Ligue 1

Status: screenshot documented.

| Criterion | Rule |
|---|---|
| Squad size | At least 15, at most 99 |
| Non-EU players | At most 4 by the game's classification |

No training quota or youth exemption is shown on this screen. Which
nationalities or agreements FM considers for the non-EU classification is not
documented here. The display is red despite 1 non-EU player with a maximum of
4; derive no further rule from this.

## 6. Portugal – Primeira Liga / Liga Portugal

Status: screenshot for a club WITH a B team documented; the variant without a B
team is incomplete.

| Criterion | Rule |
|---|---|
| Squad size with B team | At least 15, at most 68 |
| Trained in Portugal – general displayed quota | At least 8 |
| Trained in Portugal – clubs with a B team | At least 10 |
| Definition of nation training | 3 years between the 15th and 21st birthday |
| Players in their first senior year | At least 7 ("first-year senior players") |
| U23 players at clubs with a B team | At most 31 |

Notes:

- The quotas of 8 and 10 are not added; for the club shown with a B team the
  quota of 10 applies.
- The exact scope of the registered squad across first team/B team is not
  explained.
- Squad limits for clubs without a B team are not documented.
- The exact definition of "first-year senior players" is open. Do not equate it
  automatically with U21 or U23.

## 7. Switzerland – Super League

Status: screenshot documented.

| Criterion | Rule |
|---|---|
| Registered squad | At least 15, at most 25 |
| Trained in Switzerland | At least 8 |
| Non-EU players in the squad | At most 10 by the game's classification |
| Non-EU players in the starting eleven | At most 5 |

The exact definition of nation training, a separate club training quota and
youth exemptions are not shown. The red check despite 3 non-EU players in the
squad with a maximum of 10 is not counted as an additional rule.

## 8. Austria – Bundesliga

Status: registration screen with two squad limits documented.

| Criterion | Rule |
|---|---|
| Squad size | At least 15, at most 99 |

No further restrictions are visible on the screenshot. This does not prove the
absence of other match day or funding conditions. In particular, the
Austrian-player pot was not checked with this screenshot.

## 9. Russia – Premier League / RPL

Status: screenshot documented; full conditions of the youth exemption open.

| Criterion | Rule |
|---|---|
| Registered squad | At least 15, at most 55 |
| Russian U21 players in the squad | At least 30 |
| Trained in Russia | At least 8 |
| Trained at the club | At least 4 |
| Foreign players in the squad | At most 12 |
| Foreign players in the starting eleven | At most 7 |
| Youth status | Status messages: "No longer qualifies as domestic U18" |

The status messages point to a registration exemption for domestic U18 players.
Full conditions and the exact definition of "domestic" are not documented. The
minimum quota of 30 Russian U21 players is a separate rule and must not be
merged with the U18 exemption. Training periods are not explained on this
screen.

## 10. Ukraine – Premier League / UPL

Status: screenshot documented; full conditions of the youth exemption open.

| Criterion | Rule |
|---|---|
| Registered squad | At least 15, at most 25 |
| Trained at the club or at a club in Ukraine | At least 8 in total |
| Trained at the club | At least 4 within the training quota |
| Definition of club training | 3 years between the 15th and 21st birthday |
| Foreign players in the starting eleven | At most 7 |
| Youth status | Status messages: "No longer qualifies as U21" |

The status messages point to a U21 registration exemption; full conditions are
not documented. No nationality restriction of this exemption is shown there. A
limit on foreign players in the whole registered squad is not shown here.

## 11. Türkiye – Süper Lig

Status: screenshot documented; exact domestic classification and youth
exemption partly open.

| Criterion | Rule |
|---|---|
| Registered squad | At least 15, at most 28 |
| Foreign players in the squad | At most 14 |
| Foreign players over 24 | At most 10 |
| Transition for the age rule | According to the game's hint, players from the previous season may be one year older |
| Domestic/Turkish U23 players | At least 4 ("Türkiye Under-23 players") |
| Goalkeepers in total | At most 3 |
| Domestic goalkeepers | At least 2 |
| Youth status | Status message for the league: "No longer qualifies as domestic U21" |

Special rule according to the screenshot: players from Turkic nations who joined
their current club between 1 June 2023 and 31 May 2024 count as domestic. The
exact list of countries and the full scope of this classification are not
documented.

The status message points to a registration exemption for domestic U21 players.
The full conditions are open. The U23 minimum quota and the U21 exemption are
separate rules. A limit on foreign players in the starting eleven is not shown
here.

## Skipped and still open

- Netherlands – Eredivisie: skipped at the user's request because the game did
  not show the screen at the start. No rules confirmed.
- All 11 planned leagues are documented with the information visible on the
  screenshots.
- Do not replace open details with seemingly matching real rules or rules of
  older FM versions. Mark additions separately as external research or
  assumption.
- To check in particular, if needed for further processing: youth exemptions,
  handling of missing training places, Portugal without a B team, meaning of the
  first senior year, the LaLiga rule on other teams and exact domestic/non-EU
  classifications.
