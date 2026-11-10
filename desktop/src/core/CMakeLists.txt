add_library(fmcore STATIC
    AppConfig.h
    AppConfig.cpp
    Assignment.h
    Assignment.cpp
    BridgeAssignment.h
    BridgeAssignment.cpp
    AttributeTiers.h
    AttributeTiers.cpp
    Attributes.h
    Constants.h
    Constants.cpp
    CsvReader.h
    CsvReader.cpp
    Database.h
    Database.cpp
    Definitions.h
    Definitions.cpp
    Development.h
    Development.cpp
    ImportHistory.h
    ImportHistory.cpp
    RatingProfileLibrary.h
    RatingProfileLibrary.cpp
    RatingSnapshot.h
    RatingSnapshot.cpp
    RatingProfile.h
    RatingProfile.cpp
    RatingCurve.h
    RatingCurve.cpp
    DwrsEngine.h
    DwrsEngine.cpp
    Fm27Save.h
    Fm27Save.cpp
    Fm27SaveClubs.h
    Fm27SaveClubs.cpp
    Fm27SaveImport.h
    Fm27SaveImport.cpp
    Fm27SaveFamily.h
    Fm27SaveFamily.cpp
    Freshness.h
    Freshness.cpp
    GapAnalysis.h
    GapAnalysis.cpp
    GistRainbowLut.h
    GraphicsIndex.h
    GraphicsIndex.cpp
    HtmlImporter.h
    HtmlImporter.cpp
    ImportSchema.h
    ImportSchema.cpp
    ImportTable.h
    ImportTable.cpp
    ImportPipeline.h
    ImportPipeline.cpp
    LegacyMigrator.h
    LegacyMigrator.cpp
    MinCostFlow.h
    MinCostFlow.cpp
    NationalCallup.h
    NationalCallup.cpp
    PitchGeometry.h
    PitchGeometry.cpp
    Player.h
    PlayerStatus.h
    PlayerStatus.cpp
    PlayerStore.h
    PlayerStore.cpp
    ProfileStore.h
    ProfileStore.cpp
    RatingsUpdater.h
    RatingsUpdater.cpp
    Registration.h
    Registration.cpp
    RoleAnalysis.h
    RoleAnalysis.cpp
    RoleAssignment.h
    RoleAssignment.cpp
    ScoutSearch.h
    ScoutSearch.cpp
    SigningAdvisor.h
    SigningAdvisor.cpp
    SquadBuilder.h
    SquadBuilder.cpp
    TacticLibrary.h
    TacticLibrary.cpp
    TacticExplorer.h
    TacticExplorer.cpp
    TrainingAdvice.h
    TrainingAdvice.cpp
    TalentEngine.h
    TalentEngine.cpp
    Utils.h
    Utils.cpp
    ThemeColors.h
    ThemeColors.cpp
    ColorTemplates.h
    ColorTemplates.cpp
    Version.h
    Version.cpp
)

target_include_directories(fmcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Deliberately no Qt6::Widgets: fmcore must stay headless-testable.
# Qt6::Gui is needed for QColor only.
target_link_libraries(fmcore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Concurrent
)

# zstd decompresses FM27 save files (.fm). Use the system library when there is
# one (Linux), otherwise build the pinned release from source (Windows CI).
find_path(FM_ZSTD_INCLUDE_DIR zstd.h)
find_library(FM_ZSTD_LIBRARY NAMES zstd zstd_static libzstd)
if(FM_ZSTD_INCLUDE_DIR AND FM_ZSTD_LIBRARY)
    target_include_directories(fmcore PRIVATE ${FM_ZSTD_INCLUDE_DIR})
    target_link_libraries(fmcore PRIVATE ${FM_ZSTD_LIBRARY})
else()
    include(FetchContent)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz
        URL_HASH SHA256=8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1
        SOURCE_SUBDIR build/cmake
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(zstd)
    target_link_libraries(fmcore PRIVATE libzstd_static)
    target_include_directories(fmcore PRIVATE ${zstd_SOURCE_DIR}/lib)
endif()
