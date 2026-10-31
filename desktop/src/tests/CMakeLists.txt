# Path to the legacy Python app: source of fixture data (definitions.json)
# and golden-master reference outputs.
set(LEGACY_DIR "${CMAKE_SOURCE_DIR}/../legacy")

function(fm_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE fmcore Qt6::Test)
    target_compile_definitions(${name} PRIVATE LEGACY_DIR="${LEGACY_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    # Qt DLLs are not on PATH on Windows; point the test at the Qt bin directory.
    # (Harmless elsewhere: the Qt libraries are found via RPATH.)
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endfunction()

fm_add_test(test_smoke)
fm_add_test(test_utils)
fm_add_test(test_definitions)
fm_add_test(test_appconfig)
fm_add_test(test_playerstore)
fm_add_test(test_migrator)
fm_add_test(test_dwrs)
fm_add_test(test_htmlimporter)
fm_add_test(test_csvimport)
fm_add_test(test_database)
fm_add_test(test_freshness)
fm_add_test(test_nationalcallup)
fm_add_test(test_trainingadvice)
fm_add_test(test_roleratings)
fm_add_test(test_playerstatus)
fm_add_test(test_registration)
fm_add_test(test_squadbuilder)
fm_add_test(test_signingadvisor)
fm_add_test(test_development)
fm_add_test(test_pitchgeometry)
fm_add_test(test_roleassignment)
fm_add_test(test_gapanalysis)
fm_add_test(test_tacticexplorer)
fm_add_test(test_talentengine)
fm_add_test(test_ratingsupdater)
fm_add_test(test_rolepairs)

# Widget test for an app-side class: compiles the widget source directly (the
# app is an executable, not a library) and runs headless on the offscreen QPA.
add_executable(test_busydialog test_busydialog.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/BusyProgressDialog.cpp)
target_include_directories(test_busydialog PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(test_busydialog PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_busydialog COMMAND test_busydialog)
set_tests_properties(test_busydialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

# Development chart hover (crosshair tooltip), headless like test_busydialog.
add_executable(test_charts test_charts.cpp
    ${CMAKE_SOURCE_DIR}/src/app/widgets/Charts.cpp
    ${CMAKE_SOURCE_DIR}/src/app/theming/ThemeManager.cpp)
target_include_directories(test_charts PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(test_charts PRIVATE fmcore Qt6::Widgets Qt6::Charts Qt6::Test)
add_test(NAME test_charts COMMAND test_charts)
set_tests_properties(test_charts PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

# Sidebar structure (app/Navigation): plain Qt Core, compiled in directly.
add_executable(test_navigation test_navigation.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Navigation.cpp)
target_include_directories(test_navigation PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(test_navigation PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_navigation COMMAND test_navigation)
set_tests_properties(test_navigation PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

# FM27 role data (generated definitions_fm27.json).
fm_add_test(test_definitions_fm27)
target_compile_definitions(test_definitions_fm27 PRIVATE
    DEFINITIONS_FM27="${CMAKE_SOURCE_DIR}/src/app/resources/definitions_fm27.json")
