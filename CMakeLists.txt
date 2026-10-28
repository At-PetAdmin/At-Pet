cmake_minimum_required(VERSION 3.21)
project(AtPet VERSION 1.2.0 LANGUAGES CXX)

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
target_compile_definitions(at-pet PRIVATE AT_PET_VERSION="${PROJECT_VERSION}")

install(TARGETS at-pet BUNDLE DESTINATION . RUNTIME DESTINATION bin)

# --- Tests -------------------------------------------------------------------
# Regression test for user-visible UI state sync (Gravity toggle: pet context menu
# <-> Settings dialog). Opt in with -DAT_PET_BUILD_TESTS=ON so normal builds and the
# packaging flow are unaffected.
option(AT_PET_BUILD_TESTS "Build the At-Pet QTest regression tests" OFF)

if(AT_PET_BUILD_TESTS)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    enable_testing()

    qt_add_executable(at-pet-tests
        tests/tst_gravitysync.cpp
        app/settingsdialog.cpp
        app/settingsdialog.h
        app/petwindow.cpp
        app/petwindow.h
        app/theme.cpp
        app/theme.h
        app/agentregistry.h
        assets/branding/branding.qrc
        assets/default/pet.qrc
    )
    target_include_directories(at-pet-tests PRIVATE app)
    target_compile_definitions(at-pet-tests PRIVATE AT_PET_VERSION="${PROJECT_VERSION}")
    target_link_libraries(at-pet-tests PRIVATE Qt6::Widgets Qt6::Test)
    # Keep the test a console program so QTest's PASS/FAIL output stays visible.
    set_target_properties(at-pet-tests PROPERTIES WIN32_EXECUTABLE FALSE)
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
        target_sources(at-pet-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc")
    endif()

    add_test(NAME gravity-sync COMMAND at-pet-tests)
    set_tests_properties(gravity-sync PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
