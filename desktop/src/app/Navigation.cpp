#include "Navigation.h"

#include <QCoreApplication>
#include <QHash>

namespace fm {
namespace Navigation {

namespace {

Group playersGroup()
{
    return {QStringLiteral("players"),
            QCoreApplication::translate("Navigation", "Players"),
            {{QCoreApplication::translate("Navigation", "Profile"),
              QStringLiteral("player_profile")},
             {QCoreApplication::translate("Navigation", "Comparison"),
              QStringLiteral("player_comparison")},
             {QCoreApplication::translate("Navigation", "Edit"),
              QStringLiteral("edit_player")}}};
}

Section adminSection(bool customRoles)
{
    QList<Tab> roleTabs = {{QCoreApplication::translate("Navigation", "Assign Roles"),
                            QStringLiteral("assign_roles")}};
    if (customRoles)
        roleTabs.append({QCoreApplication::translate("Navigation", "New Role"),
                         QStringLiteral("new_role")});
    roleTabs.append({QCoreApplication::translate("Navigation", "Tactics"),
                     QStringLiteral("new_tactic")});
    return {QCoreApplication::translate("Navigation", "MANAGEMENT"),
            {{QStringLiteral("roles_tactics"),
              QCoreApplication::translate("Navigation", "Roles & Tactics"), roleTabs},
             {QStringLiteral("settings"),
              QCoreApplication::translate("Navigation", "Settings"),
              {{QCoreApplication::translate("Navigation", "Settings"),
                QStringLiteral("settings")}}}}};
}

} // namespace

QList<Section> sidebar(bool national, bool customRoles)
{
    if (national) {
        return {
            {QCoreApplication::translate("Navigation", "NATIONAL TEAM"),
             {{QStringLiteral("national_overview"),
               QCoreApplication::translate("Navigation", "Overview"),
               {{QCoreApplication::translate("Navigation", "Overview"),
                 QStringLiteral("national_dashboard")}}},
              {QStringLiteral("national_squad"),
               QCoreApplication::translate("Navigation", "Squad"),
               {{QCoreApplication::translate("Navigation", "Selection"),
                 QStringLiteral("national_squad_selection")},
                {QCoreApplication::translate("Navigation", "Call-up Assistant"),
                 QStringLiteral("national_callup")},
                {QCoreApplication::translate("Navigation", "Matrix"),
                 QStringLiteral("national_squad_matrix")}}},
              {QStringLiteral("national_lineup"),
               QCoreApplication::translate("Navigation", "Line-up"),
               {{QCoreApplication::translate("Navigation", "Best XI"),
                 QStringLiteral("national_best_xi")},
                {QCoreApplication::translate("Navigation", "Tactic Comparison"),
                 QStringLiteral("tactic_explorer")}}},
              {QStringLiteral("national_development"),
               QCoreApplication::translate("Navigation", "Development"),
               {{QCoreApplication::translate("Navigation", "Development"),
                 QStringLiteral("dwrs_development")}}},
              playersGroup()}},
            adminSection(customRoles),
        };
    }

    return {
        {QCoreApplication::translate("Navigation", "CLUB"),
         {{QStringLiteral("overview"),
           QCoreApplication::translate("Navigation", "Overview"),
           {{QCoreApplication::translate("Navigation", "Overview"),
             QStringLiteral("dashboard")}}},
          {QStringLiteral("lineup"),
           QCoreApplication::translate("Navigation", "Line-up"),
           {{QCoreApplication::translate("Navigation", "Starting XI & B-Team"),
             QStringLiteral("best_xi")},
            {QCoreApplication::translate("Navigation", "Youth & Second Team"),
             QStringLiteral("best_xi_youth")},
            {QCoreApplication::translate("Navigation", "Tactic Comparison"),
             QStringLiteral("tactic_explorer")}}},
          {QStringLiteral("squad"),
           QCoreApplication::translate("Navigation", "Squad"),
           {{QCoreApplication::translate("Navigation", "Matrix"),
             QStringLiteral("squad_matrix")},
            {QCoreApplication::translate("Navigation", "Role Coverage"),
             QStringLiteral("role_analysis")},
            // Always listed: hiding it while no rules are active made it
            // impossible to find; the page itself explains how to enable it.
            {QCoreApplication::translate("Navigation", "Squad Registration"),
             QStringLiteral("registration"),
             QCoreApplication::translate("Navigation", "registration squad list squad registration")}}},
          {QStringLiteral("development"),
           QCoreApplication::translate("Navigation", "Training & Development"),
           {{QCoreApplication::translate("Navigation", "Development"),
             QStringLiteral("dwrs_development")},
            {QCoreApplication::translate("Navigation", "Training Plan"),
             QStringLiteral("training_plan")}}},
          {QStringLiteral("transfers"),
           QCoreApplication::translate("Navigation", "Transfers"),
           {{QCoreApplication::translate("Navigation", "Signing Advisor"),
             QStringLiteral("signing_advisor")},
            {QCoreApplication::translate("Navigation", "Loans & Sales"),
             QStringLiteral("transfers")}}}}},
        {QCoreApplication::translate("Navigation", "SCOUTING"),
         {{QStringLiteral("scouting"),
           QCoreApplication::translate("Navigation", "Scouting"),
           {{QCoreApplication::translate("Navigation", "Matrix"),
             QStringLiteral("scouting_matrix")},
            {QCoreApplication::translate("Navigation", "By Role"),
             QStringLiteral("scouting_roles")}}},
          playersGroup()}},
        adminSection(customRoles),
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
