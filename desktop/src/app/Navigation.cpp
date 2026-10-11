#include "Navigation.h"

#include <QCoreApplication>
#include <QHash>

namespace fm {
namespace Navigation {

namespace {

Group playersGroup()
{
    return {QStringLiteral("players"),
            QCoreApplication::translate("Navigation", "Spieler"),
            {{QCoreApplication::translate("Navigation", "Profil"),
              QStringLiteral("player_profile")},
             {QCoreApplication::translate("Navigation", "Vergleich"),
              QStringLiteral("player_comparison")},
             {QCoreApplication::translate("Navigation", "Bearbeiten"),
              QStringLiteral("edit_player")}}};
}

Section adminSection()
{
    return {QCoreApplication::translate("Navigation", "VERWALTUNG"),
            {{QStringLiteral("roles_tactics"),
              QCoreApplication::translate("Navigation", "Rollen & Taktiken"),
              {{QCoreApplication::translate("Navigation", "Rollen zuweisen"),
                QStringLiteral("assign_roles")},
               {QCoreApplication::translate("Navigation", "Neue Rolle"),
                QStringLiteral("new_role")},
               {QCoreApplication::translate("Navigation", "Neue Taktik"),
                QStringLiteral("new_tactic")}}},
             {QStringLiteral("settings"),
              QCoreApplication::translate("Navigation", "Einstellungen"),
              {{QCoreApplication::translate("Navigation", "Einstellungen"),
                QStringLiteral("settings")}}}}};
}

} // namespace

QList<Section> sidebar(bool national, bool registrationActive)
{
    if (national) {
        return {
            {QCoreApplication::translate("Navigation", "NATIONALTEAM"),
             {{QStringLiteral("national_overview"),
               QCoreApplication::translate("Navigation", "Übersicht"),
               {{QCoreApplication::translate("Navigation", "Übersicht"),
                 QStringLiteral("national_dashboard")}}},
              {QStringLiteral("national_squad"),
               QCoreApplication::translate("Navigation", "Kader"),
               {{QCoreApplication::translate("Navigation", "Auswahl"),
                 QStringLiteral("national_squad_selection")},
                {QCoreApplication::translate("Navigation", "Nominierungs-Assistent"),
                 QStringLiteral("national_callup")},
                {QCoreApplication::translate("Navigation", "Matrix"),
                 QStringLiteral("national_squad_matrix")}}},
              {QStringLiteral("national_lineup"),
               QCoreApplication::translate("Navigation", "Aufstellung"),
               {{QCoreApplication::translate("Navigation", "Best XI"),
                 QStringLiteral("national_best_xi")},
                {QCoreApplication::translate("Navigation", "Taktik-Vergleich"),
                 QStringLiteral("tactic_explorer")}}},
              {QStringLiteral("national_development"),
               QCoreApplication::translate("Navigation", "Entwicklung"),
               {{QCoreApplication::translate("Navigation", "Entwicklung"),
                 QStringLiteral("dwrs_development")}}},
              playersGroup()}},
            adminSection(),
        };
    }

    QList<Tab> transferTabs = {
        {QCoreApplication::translate("Navigation", "Kaufberatung"),
         QStringLiteral("signing_advisor")},
        {QCoreApplication::translate("Navigation", "Leihen & Verkäufe"),
         QStringLiteral("transfers")},
    };
    if (registrationActive) {
        transferTabs.append(
            {QCoreApplication::translate("Navigation", "Meldeliste"), QStringLiteral("registration")});
    }

    return {
        {QCoreApplication::translate("Navigation", "VEREIN"),
         {{QStringLiteral("overview"),
           QCoreApplication::translate("Navigation", "Übersicht"),
           {{QCoreApplication::translate("Navigation", "Übersicht"),
             QStringLiteral("dashboard")}}},
          {QStringLiteral("lineup"),
           QCoreApplication::translate("Navigation", "Aufstellung"),
           {{QCoreApplication::translate("Navigation", "Startelf & B-Team"),
             QStringLiteral("best_xi")},
            {QCoreApplication::translate("Navigation", "Jugend & Zweitteam"),
             QStringLiteral("best_xi_youth")},
            {QCoreApplication::translate("Navigation", "Taktik-Vergleich"),
             QStringLiteral("tactic_explorer")}}},
          {QStringLiteral("squad"),
           QCoreApplication::translate("Navigation", "Kader"),
           {{QCoreApplication::translate("Navigation", "Matrix"),
             QStringLiteral("squad_matrix")},
            {QCoreApplication::translate("Navigation", "Rollen-Abdeckung"),
             QStringLiteral("role_analysis")}}},
          {QStringLiteral("development"),
           QCoreApplication::translate("Navigation", "Training & Entwicklung"),
           {{QCoreApplication::translate("Navigation", "Entwicklung"),
             QStringLiteral("dwrs_development")},
            {QCoreApplication::translate("Navigation", "Trainingsplan"),
             QStringLiteral("training_plan")}}},
          {QStringLiteral("transfers"),
           QCoreApplication::translate("Navigation", "Transfers"),
           transferTabs}}},
        {QCoreApplication::translate("Navigation", "SCOUTING"),
         {{QStringLiteral("scouting"),
           QCoreApplication::translate("Navigation", "Scouting"),
           {{QCoreApplication::translate("Navigation", "Matrix"),
             QStringLiteral("scouting_matrix")},
            {QCoreApplication::translate("Navigation", "Nach Rolle"),
             QStringLiteral("scouting_roles")}}},
          playersGroup()}},
        adminSection(),
    };
}

const Group *findGroup(const QList<Section> &sections, const QString &pageId)
{
    for (const Section &section : sections) {
        for (const Group &group : section.groups) {
            for (const Tab &tab : group.tabs) {
                if (tab.pageId == pageId)
                    return &group;
            }
        }
    }
    return nullptr;
}

QString hostPage(const QString &pageId)
{
    static const QHash<QString, QString> hosts = {
        {QStringLiteral("best_xi_youth"), QStringLiteral("best_xi")},
        {QStringLiteral("signing_advisor"), QStringLiteral("best_xi")},
        {QStringLiteral("scouting_matrix"), QStringLiteral("squad_matrix")},
        {QStringLiteral("scouting_roles"), QStringLiteral("role_analysis")},
    };
    return hosts.value(pageId, pageId);
}

} // namespace Navigation
} // namespace fm
