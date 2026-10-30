cmake_minimum_required(VERSION 3.21)
project(AtPet VERSION 1.3.0 LANGUAGES CXX)
# Human-readable version string compiled into the binary (CMake's project VERSION
# must stay numeric; pre-release suffixes live here). The Windows resource keeps
# using PROJECT_VERSION_* for FILEVERSION/PRODUCTVERSION.
set(AT_PET_VERSION_STRING "1.3.0-beta1" CACHE STRING "Human-readable At-Pet version string")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network LinguistTools)

qt_add_executable(at-pet WIN32 MACOSX_BUNDLE
    app/main.cpp
    app/petwindow.cpp
    app/petwindow.h
    app/httpapi.cpp
    app/httpapi.h
    app/agentmonitor.cpp
    app/agentmonitor.h
    app/agentadapters/claude-code/adapter.cpp
    app/agentadapters/claude-code/adapter.h
    app/agentadapters/opencode-v2/adapter.cpp
    app/agentadapters/opencode-v2/adapter.h
    app/permissiondialog.cpp
    app/permissiondialog.h
    app/questiondialog.cpp
    app/questiondialog.h
    app/agentregistry.h
    app/petsettings.cpp
    app/petsettings.h
    app/settingsdialog.cpp
    app/settingsdialog.h
    app/theme.cpp
    app/theme.h
    assets/branding/branding.qrc
    assets/default/pet.qrc
)

target_link_libraries(at-pet PRIVATE Qt6::Widgets Qt6::Network)
if(WIN32)
    configure_file(
        assets/branding/app-icon.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc"
        @ONLY
    )
    configure_file(
        assets/branding/app-icon.ico
        "${CMAKE_CURRENT_BINARY_DIR}/app-icon.ico"
        COPYONLY
    )
    target_sources(at-pet PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc")
endif()
qt_add_translations(at-pet
    TS_FILES translations/at-pet_zh_CN.ts
    RESOURCE_PREFIX "/i18n"
)
target_compile_definitions(at-pet PRIVATE AT_PET_VERSION="${AT_PET_VERSION_STRING}")

install(TARGETS at-pet BUNDLE DESTINATION . RUNTIME DESTINATION bin)

# --- Tests -------------------------------------------------------------------
# Regression tests for user-visible UI state sync (pet context menu / tray <-> Settings dialog):
# Gravity, "Look at the mouse cursor" and interface language. Opt in with
# -DAT_PET_BUILD_TESTS=ON so normal builds and the packaging flow are unaffected.
option(AT_PET_BUILD_TESTS "Build the At-Pet QTest regression tests" OFF)

if(AT_PET_BUILD_TESTS)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    enable_testing()

    # Production sources the SettingsDialog suites need. PetController lives in app/main.cpp
    # and is not linkable on its own, so each suite drives a fake controller instead.
    set(AT_PET_TEST_SOURCES
        app/settingsdialog.cpp
        app/settingsdialog.h
        app/petwindow.cpp
        app/petwindow.h
        app/petsettings.cpp
        app/petsettings.h
        app/theme.cpp
        app/theme.h
        app/agentregistry.h
        assets/branding/branding.qrc
        assets/default/pet.qrc
    )

    function(at_pet_add_test name source)
        qt_add_executable(${name} tests/${source} ${AT_PET_TEST_SOURCES})
        target_include_directories(${name} PRIVATE app)
        target_compile_definitions(${name} PRIVATE AT_PET_VERSION="${AT_PET_VERSION_STRING}")
        target_link_libraries(${name} PRIVATE Qt6::Widgets Qt6::Test)
        # Keep tests console programs so QTest's PASS/FAIL output stays visible.
        set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
        if(WIN32)
            configure_file(
                assets/branding/app-icon.rc.in
                "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc"
                @ONLY
            )
            configure_file(
                assets/branding/app-icon.ico
                "${CMAKE_CURRENT_BINARY_DIR}/app-icon.ico"
                COPYONLY
            )
            target_sources(${name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc")
        endif()
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endfunction()

    # One executable per suite: QTEST_MAIN expands to main(), so two suites cannot share one
    # binary.
    at_pet_add_test(gravity-sync tst_gravitysync.cpp)
    at_pet_add_test(lookat-sync tst_lookatsync.cpp)
    at_pet_add_test(language-sync tst_languagesync.cpp)
endif()
