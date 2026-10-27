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
