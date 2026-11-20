add_executable(fmplayeranalyzer
    main.cpp
    ScreenshotTour.cpp
    AboutDialog.h
    AboutDialog.cpp
    AppContext.h
    AppContext.cpp
    AppPaths.h
    AppPaths.cpp
    FirstRunDialog.h
    GraphicsLibrary.h
    GraphicsLibrary.cpp
    FirstRunDialog.cpp
    MainWindow.h
    MainWindow.cpp
    NewSaveWizard.h
    NewSaveWizard.cpp
    WelcomeWizard.h
    WelcomeWizard.cpp
    SaveLibrary.h
    SaveLibrary.cpp
    StartScreen.h
    StartScreen.cpp
    UpdateChecker.h
    UpdateChecker.cpp
    UpdateDialog.h
    UpdateDialog.cpp
    MigrationWizard.h
    MigrationWizard.cpp
    Navigation.h
    Navigation.cpp
    ImportRunner.h
    ImportRunner.cpp
    LiveSaveWatcher.h
    LiveSaveWatcher.cpp
    PlayerActions.h
    PlayerActions.cpp
    RecalcHelper.h
    RecalcHelper.cpp
    pages/AssignRolesPage.h
    pages/AssignRolesPage.cpp
    pages/BestXiPage.h
    pages/BestXiPage.cpp
    pages/BestXiDepth.cpp
    pages/DashboardPage.h
    pages/DashboardPage.cpp
    pages/DwrsProgressPage.h
    pages/DwrsProgressPage.cpp
    pages/EditPlayerPage.h
    pages/EditPlayerPage.cpp
    pages/NationalBestXiPage.h
    pages/NationalBestXiPage.cpp
    pages/NationalCallupPage.h
    pages/NationalCallupPage.cpp
    pages/RegistrationPage.h
    pages/RegistrationPage.cpp
    pages/TrainingPlanPage.h
    pages/TrainingPlanPage.cpp
    pages/NationalDashboardPage.h
    pages/NationalDashboardPage.cpp
    pages/NationalSquadMatrixPage.h
    pages/NationalSquadMatrixPage.cpp
    pages/NationalSquadSelectionPage.h
    pages/NationalSquadSelectionPage.cpp
    pages/NewRolePage.h
    pages/NewRolePage.cpp
    pages/NewTacticPage.h
    pages/NewTacticPage.cpp
    pages/PageBase.h
    pages/PageHelpers.h
    pages/PlaceholderPage.h
    pages/PlayerComparisonPage.h
    pages/PlayerComparisonPage.cpp
    pages/PlayerProfilePage.h
    pages/PlayerProfilePage.cpp
    pages/RoleAnalysisPage.h
    pages/RoleAnalysisPage.cpp
    pages/SigningAdvisorTab.h
    pages/SigningAdvisorTab.cpp
    pages/SettingsPage.h
    pages/SettingsPage.cpp
    pages/SquadMatrixPage.h
    pages/SquadMatrixPage.cpp
    pages/TacticExplorerPage.h
    pages/TacticExplorerPage.cpp
    pages/TransfersPage.h
    pages/TransfersPage.cpp
    theming/ThemeManager.h
    theming/ThemeManager.cpp
    theming/ThemePresets.h
    theming/ThemePresets.cpp
    widgets/CommandPalette.h
    widgets/CommandPalette.cpp
    widgets/DropZone.h
    widgets/ColumnChooser.h
    widgets/DwrsColorSchemeEditor.h
    widgets/DwrsColorSchemeEditor.cpp
    widgets/ColumnChooser.cpp
    widgets/FlowLayout.h
    widgets/FlowLayout.cpp
    widgets/FootStrengthWidget.h
    widgets/FootStrengthWidget.cpp
    widgets/RolePhaseSwitch.h
    widgets/TrainingProfileEditor.h
    widgets/TrainingProfileEditor.cpp
    widgets/RolePhaseSwitch.cpp
    widgets/ClubHeaderBar.h
    widgets/LineIcons.h
    widgets/LineIcons.cpp
    widgets/DropZone.cpp
    widgets/ClubHeaderBar.cpp
    widgets/BusyProgressDialog.h
    widgets/BusyProgressDialog.cpp
    widgets/CellStyleDelegate.h
    widgets/CellStyleDelegate.cpp
    widgets/NumericTableItem.h
    widgets/PlayerSearchModel.h
    widgets/Charts.h
    widgets/Charts.cpp
    widgets/PersonalityFilterWidget.h
    widgets/PersonalityFilterWidget.cpp
    widgets/PlayerTableModel.h
    widgets/PlayerTableModel.cpp
    widgets/ScoutRequirementsBar.h
    widgets/ScoutRequirementsBar.cpp
    widgets/StrengthGridWidget.h
    widgets/StrengthGridWidget.cpp
    widgets/TacticPitchWidget.h
    widgets/TacticPitchWidget.cpp
    resources/app.qrc
)

# Windows only: GUI subsystem (no console window) and the exe icon resource.
if(WIN32)
    # Icon + version info; the version comes from project() in the top-level
    # CMakeLists.txt.
    set(FMPA_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/favicon.ico")
    configure_file(app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(fmplayeranalyzer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    set_target_properties(fmplayeranalyzer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(fmplayeranalyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(fmplayeranalyzer PRIVATE
    fmcore
    Qt6::Widgets
    Qt6::Charts
    Qt6::Network
)

# --- Translations -----------------------------------------------------------
# The source strings are English. Every translations/fmplayeranalyzer_<lang>.ts
# is picked up automatically, embedded under :/i18n/fmplayeranalyzer_<lang>.qm
# and offered in the language menus; it is loaded on top of the English file,
# which only holds English plural forms. After adding or changing tr() strings,
# regenerate the template and the English plural forms, then bring the
# languages in line:
#   lupdate src -ts translations/fmplayeranalyzer.ts -no-obsolete -source-language en
#   lupdate src -ts translations/fmplayeranalyzer_en.ts -pluralonly -no-obsolete -source-language en
#   python translations/translate.py sync
# See translations/TRANSLATING.md.
file(GLOB FM_TS_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/translations/fmplayeranalyzer_*.ts)
set(FM_QM_FILES)
foreach(ts_file IN LISTS FM_TS_FILES)
    get_filename_component(ts_name ${ts_file} NAME_WE)
    string(REPLACE "fmplayeranalyzer_" "" lang ${ts_name})
    set(qm_file ${CMAKE_CURRENT_BINARY_DIR}/fmplayeranalyzer_${lang}.qm)
    add_custom_command(
        OUTPUT ${qm_file}
        COMMAND Qt6::lrelease ${ts_file} -qm ${qm_file}
        DEPENDS ${ts_file}
        VERBATIM
        COMMENT "Compiling translation ${lang} (lrelease)"
    )
    set_source_files_properties(${qm_file} PROPERTIES QT_RESOURCE_ALIAS fmplayeranalyzer_${lang}.qm)
    list(APPEND FM_QM_FILES ${qm_file})
endforeach()
qt_add_resources(fmplayeranalyzer "translations"
    PREFIX "/i18n"
    FILES ${FM_QM_FILES}
)
target_sources(fmplayeranalyzer PRIVATE widgets/RatingCurveEditor.h widgets/RatingCurveEditor.cpp)
target_sources(fmplayeranalyzer PRIVATE widgets/RatingProfileEditor.h widgets/RatingProfileEditor.cpp)
target_sources(fmplayeranalyzer PRIVATE RatingProfileDefaults.h RatingProfileDefaults.cpp)
